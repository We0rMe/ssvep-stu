#include "SSVEPOfflineThread.h"
#include <QDebug>
#include <QCoreApplication>
#include <QMutexLocker>
#include <QDateTime>
#include <QStringList>
#include <random>
#include <algorithm>
#include <cmath>

SSVEPOfflineThread::SSVEPOfflineThread(QObject *parent)
    : QThread(parent), m_repetitions(1), m_currentTrial(0), m_totalTrials(0), m_currentBlock(0), m_isRunning(false), m_isPaused(false), m_shouldStop(false), m_collectData(false), m_currentState(static_cast<int>(ExperimentState::Idle))
{
    qDebug() << "SSVEPOfflineThread 构造函数";
}

QVector<SSVEPStimulus> SSVEPOfflineThread::createDefaultJFPMStimuli()
{
    const QStringList labels = {
        "A", "B", "C", "D", "E", "F", "G", "H",
        "I", "J", "K", "L", "M", "N", "O", "P",
        "Q", "R", "S", "T", "U", "V", "W", "X",
        "Y", "Z", "0", "1", "2", "3", "4", "5",
        "6", "7", "8", "9", " ", ",", ".", "<-"
    };

    QVector<SSVEPStimulus> stimuli;
    stimuli.reserve(labels.size());

    constexpr double kPi = 3.14159265358979323846;

    for (int i = 0; i < labels.size(); ++i)
    {
        const int row = i / 8;
        const int col = i % 8;

        const double frequency = 8.0 + static_cast<double>(col) + 0.2 * static_cast<double>(row);
        double phaseCoeff = std::fmod(2.0 - 0.25 * static_cast<double>(col) + 0.35 * static_cast<double>(row), 2.0);
        if (phaseCoeff < 0.0)
            phaseCoeff += 2.0;

        SSVEPStimulus stimulus;
        stimulus.targetId = i + 1;
        stimulus.label = labels[i];
        stimulus.frequency = frequency;
        stimulus.phase = phaseCoeff * kPi;
        stimulus.cueDurationMs = 500;
        stimulus.flickerDurationMs = 1000;
        stimulus.restDurationMs = 500;
        stimulus.description = QString("TARGET_%1_FREQ_%2_PHASE_%3PI")
                                  .arg(labels[i])
                                  .arg(frequency, 0, 'f', 1)
                                  .arg(phaseCoeff, 0, 'f', 2);
        stimuli.append(stimulus);
    }

    return stimuli;
}

QVector<SSVEPStimulus> SSVEPOfflineThread::createCursorControlStimuli()
{
    struct CursorStimDef {
        const char* label;
        double frequency;
        double phaseCoeffPi;
    };

    const CursorStimDef defs[] = {
        {"Left", 8.0, 0.0},
        {"Right", 10.0, 1.5},
        {"Type", 12.0, 1.0},
        {"Back", 14.0, 0.5}
    };

    QVector<SSVEPStimulus> stimuli;
    stimuli.reserve(4);

    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < 4; ++i)
    {
        SSVEPStimulus stimulus;
        stimulus.targetId = i + 1;
        stimulus.label = QString::fromLatin1(defs[i].label);
        stimulus.frequency = defs[i].frequency;
        stimulus.phase = defs[i].phaseCoeffPi * kPi;
        stimulus.cueDurationMs = 500;
        stimulus.flickerDurationMs = 1000;
        stimulus.restDurationMs = 500;
        stimulus.description = QString("CURSOR_%1_FREQ_%2_PHASE_%3PI")
                                  .arg(stimulus.label)
                                  .arg(stimulus.frequency, 0, 'f', 1)
                                  .arg(defs[i].phaseCoeffPi, 0, 'f', 2);
        stimuli.append(stimulus);
    }

    return stimuli;
}

