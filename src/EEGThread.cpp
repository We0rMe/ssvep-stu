#include "EEGThread.h"
#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <cstring>

// 使用动态绑定eego-SDK
#define EEGO_SDK_BIND_DYNAMIC
#include "eemagine/sdk/factory.h"
#include "eemagine/sdk/wrapper.h"

using namespace eemagine::sdk;

EEGThread::EEGThread(QObject *parent)
    : QThread(parent)
    , m_isRunning(false)
    , m_stopRequested(false)
    , m_isPaused(false) 
    , m_factory(nullptr)
    , m_amplifier(nullptr)
    , m_stream(nullptr)
    , m_sampleRate(1000)
    , m_triggerStreamIndex(-1)
    , m_sampleCounterStreamIndex(-1)
    , m_acquisitionStartMs(0)
    , m_totalSamplesEmitted(0)
{
}

EEGThread::~EEGThread()
{
    // 停止采集并等待线程结束
    stopAcquisition();
    wait(3000); // 最多等待3秒
    
    // 释放SDK资源
    releaseSDK();
}

bool EEGThread::startAcquisition(int sampleRate)
{
    if (m_isRunning) {
        qDebug() << "脑电采集已在运行中";
        return false;
    }
    
    m_sampleRate = sampleRate;
    m_stopRequested = false;
    m_totalSamplesEmitted = 0;
    m_acquisitionStartMs = QDateTime::currentMSecsSinceEpoch();
    
    // 启动线程
    start(QThread::HighPriority); // 使用高优先级
    return true;
}

// 添加暂停方法
void EEGThread::pauseAcquisition()
{
    QMutexLocker locker(&m_mutex);
    m_isPaused = true;
    qDebug() << "EEG采集已暂停"; 
}

// 添加恢复方法
void EEGThread::resumeAcquisition()
{
    QMutexLocker locker(&m_mutex);
    m_isPaused = false;
    qDebug() << "EEG采集已恢复"; 
}

void EEGThread::stopAcquisition()
{
    if (m_isRunning) {
        QMutexLocker locker(&m_mutex);
        m_stopRequested = true;
        m_condition.wakeAll();
    }
}

void EEGThread::addMarker(int code, const QString &description, qint64 sourceTimestamp)
{
    QMutexLocker locker(&m_mutex);
    
    // 创建事件标记
    EventMarker marker;
    marker.code = code;
    marker.timestamp = (sourceTimestamp >= 0) ? sourceTimestamp : QDateTime::currentMSecsSinceEpoch();
    marker.description = description;
    
    // 添加到队列并发射信号
    m_markerQueue.enqueue(marker);
    emit eventMarked(marker.code, marker.timestamp, marker.description);
    
    qDebug() << "添加事件标记: 代码=" << code << ", 描述=" << description;
}

void EEGThread::setChannels(const QStringList &channels)
{
    QMutexLocker locker(&m_mutex);
    m_selectedChannels = channels;
    qDebug() << "设置监视通道: " << channels.join(", ");
}

bool EEGThread::initializeSDK()
{
    try {
        qDebug() << "初始化eego-SDK...";
        
        // 创建SDK工厂
        m_factory = new factory("eego-SDK.dll");
        if (!m_factory) {
            m_lastConnectionError = "无法创建SDK工厂实例";
            emit errorOccurred(m_lastConnectionError);
            return false;
        }
        
        // 获取放大器
        m_amplifier = m_factory->getAmplifier();
        if (!m_amplifier) {
            m_lastConnectionError = "无法连接脑电放大器，请检查设备连接";
            emit errorOccurred(m_lastConnectionError);
            return false;
        }
        
        // 获取设备信息
        QString serialNumber = QString::fromStdString(m_amplifier->getSerialNumber());
        m_deviceInfo = QString("已连接设备: %1").arg(serialNumber);
        qDebug() << m_deviceInfo;
        emit connectionChanged(true, m_deviceInfo);
        
        // 打开数据流
        m_stream = m_amplifier->OpenEegStream(m_sampleRate);
        if (!m_stream) {
            m_lastConnectionError = "无法打开脑电数据流";
            emit errorOccurred(m_lastConnectionError);
            return false;
        }

        // 基于SDK通道列表建立映射，避免把trigger/sample_counter当EEG导联。
        const std::vector<channel> streamChannels = m_stream->getChannelList();
        m_streamToPacketIndex.assign(streamChannels.size(), -1);
        m_triggerStreamIndex = -1;
        m_sampleCounterStreamIndex = -1;

        int mappedEegCount = 0;
        int ignoredCount = 0;
        for (size_t streamPos = 0; streamPos < streamChannels.size(); ++streamPos) {
            const channel &ch = streamChannels[streamPos];
            const int chIndex = static_cast<int>(ch.getIndex());

            switch (ch.getType()) {
            case channel::reference:
            case channel::bipolar:
                if (chIndex >= 0 && chIndex < EEG_CHANNEL_COUNT) {
                    m_streamToPacketIndex[streamPos] = chIndex;
                    mappedEegCount++;
                } else {
                    ignoredCount++;
                }
                break;
            case channel::trigger:
                if (m_triggerStreamIndex < 0) {
                    m_triggerStreamIndex = static_cast<int>(streamPos);
                }
                ignoredCount++;
                break;
            case channel::sample_counter:
                if (m_sampleCounterStreamIndex < 0) {
                    m_sampleCounterStreamIndex = static_cast<int>(streamPos);
                }
                ignoredCount++;
                break;
            default:
                ignoredCount++;
                break;
            }
        }

        qDebug() << "stream通道总数=" << streamChannels.size()
                 << "映射EEG导联=" << mappedEegCount
                 << "忽略非EEG通道=" << ignoredCount
                 << "trigger通道位置=" << m_triggerStreamIndex
                 << "sample_counter通道位置=" << m_sampleCounterStreamIndex;

        if (mappedEegCount == 0) {
            m_lastConnectionError = "未检测到可映射的EEG导联，请检查设备通道配置";
            emit errorOccurred(m_lastConnectionError);
            return false;
        }
        
        qDebug() << "eego-SDK初始化成功，采样率: " << m_sampleRate << " Hz";
        return true;
        
    } catch (const std::exception &e) {
        m_lastConnectionError = QString("SDK初始化异常: %1").arg(e.what());
        emit errorOccurred(m_lastConnectionError);
        return false;
    } catch (...) {
        m_lastConnectionError = "SDK初始化时发生未知异常";
        emit errorOccurred(m_lastConnectionError);
        return false;
    }
}

