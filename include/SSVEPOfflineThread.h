#ifndef SSVEPOFFLINETHREAD_H
#define SSVEPOFFLINETHREAD_H

#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QWaitCondition>
#include <QVector>
#include <QString>
#include <random>
#include <algorithm>
#include "EEG_DataStruct.h"
#include "EEGThread.h"

enum class ExperimentState {
    Idle,           // 空闲状态
    Instruction,    // 指导语显示状态
    TrialCue,       // 目标提示（红框）
    TrialFlicker,   // 刺激闪烁期
    TrialRest,      // trial内短休息
    TrialFeedback,  // 在线反馈（蓝框）
    BlockRest,      // block间休息
    Completed       // 实验完成
};

// 定义SSVEP刺激结构体
struct SSVEPStimulus {
    int targetId;
    QString label;
    double frequency;
    double phase;       // 初始相位（弧度）
    int cueDurationMs;  // 目标提示持续时间（毫秒）
    int flickerDurationMs; // 刺激持续时间（毫秒）
    int restDurationMs; // 休息时间（毫秒）
    QString description;
};

class SSVEPOfflineThread : public QThread
{
    Q_OBJECT

public:
    // 添加枚举定义停止原因
    enum StopReason {
        UserStop,      // 用户手动停止
        EscapePressed, // 按ESC中断
        NormalFinish,  // 正常完成
        ErrorStop      // 错误导致停止
    };

    enum class EyeValidationDecision {
        Passed,
        RetryCurrentTrial,
        StopAndSave
    };

    explicit SSVEPOfflineThread(QObject *parent = nullptr);
    ~SSVEPOfflineThread();

    static QVector<SSVEPStimulus> createDefaultJFPMStimuli();
    static QVector<SSVEPStimulus> createCursorControlStimuli();
    static QVector<SSVEPStimulus> createCarControlStimuli();

    // 实验控制
    void setExperimentParams(const QVector<SSVEPStimulus>& stimuli, int repetitions);
    void startExperiment();
    void pauseExperiment();
    void stopExperiment(StopReason reason = UserStop);
    void setEyeValidationEnabled(bool enabled) { m_eyeValidationEnabled = enabled; }
    void resolveEyeValidation(EyeValidationDecision decision);
    void confirmStimulusOnset(int targetId, qint64 timestampMs);

    // EEG数据处理
    void handleEEGData(const EEG_PACKET &packet);
    void startDataCollection();
    void stopDataCollection();
    void clearCollectedData();
    QList<EEG_PACKET> getCollectedData() const{ return m_collectedData;}
    QList<EventMarker> getMarkers() const { return m_eventMarkers; }

    // 获取实验参数
    int getRepetitions() const { return m_repetitions; }
    int getStimulusCount() const { return m_stimuli.size() / m_repetitions; }

    void onExperimentStateChanged(int state) { m_currentState = state; }
    
signals:
    void stimulusStateChanged(int targetId, bool isActive, double frequency);
    void experimentStateChanged(int state); // 实验状态改变
    void eventMarkerTriggered(int code, qint64 timestamp, const QString& description);
    void experimentProgressUpdated(int currentTrial, int totalTrials);
    void experimentFinished();
    void eyeValidationRequested(int block, int target, qint64 flickerStartMs);
    // 实验数据准备好可以保存
    void dataReadyForSaving(const QList<EEG_PACKET>& eegData, const QList<EventMarker>& markers);
    void experimentInterrupted(const QList<EEG_PACKET>& eegData, const QList<EventMarker>& markers);
protected:
    void run() override;

private:
    void initializeExperiment();
    void executeNextTrial();
    void sendMarker(int code, const QString& description);
    void sendMarkerAt(int code, const QString& description, qint64 timestampMs);
    qint64 waitForStimulusOnset(int targetId);
    bool sleepInterruptible(int durationMs);

    // 实验参数
    QVector<SSVEPStimulus> m_stimuli;
    int m_repetitions;
    int m_currentTrial;
    int m_totalTrials;
    int m_currentBlock;
    
    // 定时器
    QElapsedTimer m_experimentTimer;
    
    // 线程控制
    bool m_isRunning;
    bool m_isPaused;
    bool m_shouldStop;
    QMutex m_mutex;
    QWaitCondition m_pauseCondition;
    bool m_eyeValidationEnabled = false;
    bool m_eyeValidationPending = false;
    bool m_eyeValidationFailed = false;
    bool m_eyeValidationRetryRequested = false;
    bool m_stimulusOnsetPending = false;
    int m_pendingStimulusTarget = -1;
    qint64 m_confirmedStimulusOnsetMs = -1;

    QList<EEG_PACKET> m_collectedData;  // 收集的EEG数据
    QList<EventMarker> m_eventMarkers;  // 事件标记列表
    bool m_collectData;                 // 是否收集数据的标志
    int m_currentState;                 // 当前实验状态，用于跟踪UI状态变化
};

#endif // SSVEPOFFLINETHREAD_H