QVector<SSVEPStimulus> SSVEPOfflineThread::createCarControlStimuli()
{
    struct CarStimDef {
        const char* label;
        double frequency;
        double phaseCoeffPi;
    };

    const CarStimDef defs[] = {
        {"Left", 8.0, 0.0},
        {"Stop", 12.0, 1.0},
        {"Right", 9.0, 1.75},
        {"Up", 10.0, 1.5},
        {"Down", 14.0, 0.5}
    };

    QVector<SSVEPStimulus> stimuli;
    stimuli.reserve(5);

    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < 5; ++i)
    {
        SSVEPStimulus stimulus;
        stimulus.targetId = i + 1;
        stimulus.label = QString::fromLatin1(defs[i].label);
        stimulus.frequency = defs[i].frequency;
        stimulus.phase = defs[i].phaseCoeffPi * kPi;
        stimulus.cueDurationMs = 500;
        stimulus.flickerDurationMs = 1000;
        stimulus.restDurationMs = 500;
        stimulus.description = QString("CAR_%1_FREQ_%2_PHASE_%3PI")
                              .arg(stimulus.label)
                              .arg(stimulus.frequency, 0, 'f', 1)
                              .arg(defs[i].phaseCoeffPi, 0, 'f', 2);
        stimuli.append(stimulus);
    }

    return stimuli;
}

SSVEPOfflineThread::~SSVEPOfflineThread()
{
    qDebug() << "SSVEPOfflineThread 析构函数";
    stopExperiment();
    if (isRunning())
    {
        wait(3000);
    }
}

void SSVEPOfflineThread::setExperimentParams(const QVector<SSVEPStimulus> &stimuli, int repetitions)
{
    QMutexLocker locker(&m_mutex);

    // 清空原有刺激序列
    m_stimuli.clear();

    // 为每个重复生成随机化的刺激序列
    QVector<SSVEPStimulus> baseStimuli = stimuli;

    for (int rep = 0; rep < repetitions; rep++)
    {
        // 随机打乱每轮的刺激顺序
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(baseStimuli.begin(), baseStimuli.end(), g);

        // 添加到总序列
        m_stimuli.append(baseStimuli);
    }

    m_repetitions = std::max(1, repetitions);
    m_totalTrials = m_stimuli.size();
    m_currentTrial = 0;
    m_currentBlock = 0;

    qDebug() << "实验参数设置完成 - block数:" << m_repetitions << "总试验数:" << m_totalTrials;
}

void SSVEPOfflineThread::startExperiment()
{
    qDebug() << "开始SSVEP实验";

    // 清理之前可能残留的数据
    clearCollectedData();

    m_isRunning = true;
    m_shouldStop = false;
    m_isPaused = false;
    m_currentTrial = 0;
    m_currentBlock = 0;

    // 开始收集数据
    m_eyeValidationPending = false;
    m_eyeValidationFailed = false;
    m_eyeValidationRetryRequested = false;
    m_stimulusOnsetPending = false;
    startDataCollection();

    start();
}

void SSVEPOfflineThread::pauseExperiment()
{
    qDebug() << "暂停SSVEP实验";
    QMutexLocker locker(&m_mutex);
    m_isPaused = true;
}

void SSVEPOfflineThread::resolveEyeValidation(EyeValidationDecision decision)
{
    QMutexLocker locker(&m_mutex);
    m_eyeValidationFailed = decision == EyeValidationDecision::StopAndSave;
    m_eyeValidationRetryRequested = decision == EyeValidationDecision::RetryCurrentTrial;
    m_eyeValidationPending = false;
    m_pauseCondition.wakeAll();
}

void SSVEPOfflineThread::confirmStimulusOnset(int targetId, qint64 timestampMs)
{
    QMutexLocker locker(&m_mutex);
    if (!m_stimulusOnsetPending || targetId != m_pendingStimulusTarget || timestampMs <= 0) {
        return;
    }
    m_confirmedStimulusOnsetMs = timestampMs;
    m_stimulusOnsetPending = false;
    m_pauseCondition.wakeAll();
}

void SSVEPOfflineThread::stopExperiment(StopReason reason)
{
    qDebug() << "请求停止SSVEP实验，原因:" << reason;

    // 保存当前状态以便判断是否需要发送数据
    bool wasRunning = m_isRunning;

    // 立即停止标志
    m_shouldStop = true;
    m_isRunning = false;

    // 停止数据收集
    stopDataCollection();

    // 唤醒可能等待的线程
    m_stimulusOnsetPending = false;
    m_pauseCondition.wakeAll();

    // 发送停止信号（在线程仍可用时）
    if (wasRunning)
    {
        emit stimulusStateChanged(-1, false, 0.0);
        emit experimentStateChanged(static_cast<int>(ExperimentState::Idle));

        // 只有当用户手动点击停止按钮时才询问是否保存数据
        if (reason == UserStop && !m_collectedData.isEmpty())
        {
            emit experimentInterrupted(m_collectedData, m_eventMarkers);
        }
    }

    qDebug() << "SSVEP实验停止请求已发送";
}

