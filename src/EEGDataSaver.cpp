#include "EEGDataSaver.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>
#include "AppPathResolver.h"
#include <limits>
#include <algorithm>

namespace {
int findElectrodeIndexByName(const QString &name)
{
    for (auto it = electrodeMap.constBegin(); it != electrodeMap.constEnd(); ++it) {
        if (it.value().compare(name, Qt::CaseInsensitive) == 0) {
            return it.key();
        }
    }
    return -1;
}

bool isOnlineSessionInfo(const QString& sessionInfo)
{
    return sessionInfo.contains("在线");
}
}

EEGDataSaver::EEGDataSaver(QObject *parent)
    : QObject(parent)
    , m_paradigm(ExperimentParadigm::Custom)
    , m_samplingRate(1000.0)
{
}

EEGDataSaver::~EEGDataSaver()
{
}

void EEGDataSaver::setParadigm(ExperimentParadigm paradigm)
{
    m_paradigm = paradigm;
}

void EEGDataSaver::setSubjectInfo(const QString& subjectId, const QString& sessionInfo)
{
    m_subjectId = subjectId;
    m_sessionInfo = sessionInfo;
}

void EEGDataSaver::addExperimentParameter(const QString& key, const QVariant& value)
{
    m_experimentParameters[key] = value;
}

void EEGDataSaver::addEEGPacket(const EEG_PACKET& packet)
{
    m_eegData.append(packet);
}

void EEGDataSaver::addEventMarker(int code, qint64 timestamp, const QString& description)
{
    EventMarker marker;
    marker.code = code;
    marker.timestamp = timestamp;
    marker.description = description;
    m_eventMarkers.append(marker);
}

bool EEGDataSaver::saveData(const QString& filename, SaveFormat format)
{
    // 如果没有指定文件名，则使用推荐的文件名
    QString actualFilename = filename.isEmpty() ? getRecommendedFilename() : filename;
    
    switch (format) {
        case SaveFormat::CSV:
            return saveToCSV(actualFilename);
        case SaveFormat::MAT:
            return saveToMAT(actualFilename);
        case SaveFormat::NPY:
            return saveToNPY(actualFilename);
        default:
            m_lastError = "不支持的文件格式";
            return false;
    }
}

