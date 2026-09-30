#ifndef EEGDATASAVER_H
#define EEGDATASAVER_H

#include <QObject>
#include <QList>
#include <QString>
#include <QMap>
#include <QVector>
#include "EEG_DataStruct.h"

// 实验范式类型
enum class ExperimentParadigm {
    SSVEP,
    HYBRID,
    P300,
    MI,
    EMO,
    REST,
    Custom
};

// 保存格式
enum class SaveFormat {
    CSV,
    MAT,
    NPY
};

class EEGDataSaver : public QObject
{
    Q_OBJECT
    
public:
    explicit EEGDataSaver(QObject *parent = nullptr);
    ~EEGDataSaver();

    // 设置实验范式类型
    void setParadigm(ExperimentParadigm paradigm);
    
    // 设置被试者信息
    void setSubjectInfo(const QString& subjectId, const QString& sessionInfo = QString());
    
    // 设置数据采样率
    void setSamplingRate(int samplingRate) { m_samplingRate = samplingRate; }
    
    // 设置使用的通道
    void setChannels(const QStringList& channels) { m_channels = channels; }
    
    // 添加实验参数信息（对于不同范式可能有不同的参数）
    void addExperimentParameter(const QString& key, const QVariant& value);
    
    // 设置所有EEG数据
    void setEEGData(const QList<EEG_PACKET>& eegData) { m_eegData = eegData; }
    
    // 添加单个EEG数据包
    void addEEGPacket(const EEG_PACKET& packet);
    
    // 设置所有事件标记
    void setEventMarkers(const QList<EventMarker>& markers) { m_eventMarkers = markers; }
    
    // 添加单个事件标记
    void addEventMarker(int code, qint64 timestamp, const QString& description = QString());
    
    // 保存数据（自动选择格式）
    bool saveData(const QString& filename = QString(), SaveFormat format = SaveFormat::CSV);
    
    // 保存数据到CSV文件
    bool saveToCSV(const QString& filename);
    
    // 保存数据到MATLAB .mat文件
    bool saveToMAT(const QString& filename);
    
    // 保存到NumPy .npy文件 (Python兼容)
    bool saveToNPY(const QString& filename);
    
    // 获取推荐的文件名（基于实验类型、被试者ID等）
    QString getRecommendedFilename() const;
    
    // 清除所有数据
    void clearData();
    
private:
    // 创建包含标记的完整数据矩阵
    QVector<QVector<double>> createDataMatrix(int *markerColumnCountOut = nullptr) const;
    
    // 获取通道名称列表
    QStringList getChannelNames() const;
    
    // 自动创建存储目录
    QString ensureDirectoryExists(const QString& baseDir = QString()) const;
    
    // 获取基于范式的文件前缀
    QString getParadigmPrefix() const;
    
    // 私有成员变量
    ExperimentParadigm m_paradigm;
    QList<EEG_PACKET> m_eegData;
    QList<EventMarker> m_eventMarkers;
    QStringList m_channels;
    double m_samplingRate;
    QString m_subjectId;
    QString m_sessionInfo;
    QMap<QString, QVariant> m_experimentParameters;
    QString m_lastError;
};

#endif // EEGDATASAVER_H