void SSVEPOfflineThread::run()
{
    qDebug() << "SSVEP离线训练线程启动";

    initializeExperiment();
    m_experimentTimer.start();

    // 设置初始状态 - 显示指导语
    emit experimentStateChanged(static_cast<int>(ExperimentState::Instruction));

    // 显示指导语并等待空格键
    bool instructionDone = false;
    while (m_isRunning && !m_shouldStop && !instructionDone)
    {
        // 空格键后从指导语进入trial
        if (m_currentState == static_cast<int>(ExperimentState::TrialCue))
        {
            instructionDone = true;
        }

        // 检查暂停状态
        if (m_isPaused && !m_shouldStop)
        {
            QMutexLocker locker(&m_mutex);
            m_pauseCondition.wait(&m_mutex, 100); // 100ms超时
            continue;
        }

        msleep(100); // 等待状态变化
    }

    // 如果被中断则退出
    if (!m_isRunning || m_shouldStop)
    {
        stopDataCollection();
        qDebug() << "在指导语阶段检测到中断信号";
        m_isRunning = false;
        return;
    }

    const int totalBlocks = std::max(1, m_repetitions);
    const int trialsPerBlock = (totalBlocks > 0) ? (m_totalTrials / totalBlocks) : m_totalTrials;

    while (m_isRunning && !m_shouldStop && m_currentBlock < totalBlocks)
    {
        const int startTrial = m_currentBlock * trialsPerBlock;
        int endTrial = (m_currentBlock + 1) * trialsPerBlock;

        // 确保最后一轮不超出总试次数
        if (endTrial > m_totalTrials)
            endTrial = m_totalTrials;

        sendMarker(1000 + (m_currentBlock + 1),
                   QString("BLOCK_%1_START").arg(m_currentBlock + 1));

        // 执行当前block的所有试次
        for (int trial = startTrial; trial < endTrial && m_isRunning && !m_shouldStop; trial++)
        {
            m_currentTrial = trial;

            // 检查暂停状态
            if (m_isPaused && !m_shouldStop)
            {
                QMutexLocker locker(&m_mutex);
                m_pauseCondition.wait(&m_mutex, 100); // 100ms超时
                continue;
            }

            bool retryCurrentTrial = false;
            do {
                {
                    QMutexLocker locker(&m_mutex);
                    m_eyeValidationRetryRequested = false;
                }
                executeNextTrial();
                {
                    QMutexLocker locker(&m_mutex);
                    retryCurrentTrial = m_eyeValidationRetryRequested;
                    if (m_eyeValidationFailed) {
                        break;
                    }
                }
                if (retryCurrentTrial && m_isRunning && !m_shouldStop) {
                    sendMarker(9100 + m_stimuli[m_currentTrial % m_stimuli.size()].targetId,
                               QString("TRIAL_%1_EYE_RETRY")
                                   .arg(m_stimuli[m_currentTrial % m_stimuli.size()].targetId));
                }
            } while (retryCurrentTrial && m_isRunning && !m_shouldStop);

            if (m_eyeValidationFailed || !m_isRunning || m_shouldStop) {
                break;
            }

            // 更新进度
            if (!m_shouldStop)
            {
                emit experimentProgressUpdated(m_currentTrial + 1, m_totalTrials);
            }

        }

        if (m_eyeValidationFailed) {
            break;
        }

        sendMarker(6000 + (m_currentBlock + 1),
                   QString("BLOCK_%1_END").arg(m_currentBlock + 1));

        // 当前block完成，递增block序号
        m_currentBlock++;

        // 如果不是最后一个block，且未被中断，进入轮间休息
        if (m_currentBlock < totalBlocks && m_isRunning && !m_shouldStop)
        {
            emit experimentStateChanged(static_cast<int>(ExperimentState::BlockRest));

            // 重置状态变量，确保需要用户交互
            m_currentState = static_cast<int>(ExperimentState::BlockRest);

            // 等待休息结束（按空格键确认继续）
            bool restDone = false;
            qDebug() << "block间休息开始 - 即将进入block:" << m_currentBlock + 1;

            while (m_isRunning && !m_shouldStop && !restDone)
            {
                // 检查状态是否变为TrialCue（说明用户按下了空格键）
                if (m_currentState == static_cast<int>(ExperimentState::TrialCue))
                {
                    restDone = true;
                    qDebug() << "block间休息结束，空格键触发，继续下一block";
                }

                // 检查暂停状态
                if (m_isPaused && !m_shouldStop)
                {
                    QMutexLocker locker(&m_mutex);
                    m_pauseCondition.wait(&m_mutex, 100);
                    continue;
                }

                msleep(100); // 等待状态变化
            }
        }
    }

    // 质量检查失败时先保存已采数据，再清理线程，避免继续无效采集。
    if (m_eyeValidationFailed && m_isRunning && !m_shouldStop) {
        stopDataCollection();
        sendMarker(901, "SSVEP_EYE_VALIDATION_FAILED");
        m_isRunning = false;
        emit experimentStateChanged(static_cast<int>(ExperimentState::Idle));
        emit dataReadyForSaving(m_collectedData, m_eventMarkers);
        emit experimentFinished();
        return;
    }

    // 所有block完成或实验被中断
    if (m_isRunning && !m_shouldStop)
    {
        // 正常完成实验
        emit experimentStateChanged(static_cast<int>(ExperimentState::Completed));

        // 给用户足够时间查看完成页面
        qDebug() << "实验完成，显示结束页面3秒";
        for (int i = 0; i < 30 && m_isRunning && !m_shouldStop; i++)
        {
            msleep(100); // 等待总计3秒
        }

        // 停止收集数据（在退出run方法前）
        stopDataCollection();
        sendMarker(900, "SSVEP_EXPERIMENT_END");

        m_isRunning = false;
        qDebug() << "SSVEP离线训练正常完成";

        // 发送完成信号
        emit dataReadyForSaving(m_collectedData, m_eventMarkers);
        emit experimentFinished();
    }
    else
    {
        // 实验被中断
        stopDataCollection();
        m_isRunning = false;
        qDebug() << "SSVEP离线训练被中断";
    }
}