bool EEGDataSaver::saveToCSV(const QString& filename)
{
    if (m_eegData.isEmpty()) {
        m_lastError = "没有数据可保存";
        return false;
    }

    QString actualFilename = filename;
    if (!actualFilename.endsWith(".csv", Qt::CaseInsensitive)) {
        actualFilename += ".csv";
    }

    QFile file(actualFilename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastError = "无法打开文件: " + actualFilename;
        return false;
    }

    QTextStream stream(&file);

    // 写入CSV头 - 移除TimePoint列，只保留通道数据和标记
    QStringList channelNames = getChannelNames();
    for (int i = 0; i < channelNames.size(); i++) {
        stream << channelNames[i];
        if (i < channelNames.size() - 1)
            stream << ",";
    }
    // 常见场景（如静息态）无marker时走快速路径，减少内存复制和主线程阻塞。
    if (m_eventMarkers.isEmpty()) {
        stream << ",Marker\n";

        QVector<int> channelIndices;
        channelIndices.reserve(channelNames.size());
        for (int i = 0; i < channelNames.size(); ++i) {
            int idx = findElectrodeIndexByName(channelNames[i]);
            if (idx < 0 || idx >= EEG_CHANNEL_COUNT) {
                idx = i < EEG_CHANNEL_COUNT ? i : -1;
            }
            channelIndices.append(idx);
        }

        for (const EEG_PACKET& packet : m_eegData) {
            for (int i = 0; i < channelIndices.size(); ++i) {
                const int idx = channelIndices[i];
                const double value = (idx >= 0 && idx < EEG_CHANNEL_COUNT)
                    ? packet.voltage.data[idx]
                    : 0.0;
                stream << value;
                stream << ",";
            }
            stream << "0\n";
        }
    } else {
        int markerColumnCount = 1;

        // 获取数据矩阵
        auto dataMatrix = createDataMatrix(&markerColumnCount);

        // 写入marker列头。若同一样本点可能有多个marker，则展开为多列。
        if (markerColumnCount <= 1) {
            stream << ",Marker\n";
        } else {
            for (int i = 0; i < markerColumnCount; ++i) {
                stream << ",Marker" << (i + 1);
            }
            stream << "\n";
        }

        // 写入数据 - 跳过第一列（TimePoint）
        for (const auto& row : dataMatrix) {
            // 从索引1开始，跳过TimePoint列
            for (int i = 1; i < row.size(); i++) {
                stream << row[i];
                if (i < row.size() - 1)
                    stream << ",";
            }
            stream << "\n";
        }
    }

    stream.flush();
    const bool dataWritten = file.flush() && stream.status() == QTextStream::Ok;
    file.close();
    if (!dataWritten) {
        m_lastError = "数据写入失败，请检查磁盘空间和写入权限: " + actualFilename;
        return false;
    }
    
    // 保存实验元数据
    QString metaFilename = actualFilename;
    metaFilename.replace(".csv", "_metadata.txt");
    
    QFile metaFile(metaFilename);
    if (metaFile.open(QIODevice::WriteOnly)) {
        // 写入UTF-8 BOM (EF BB BF)
        const char bom[] = { '\xEF', '\xBB', '\xBF' };
        metaFile.write(bom, 3);
        
        // 手动构建元数据内容
        QString metaContent;
        metaContent += QString("实验类型: %1\n").arg(getParadigmPrefix());
        metaContent += QString("被试ID: %1\n").arg(m_subjectId);
        metaContent += QString("会话信息: %1\n").arg(m_sessionInfo);
        metaContent += QString("采样率: %1 Hz\n").arg(m_samplingRate);
        metaContent += QString("通道数: %1\n").arg(m_channels.size());
        metaContent += QString("通道列表: %1\n").arg(m_channels.join(", "));
        metaContent += QString("数据点数: %1\n").arg(m_eegData.size());
        metaContent += QString("事件标记数: %1\n").arg(m_eventMarkers.size());
        metaContent += QString("保存时间: %1\n\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
        
        // 写入实验参数
        metaContent += "实验参数:\n";
        for (auto it = m_experimentParameters.begin(); it != m_experimentParameters.end(); ++it) {
            metaContent += QString("  %1: %2\n").arg(it.key()).arg(it.value().toString());
        }
        metaContent += "\n";
        
        // 写入事件标记
        metaContent += "事件标记列表:\n";
        for (const EventMarker& marker : m_eventMarkers) {
            metaContent += QString("  代码: %1, 时间戳: %2, 描述: %3\n")
                          .arg(marker.code)
                          .arg(marker.timestamp)
                          .arg(marker.description);
        }
        
        // 一次性写入所有内容，确保UTF-8编码
        metaFile.write(metaContent.toUtf8());
        metaFile.close();
    }
    
    return true;
}

bool EEGDataSaver::saveToMAT(const QString& filename)
{
    // MAT格式需要使用外部库如matio，这里只是一个示例框架
    QString actualFilename = filename;
    if (!actualFilename.endsWith(".mat", Qt::CaseInsensitive)) {
        actualFilename += ".mat";
    }
    
    qDebug() << "保存到MAT文件功能尚未实现，需要集成matio库";
    m_lastError = "MAT格式保存暂未实现";
    return false;
}

bool EEGDataSaver::saveToNPY(const QString& filename)
{
    // NPY格式需要使用cnpy等库，这里只是一个示例框架
    QString actualFilename = filename;
    if (!actualFilename.endsWith(".npy", Qt::CaseInsensitive)) {
        actualFilename += ".npy";
    }
    
    qDebug() << "保存到NPY文件功能尚未实现，需要集成cnpy库";
    m_lastError = "NPY格式保存暂未实现";
    return false;
}

QVector<QVector<double>> EEGDataSaver::createDataMatrix(int *markerColumnCountOut) const
{
    QVector<QVector<double>> result;
    
    // 没有数据时返回空矩阵
    if (m_eegData.isEmpty())
        return result;
    
    // 计算总样本数
    const int totalSamples = m_eegData.size();
    const int numChannels = m_channels.isEmpty() ? EEG_CHANNEL_COUNT : m_channels.size();

    // 预计算时间戳并检查是否单调，单调时可使用二分查找快速对齐marker
    QVector<qint64> sampleTimestamps(totalSamples);
    bool isMonotonic = true;
    for (int i = 0; i < totalSamples; ++i) {
        sampleTimestamps[i] = static_cast<qint64>(m_eegData[i].timestamp);
        if (i > 0 && sampleTimestamps[i] < sampleTimestamps[i - 1]) {
            isMonotonic = false;
        }
    }

    // 第一步：计算每个marker对应的采样点索引，并统计每个采样点上的marker数量
    QVector<int> markerSampleIndices;
    markerSampleIndices.reserve(m_eventMarkers.size());

    QVector<int> markersPerSample(totalSamples, 0);
    for (const EventMarker& marker : m_eventMarkers) {
        int nearestDataPoint = -1;

        if (isMonotonic) {
            auto it = std::lower_bound(sampleTimestamps.begin(), sampleTimestamps.end(), marker.timestamp);
            if (it == sampleTimestamps.begin()) {
                nearestDataPoint = 0;
            } else if (it == sampleTimestamps.end()) {
                nearestDataPoint = totalSamples - 1;
            } else {
                const int rightIndex = static_cast<int>(it - sampleTimestamps.begin());
                const int leftIndex = rightIndex - 1;
                const qint64 leftDiff = std::llabs(marker.timestamp - sampleTimestamps[leftIndex]);
                const qint64 rightDiff = std::llabs(sampleTimestamps[rightIndex] - marker.timestamp);
                nearestDataPoint = (leftDiff <= rightDiff) ? leftIndex : rightIndex;
            }
        } else {
            qint64 minTimeDiff = std::numeric_limits<qint64>::max();
            for (int i = 0; i < totalSamples; ++i) {
                const qint64 timeDiff = std::llabs(sampleTimestamps[i] - marker.timestamp);
                if (timeDiff < minTimeDiff) {
                    minTimeDiff = timeDiff;
                    nearestDataPoint = i;
                }
            }
        }

        if (nearestDataPoint < 0)
            nearestDataPoint = 0;

        markerSampleIndices.append(nearestDataPoint);
        markersPerSample[nearestDataPoint]++;
    }

    int markerColumnCount = 1;
    for (int count : markersPerSample) {
        markerColumnCount = std::max(markerColumnCount, count);
    }
    if (markerColumnCountOut) {
        *markerColumnCountOut = markerColumnCount;
    }

    // 第二步：分配矩阵。列布局：TimePoint + Channels + Marker1..MarkerN
    const int markerStartCol = numChannels + 1;
    const int totalCols = markerStartCol + markerColumnCount;

    result.resize(totalSamples);
    for (int i = 0; i < totalSamples; ++i) {
        result[i].resize(totalCols);
        for (int c = markerStartCol; c < totalCols; ++c) {
            result[i][c] = 0;
        }
    }

    QVector<int> channelIndices(numChannels, -1);
    if (!m_channels.isEmpty()) {
        for (int i = 0; i < numChannels; ++i) {
            channelIndices[i] = findElectrodeIndexByName(m_channels[i]);
        }
    } else {
        for (int i = 0; i < numChannels && i < EEG_CHANNEL_COUNT; ++i) {
            channelIndices[i] = i;
        }
    }

    // 写入EEG数据
    for (int i = 0; i < totalSamples; ++i) {
        const EEG_PACKET& packet = m_eegData[i];
        result[i][0] = i;
        for (int ch = 0; ch < numChannels; ++ch) {
            const int electrodeIndex = channelIndices[ch];
            if (electrodeIndex >= 0 && electrodeIndex < EEG_CHANNEL_COUNT) {
                result[i][ch + 1] = packet.voltage.data[electrodeIndex];
            } else {
                result[i][ch + 1] = 0.0;
            }
        }
    }

    // 第三步：严格对齐写入marker；同一采样点多个marker写入Marker1..MarkerN
    QVector<int> nextSlotForSample(totalSamples, 0);
    for (int k = 0; k < m_eventMarkers.size(); ++k) {
        const int sampleIndex = markerSampleIndices[k];
        int slot = nextSlotForSample[sampleIndex]++;
        if (slot >= markerColumnCount) {
            slot = markerColumnCount - 1;
        }
        result[sampleIndex][markerStartCol + slot] = m_eventMarkers[k].code;
    }

    int collisionSamples = 0;
    for (int count : markersPerSample) {
        if (count > 1) {
            collisionSamples++;
        }
    }

    qDebug() << "CSV marker映射完成: 总标记=" << m_eventMarkers.size()
             << "marker列数=" << markerColumnCount
             << "多marker采样点数=" << collisionSamples
             << "严格按采样点对齐写入";
    
    return result;
}

QStringList EEGDataSaver::getChannelNames() const
{
    if (!m_channels.isEmpty())
        return m_channels;
    
    // 如果没有设置通道名称，则生成默认通道名称
    QStringList defaultChannels;
    
    // 根据不同范式生成默认通道
    switch (m_paradigm) {
        case ExperimentParadigm::SSVEP:
        case ExperimentParadigm::HYBRID:
            defaultChannels << "Pz" << "POz" << "O1" << "O2" << "PO5" << "PO3" << "PO4" << "PO6" << "Oz";
            break;
        case ExperimentParadigm::P300:
            defaultChannels << "Fz" << "Cz" << "Pz" << "P3" << "P4" << "PO7" << "PO8" << "Oz";
            break;
        case ExperimentParadigm::MI:
            defaultChannels << "C3" << "Cz" << "C4" << "CP3" << "CPz" << "CP4" << "P3" << "Pz" << "P4";
            break;
        case ExperimentParadigm::EMO:
            defaultChannels << "Fp1" << "Fp2" << "F3" << "F4" << "F7" << "F8" << "Fz" << "AF3" << "AF4" << "AF7" << "AF8";
            break;
        case ExperimentParadigm::Custom:
            // 生成通用名称 Ch1, Ch2, ...
            for (int i = 0; i < 64; i++) {
                defaultChannels.append(QString("Ch%1").arg(i+1));
            }
            break;
    }
    
    return defaultChannels;
}

QString EEGDataSaver::getRecommendedFilename() const
{
    // 确保目录存在
    QString directory = ensureDirectoryExists();
    
    // 构造文件名
    QString filename = QString("%1/%2_%3_%4.csv")
                       .arg(directory)
                       .arg(getParadigmPrefix())
                       .arg(m_subjectId.isEmpty() ? "unknown" : m_subjectId)
                       .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    
    return filename;
}

QString EEGDataSaver::ensureDirectoryExists(const QString& baseDir) const
{
    QString directory;
    
    if (baseDir.isEmpty()) {
        QDir dir = resolveProjectRootDir();
        
        // 使用应用程序根目录下的data目录
        directory = dir.absolutePath() + "/data";
        // 如果有被试ID，则添加被试目录
        if (!m_subjectId.isEmpty()) {
            directory += "/" + m_subjectId;
        }

        // 在线/离线数据物理隔离，避免训练误读在线数据。
        directory += isOnlineSessionInfo(m_sessionInfo) ? "/online" : "/offline";
    } else {
        directory = baseDir;
    }
    
    // 确保目录存在
    QDir dir;
    if (!dir.exists(directory)) {
        dir.mkpath(directory);
    }
    
    return directory;
}

QString EEGDataSaver::getParadigmPrefix() const
{
    switch (m_paradigm) {
        case ExperimentParadigm::SSVEP:
            return "ssvep";
        case ExperimentParadigm::HYBRID:
            return "hybrid";
        case ExperimentParadigm::P300:
            return "p300";
        case ExperimentParadigm::MI:
            return "mi";
        case ExperimentParadigm::EMO:
            return "emo";
        case ExperimentParadigm::REST:
            return "rest";
        case ExperimentParadigm::Custom:
        default:
            return "eeg";
    }
}

void EEGDataSaver::clearData()
{
    m_eegData.clear();
    m_eventMarkers.clear();
    m_experimentParameters.clear();
}
