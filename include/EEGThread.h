#ifndef EEGTHREAD_H
#define EEGTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include <QQueue>
#include <vector>
#include "EEG_DataStruct.h"

// 前向声明，避免包含完整SDK头文件
namespace eemagine {
    namespace sdk {
        class factory;
        class amplifier;
        class stream;
        class buffer;
    }
}

class EEGThread : public QThread
{
    Q_OBJECT

public:
    explicit EEGThread(QObject *parent = nullptr);
    ~EEGThread() override;

    // 启动采集
    bool startAcquisition(int sampleRate = 1000);

    // 暂停采集
    void pauseAcquisition();
    
    // 恢复采集
    void resumeAcquisition();
    
    // 停止采集
    void stopAcquisition();
    
    // 添加标记
    void addMarker(int code, const QString &description = QString(), qint64 sourceTimestamp = -1);
    
    // 设置监视通道
    void setChannels(const QStringList &channels);
    
    // 获取设备信息
    QString getDeviceInfo() const { return m_deviceInfo; }
    
    // 检查是否正在运行
    bool isRunning() const { return m_isRunning; }
    
    // 检查是否暂停
    bool isPaused() const { return m_isPaused; }

    // 检查设备是否连接成功
    bool isDeviceConnected() const { 
        return m_amplifier != nullptr && m_stream != nullptr; 
    }
    
    // 获取连接错误信息
    QString getConnectionError() const { 
        return m_lastConnectionError; 
    }

    int getSamplingRate() const { return m_sampleRate; }
    QStringList getMappedEEGChannels() const;



signals:
    // 新数据可用信号
    void newDataAvailable(const EEG_PACKET &packet);
    
    // 事件标记信号
    void eventMarked(int code, qint64 timestamp, const QString &description);
    
    // 连接状态变化信号
    void connectionChanged(bool connected, const QString &message);
    
    // 错误信号
    void errorOccurred(const QString &error);

protected:
    void run() override;

private:
    // 初始化SDK
    bool initializeSDK();
    
    // 释放SDK资源
    void releaseSDK();
    
    // 处理数据缓冲区
    void processBuffer(const eemagine::sdk::buffer &buffer);

private:
    // 线程控制
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_stopRequested;
    std::atomic<bool> m_isPaused;
    QMutex m_mutex;
    QWaitCondition m_condition;
    
    // SDK组件
    eemagine::sdk::factory *m_factory;
    eemagine::sdk::amplifier *m_amplifier;
    eemagine::sdk::stream *m_stream;
    
    // 采样率
    int m_sampleRate;
    
    // 设备信息
    QString m_deviceInfo;
    
    // 通道配置
    QStringList m_selectedChannels;

    // stream通道下标 -> EEG_PACKET导联下标。-1 表示忽略该stream通道。
    std::vector<int> m_streamToPacketIndex;
    int m_triggerStreamIndex;
    int m_sampleCounterStreamIndex;

    // 采样时基：避免把同一buffer内样本都打成同一毫秒。
    qint64 m_acquisitionStartMs;
    quint64 m_totalSamplesEmitted;
    
    // 事件队列
    QQueue<EventMarker> m_markerQueue;

    // 在私有成员变量中添加
    QString m_lastConnectionError;
};

#endif // EEGTHREAD_H