void SSVEPOfflineThread::initializeExperiment()
{
    qDebug() << "初始化SSVEP实验";

    // 发送实验开始标记
    sendMarker(100, "SSVEP_EXPERIMENT_START");

    // 设置初始状态
    emit experimentStateChanged(static_cast<int>(ExperimentState::Idle));
}

void SSVEPOfflineThread::executeNextTrial()
{
    if (m_stimuli.isEmpty() || m_shouldStop)
        return;

    // 获取当前刺激参数
    int stimulusIndex = m_currentTrial % m_stimuli.size();
    const SSVEPStimulus &stimulus = m_stimuli[stimulusIndex];

    qDebug() << "执行试验" << (m_currentTrial + 1) << "/" << m_totalTrials
             << "目标:" << stimulus.targetId << stimulus.label
             << "频率:" << stimulus.frequency << "Hz"
             << "相位:" << stimulus.phase;

    // 1. 目标提示阶段
    emit stimulusStateChanged(stimulus.targetId, false, stimulus.frequency);
    emit experimentStateChanged(static_cast<int>(ExperimentState::TrialCue));
    sendMarker(2000 + stimulus.targetId,
               QString("TRIAL_%1_CUE_START").arg(stimulus.targetId));
    if (!sleepInterruptible(stimulus.cueDurationMs))
    {
        qDebug() << "目标提示阶段检测到停止信号";
        return;
    }

    // 2. 闪烁阶段
    {
        QMutexLocker locker(&m_mutex);
        m_stimulusOnsetPending = true;
        m_pendingStimulusTarget = stimulus.targetId;
        m_confirmedStimulusOnsetMs = -1;
    }
    emit experimentStateChanged(static_cast<int>(ExperimentState::TrialFlicker));
    emit stimulusStateChanged(stimulus.targetId, true, stimulus.frequency);
    const qint64 flickerStartMs = waitForStimulusOnset(stimulus.targetId);
    if (flickerStartMs <= 0) {
        return;
    }
    sendMarkerAt(3000 + stimulus.targetId,
                 QString("TRIAL_%1_FLICKER_START").arg(stimulus.targetId),
                 flickerStartMs);

    if (!sleepInterruptible(stimulus.flickerDurationMs))
    {
        qDebug() << "闪烁阶段检测到停止信号";
        return;
    }

    emit stimulusStateChanged(stimulus.targetId, false, stimulus.frequency);
    sendMarker(4000 + stimulus.targetId,
               QString("TRIAL_%1_FLICKER_END").arg(stimulus.targetId));

    // 3. trial内短休息
    emit experimentStateChanged(static_cast<int>(ExperimentState::TrialRest));
    if (!sleepInterruptible(stimulus.restDurationMs))
    {
        qDebug() << "trial短休息阶段检测到停止信号";
        return;
    }

    sendMarker(5000 + stimulus.targetId,
               QString("TRIAL_%1_END").arg(stimulus.targetId));

    if (m_eyeValidationEnabled && m_isRunning && !m_shouldStop) {
        {
            QMutexLocker locker(&m_mutex);
            m_eyeValidationPending = true;
        }
        emit eyeValidationRequested(m_currentBlock + 1, stimulus.targetId, flickerStartMs);
        QMutexLocker locker(&m_mutex);
        while (m_eyeValidationPending && m_isRunning && !m_shouldStop) {
            m_pauseCondition.wait(&m_mutex, 100);
        }
    }
}