void EEGThread::releaseSDK()
{
    // 安全释放资源
    if (m_stream) {
        delete m_stream;
        m_stream = nullptr;
    }
    
    if (m_amplifier) {
        delete m_amplifier;
        m_amplifier = nullptr;
    }
    
    if (m_factory) {
        delete m_factory;
        m_factory = nullptr;
    }
    
    qDebug() << "eego-SDK资源已释放";
}

QStringList EEGThread::getMappedEEGChannels() const
{
    QStringList result;
    for (int index : m_streamToPacketIndex) {
        if (index >= 0 && index < EEG_CHANNEL_COUNT) {
            const QString name = electrodeMap.value(index);
            if (!name.isEmpty() && !result.contains(name)) result.append(name);
        }
    }
    return result;
}

void EEGThread::processBuffer(const buffer &buf)
{
    // 使用SDK提供的方法获取采样点数和通道数
    unsigned int sampleCount = buf.getSampleCount();
    unsigned int channelCount = buf.getChannelCount();
    
    if (sampleCount == 0 || channelCount == 0) {
        return;
    }

    if (m_totalSamplesEmitted == 0) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 firstSampleBackoffMs = static_cast<qint64>(
            ((static_cast<quint64>(sampleCount) - 1ULL) * 1000ULL) / static_cast<quint64>(std::max(1, m_sampleRate))
        );
        m_acquisitionStartMs = nowMs - firstSampleBackoffMs;
    }
    
    // // 添加日志以确认数据获取
    // static int packetCounter = 0;
    // if (packetCounter++ % 300 == 0) {
    //     qDebug() << "处理EEG数据包: #" << packetCounter << ", 采样点数=" << sampleCount << ", 通道数=" << channelCount;
    // }
    
    const unsigned int mapCount = static_cast<unsigned int>(m_streamToPacketIndex.size());

    // 处理所有采样点
    for (unsigned int s = 0; s < sampleCount; s++) {
        // 创建数据包
        EEG_PACKET packet;
        std::memset(&packet.voltage.data, 0, sizeof(packet.voltage.data));

        // 使用固定采样时基生成时间戳，避免同一buffer样本出现相同毫秒。
        const quint64 sampleIndex = m_totalSamplesEmitted++;
        packet.timestamp = static_cast<uint64_t>(
            m_acquisitionStartMs + static_cast<qint64>((sampleIndex * 1000ULL) / static_cast<quint64>(std::max(1, m_sampleRate)))
        );

        // 仅复制映射为EEG导联的stream通道，单位统一为uV。
        const unsigned int upper = std::min(channelCount, mapCount);
        for (unsigned int streamPos = 0; streamPos < upper; ++streamPos) {
            const int packetIndex = m_streamToPacketIndex[streamPos];
            if (packetIndex < 0 || packetIndex >= EEG_CHANNEL_COUNT) {
                continue;
            }

            const double valueV = buf.getSample(streamPos, s);
            packet.voltage.data[packetIndex] = valueV * 1000000.0;
        }

        // 信号在锁外发射，降低采集线程锁竞争和卡顿风险。
        // 发送新数据信号
        emit newDataAvailable(packet);
    }
}

void EEGThread::run()
{
    m_isRunning = true;
    qDebug() << "脑电数据采集线程启动...";
    
    // 初始化SDK
    if (!initializeSDK()) {
        m_isRunning = false;
        qDebug() << "脑电数据采集初始化失败";
        return;
    }
    
    try {
        while (!m_stopRequested) {

            // 检查是否暂停
            {
                QMutexLocker locker(&m_mutex);
                if (m_isPaused) {
                // 暂停状态下，减少CPU使用
                msleep(100);
                continue;
                }
            }
            

            // 获取数据
            buffer buf = m_stream->getData();
            
            // 处理数据
            if (buf.getSampleCount() > 0) {
                processBuffer(buf);
            }
            
            // 处理事件标记
            {
                QMutexLocker locker(&m_mutex);
                while (!m_markerQueue.isEmpty()) {
                    // 处理标记
                    m_markerQueue.dequeue();
                }
            }
            
            // 短暂休息
            msleep(1);
        }
    } catch (const std::exception &e) {
        emit errorOccurred(QString("数据采集异常: %1").arg(e.what()));
        qDebug() << "数据采集异常: " << e.what();
    } catch (...) {
        emit errorOccurred("数据采集时发生未知异常");
        qDebug() << "数据采集时发生未知异常";
    }
    
    // 释放SDK资源
    releaseSDK();
    
    // 通知连接状态变更
    emit connectionChanged(false, "脑电放大器已断开");
    
    m_isRunning = false;
    qDebug() << "脑电数据采集线程结束";
}