void SSVEPOfflineThread::sendMarker(int code, const QString &description)
{
    sendMarkerAt(code, description, QDateTime::currentMSecsSinceEpoch());
}

void SSVEPOfflineThread::sendMarkerAt(int code,
                                      const QString& description,
                                      qint64 timestampMs)
{
    // 只在未停止时发送标记
    if (!m_shouldStop)
    {
        qDebug() << "发送标记:" << code << description;
        // 创建事件标记并存储
        EventMarker marker;
        marker.code = code;
        marker.timestamp = timestampMs;
        marker.description = description;
        m_eventMarkers.append(marker);

        // 发送标记信号
        emit eventMarkerTriggered(code, marker.timestamp, description);
    }
}

qint64 SSVEPOfflineThread::waitForStimulusOnset(int targetId)
{
    QElapsedTimer timeout;
    timeout.start();
    QMutexLocker locker(&m_mutex);
    while (m_stimulusOnsetPending && m_isRunning && !m_shouldStop && timeout.elapsed() < 500) {
        m_pauseCondition.wait(&m_mutex, 20);
    }

    if (!m_isRunning || m_shouldStop) {
        m_stimulusOnsetPending = false;
        return -1;
    }
    if (!m_stimulusOnsetPending && m_pendingStimulusTarget == targetId &&
        m_confirmedStimulusOnsetMs > 0) {
        return m_confirmedStimulusOnsetMs;
    }

    m_stimulusOnsetPending = false;
    const qint64 fallback = QDateTime::currentMSecsSinceEpoch();
    qWarning() << "未收到首帧频闪时间，使用控制线程时间回退，目标:" << targetId;
    return fallback;
}

// 添加方法用于接收EEG数据
void SSVEPOfflineThread::handleEEGData(const EEG_PACKET &packet)
{
    if (m_collectData && m_isRunning)
    {
        m_collectedData.append(packet);
    }
}

// 添加方法控制数据收集
void SSVEPOfflineThread::startDataCollection()
{
    m_collectData = true;
}

void SSVEPOfflineThread::stopDataCollection()
{
    m_collectData = false;
}

// 清理数据
void SSVEPOfflineThread::clearCollectedData()
{
    m_collectedData.clear();
    m_eventMarkers.clear();
}

bool SSVEPOfflineThread::sleepInterruptible(int durationMs)
{
    if (durationMs <= 0)
        return m_isRunning && !m_shouldStop;

    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < durationMs && !m_shouldStop && m_isRunning)
    {
        if (m_isPaused && !m_shouldStop)
        {
            QMutexLocker locker(&m_mutex);
            m_pauseCondition.wait(&m_mutex, 100);
            continue;
        }

        const int remaining = durationMs - static_cast<int>(timer.elapsed());
        const int sleepMs = std::max(1, std::min(5, remaining));
        msleep(sleepMs);
    }

    return m_isRunning && !m_shouldStop;
}
