#include "SSVEPOnlineThread.h"

#include "SSVEPPreprocess.h"
#include "filterbank.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>

namespace {

bool readBinaryMatrix(const QString& path,
                      int rows,
                      int cols,
                      Eigen::MatrixXd& matrix,
                      QString& errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QString("无法打开模型参数文件: %1").arg(path);
        return false;
    }

    const qint64 expectedBytes = static_cast<qint64>(rows) * static_cast<qint64>(cols) * static_cast<qint64>(sizeof(double));
    const QByteArray bytes = file.readAll();
    if (bytes.size() != expectedBytes) {
        errorMessage = QString("模型参数文件大小不匹配: %1 (期待%2字节，实际%3字节)")
                           .arg(path)
                           .arg(expectedBytes)
                           .arg(bytes.size());
        return false;
    }

    matrix.resize(rows, cols);
    std::memcpy(matrix.data(), bytes.constData(), static_cast<size_t>(expectedBytes));
    return true;
}

int electrodeIndexByName(const QString& name)
{
    for (int i = 0; i < EEG_CHANNEL_COUNT; ++i) {
        if (electrodeMap.value(i).compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

QVector<int> buildDecodeChannelIndices(const QStringList& channels)
{
    QVector<int> indices;
    indices.reserve(channels.size());
    for (int i = 0; i < channels.size(); ++i) {
        const int idx = electrodeIndexByName(channels[i]);
        if (idx >= 0) {
            indices.append(idx);
        }
    }
    return indices;
}

Eigen::VectorXd zscore(const Eigen::VectorXd& v)
{
    if (v.size() <= 1) {
        return Eigen::VectorXd::Zero(v.size());
    }
    const double mean = v.mean();
    const Eigen::VectorXd centered = v.array() - mean;
    const double var = centered.array().square().mean();
    const double stdv = std::sqrt(std::max(0.0, var));
    if (stdv <= 1e-12) {
        return Eigen::VectorXd::Zero(v.size());
    }
    return centered / stdv;
}

int argmax1Based(const Eigen::VectorXd& v)
{
    int idx = 0;
    v.maxCoeff(&idx);
    return idx + 1;
}

double maxCoeffOrZero(const Eigen::VectorXd& v)
{
    if (v.size() <= 0) {
        return 0.0;
    }
    return v.maxCoeff();
}

}  // namespace

SSVEPOnlineThread::SSVEPOnlineThread(QObject* parent)
    : QThread(parent),
      m_repetitions(4),
      m_currentTrial(0),
      m_totalTrials(0),
      m_currentBlock(0),
      m_isRunning(false),
      m_isPaused(false),
      m_shouldStop(false),
      m_currentState(static_cast<int>(ExperimentState::Idle))
{
}

SSVEPOnlineThread::~SSVEPOnlineThread()
{
    stopExperiment(NormalFinish);
    if (isRunning()) {
        wait(3000);
    }
}

bool SSVEPOnlineThread::enableHybridFusion() const
{
    return false;
}

void SSVEPOnlineThread::setExperimentParams(const QVector<SSVEPStimulus>& stimuli, int repetitions)
{
    QMutexLocker locker(&m_mutex);

    m_stimuli.clear();
    QVector<SSVEPStimulus> baseStimuli = stimuli;
    for (int rep = 0; rep < repetitions; ++rep) {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(baseStimuli.begin(), baseStimuli.end(), g);
        m_stimuli.append(baseStimuli);
    }

    m_repetitions = std::max(1, repetitions);
    m_totalTrials = m_stimuli.size();
    m_currentTrial = 0;
    m_currentBlock = 0;
}

bool SSVEPOnlineThread::setModelPackage(const QString& modelPackageDir, QString& errorMessage)
{
    return loadModelPackage(modelPackageDir, errorMessage);
}

void SSVEPOnlineThread::setDecodeChannels(const QStringList& channels)
{
    QMutexLocker locker(&m_mutex);
    m_decodeChannels = channels;
    m_decodeChannelIndices = buildDecodeChannelIndices(channels);
}

void SSVEPOnlineThread::startExperiment()
{
    QMutexLocker locker(&m_mutex);
    if (m_stimuli.isEmpty() || !m_model.valid) {
        return;
    }

    m_recentData.clear();
    m_recentGaze.clear();
    m_collectedData.clear();
    m_eventMarkers.clear();

    m_isRunning = true;
    m_shouldStop = false;
    m_isPaused = false;
    m_currentTrial = 0;
    m_currentBlock = 0;
    m_stimulusOnsetPending = false;

    start();
}

bool SSVEPOnlineThread::isHybridAssetsLoaded() const
{
    return m_hybridEnabled;
}

void SSVEPOnlineThread::pauseExperiment()
{
    QMutexLocker locker(&m_mutex);
    m_isPaused = true;
}

void SSVEPOnlineThread::stopExperiment(StopReason reason)
{
    bool wasRunning = false;
    {
        QMutexLocker locker(&m_mutex);
        wasRunning = m_isRunning;
        m_shouldStop = true;
        m_isRunning = false;
        m_stimulusOnsetPending = false;
        m_pauseCondition.wakeAll();
    }

    if (wasRunning) {
        emit stimulusStateChanged(-1, false, 0.0);
        emit experimentStateChanged(static_cast<int>(ExperimentState::Idle));

        if (reason == UserStop && !m_collectedData.isEmpty()) {
            emit experimentInterrupted(m_collectedData, m_eventMarkers);
        }
    }
}

void SSVEPOnlineThread::handleEEGData(const EEG_PACKET& packet)
{
    QMutexLocker locker(&m_mutex);
    if (!m_isRunning || m_shouldStop) {
        return;
    }

    m_recentData.append(packet);
    m_collectedData.append(packet);

    // 保留最近15秒数据用于在线切片，避免列表无限增长。
    const qint64 keepAfterMs = static_cast<qint64>(packet.timestamp) - 15000;
    while (!m_recentData.isEmpty() && static_cast<qint64>(m_recentData.first().timestamp) < keepAfterMs) {
        m_recentData.removeFirst();
    }
}

void SSVEPOnlineThread::handleGazeData(double x, double y, qint64 hostTimestampMs)
{
    QMutexLocker locker(&m_mutex);
    if (!m_isRunning || m_shouldStop) {
        return;
    }

    GazeSample s;
    s.hostTimestampMs = hostTimestampMs;
    s.x = x;
    s.y = y;
    m_recentGaze.append(s);

    const qint64 keepAfterMs = hostTimestampMs - 15000;
    while (!m_recentGaze.isEmpty() && m_recentGaze.first().hostTimestampMs < keepAfterMs) {
        m_recentGaze.removeFirst();
    }
}

void SSVEPOnlineThread::confirmStimulusOnset(int targetId, qint64 timestampMs)
{
    QMutexLocker locker(&m_mutex);
    if (!m_stimulusOnsetPending || targetId != m_pendingStimulusTarget || timestampMs <= 0) {
        return;
    }
    m_confirmedStimulusOnsetMs = timestampMs;
    m_stimulusOnsetPending = false;
    m_pauseCondition.wakeAll();
}

QList<EEG_PACKET> SSVEPOnlineThread::getCollectedData() const
{
    QMutexLocker locker(&m_mutex);
    return m_collectedData;
}

QList<EventMarker> SSVEPOnlineThread::getMarkers() const
{
    QMutexLocker locker(&m_mutex);
    return m_eventMarkers;
}

void SSVEPOnlineThread::run()
{
    initializeExperiment();

    emit experimentStateChanged(static_cast<int>(ExperimentState::Instruction));
    bool instructionDone = false;
    while (!instructionDone) {
        {
            QMutexLocker locker(&m_mutex);
            if (!m_isRunning || m_shouldStop) {
                return;
            }
            if (m_currentState == static_cast<int>(ExperimentState::TrialCue)) {
                instructionDone = true;
            }
            if (m_isPaused && !m_shouldStop) {
                m_pauseCondition.wait(&m_mutex, 100);
                continue;
            }
        }
        msleep(50);
    }

    const int totalBlocks = std::max(1, m_repetitions);
    const int trialsPerBlock = (totalBlocks > 0) ? (m_totalTrials / totalBlocks) : m_totalTrials;

    while (true) {
        {
            QMutexLocker locker(&m_mutex);
            if (!m_isRunning || m_shouldStop || m_currentBlock >= totalBlocks) {
                break;
            }
        }

        const int startTrial = m_currentBlock * trialsPerBlock;
        int endTrial = (m_currentBlock + 1) * trialsPerBlock;
        if (endTrial > m_totalTrials) {
            endTrial = m_totalTrials;
        }

        sendMarker(1000 + (m_currentBlock + 1), QString("ONLINE_BLOCK_%1_START").arg(m_currentBlock + 1));

        for (int trial = startTrial; trial < endTrial; ++trial) {
            {
                QMutexLocker locker(&m_mutex);
                if (!m_isRunning || m_shouldStop) {
                    break;
                }
                if (m_isPaused) {
                    m_pauseCondition.wait(&m_mutex, 100);
                    --trial;
                    continue;
                }
                m_currentTrial = trial;
            }

            executeNextTrial();

            {
                QMutexLocker locker(&m_mutex);
                if (!m_isRunning || m_shouldStop) {
                    break;
                }
            }

            emit experimentProgressUpdated(m_currentTrial + 1, m_totalTrials);
        }

        sendMarker(6000 + (m_currentBlock + 1), QString("ONLINE_BLOCK_%1_END").arg(m_currentBlock + 1));
        ++m_currentBlock;

        if (m_currentBlock < totalBlocks) {
            emit experimentStateChanged(static_cast<int>(ExperimentState::BlockRest));
            m_currentState = static_cast<int>(ExperimentState::BlockRest);

            bool restDone = false;
            while (!restDone) {
                {
                    QMutexLocker locker(&m_mutex);
                    if (!m_isRunning || m_shouldStop) {
                        break;
                    }
                    if (m_currentState == static_cast<int>(ExperimentState::TrialCue)) {
                        restDone = true;
                    }
                    if (m_isPaused && !m_shouldStop) {
                        m_pauseCondition.wait(&m_mutex, 100);
                        continue;
                    }
                }
                msleep(50);
            }
        }
    }

    bool normalFinish = false;
    {
        QMutexLocker locker(&m_mutex);
        normalFinish = m_isRunning && !m_shouldStop;
    }

    if (normalFinish) {
        emit experimentStateChanged(static_cast<int>(ExperimentState::Completed));
        sendMarker(900, "SSVEP_ONLINE_END");
        msleep(3000);

        {
            QMutexLocker locker(&m_mutex);
            m_isRunning = false;
        }

        emit dataReadyForSaving(m_collectedData, m_eventMarkers);
        emit experimentFinished();
    }
}

void SSVEPOnlineThread::initializeExperiment()
{
    sendMarker(100, "SSVEP_ONLINE_START");
    emit experimentStateChanged(static_cast<int>(ExperimentState::Idle));
}

void SSVEPOnlineThread::executeNextTrial()
{
    if (m_stimuli.isEmpty()) {
        return;
    }

    const int stimulusIndex = m_currentTrial % m_stimuli.size();
    const SSVEPStimulus stimulus = m_stimuli[stimulusIndex];
    QElapsedTimer trialTimer;
    trialTimer.start();

    // 0.5s 目标提示（红框）
    emit stimulusStateChanged(stimulus.targetId, false, stimulus.frequency);
    emit experimentStateChanged(static_cast<int>(ExperimentState::TrialCue));
    sendMarker(2000 + stimulus.targetId, QString("ONLINE_TRIAL_%1_CUE_START").arg(stimulus.targetId));
    if (!sleepInterruptible(500)) {
        return;
    }

    // 0.5s 频闪
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
                 QString("ONLINE_TRIAL_%1_FLICKER_START").arg(stimulus.targetId),
                 flickerStartMs);
    if (!sleepInterruptible(500)) {
        return;
    }

    emit stimulusStateChanged(stimulus.targetId, false, stimulus.frequency);
    sendMarker(4000 + stimulus.targetId, QString("ONLINE_TRIAL_%1_FLICKER_END").arg(stimulus.targetId));

    // 保持白色网格显示，直到在线分析完成
    emit experimentStateChanged(static_cast<int>(ExperimentState::TrialRest));

    // 在线解码 + 0.3s 蓝框反馈
    int predictedTarget = -1;
    QString decodeError;
    QString decodeMode = "eeg";
    double eegPeak = 0.0;
    double eyePeak = 0.0;
    int eyeSamples = 0;
    bool hybridUsed = false;
    QElapsedTimer decodeTimer;
    decodeTimer.start();
    const bool decoded = decodeEpochAround(flickerStartMs,
                                           stimulus.targetId,
                                           predictedTarget,
                                           decodeError,
                                           decodeMode,
                                           eegPeak,
                                           eyePeak,
                                           eyeSamples,
                                           hybridUsed);
    const double decodeElapsedMs = static_cast<double>(decodeTimer.nsecsElapsed()) / 1.0e6;

    emit experimentStateChanged(static_cast<int>(ExperimentState::TrialFeedback));
    if (decoded && predictedTarget > 0) {
        emit feedbackReady(predictedTarget);
        sendMarker(7000 + predictedTarget,
                   QString("ONLINE_TRIAL_%1_PREDICT_%2")
                       .arg(stimulus.targetId)
                       .arg(predictedTarget));
    } else {
        emit feedbackReady(-1);
        sendMarker(7099, QString("ONLINE_DECODE_FAILED_%1").arg(decodeError));
    }

    if (!sleepInterruptible(300)) {
        return;
    }

    // 蓝框反馈后给0.5s黑屏，作为进入下一trial前的过渡。
    emit experimentStateChanged(static_cast<int>(ExperimentState::Idle));
    if (!sleepInterruptible(500)) {
        return;
    }

    sendMarker(5000 + stimulus.targetId, QString("ONLINE_TRIAL_%1_END").arg(stimulus.targetId));

    const double trialElapsedMs = static_cast<double>(trialTimer.nsecsElapsed()) / 1.0e6;
    emit trialDecoded(m_currentTrial + 1,
                      stimulus.targetId,
                      predictedTarget,
                      trialElapsedMs,
                      decodeElapsedMs,
                      decoded,
                      decodeError,
                      decodeMode,
                      eegPeak,
                      eyePeak,
                      eyeSamples,
                      hybridUsed);
}

bool SSVEPOnlineThread::decodeEpochAround(qint64 flickerStartMs,
                                          int presentedTargetId,
                                          int& predictedTarget,
                                          QString& errorMessage,
                                          QString& decodeMode,
                                          double& eegPeak,
                                          double& eyePeak,
                                          int& eyeSamples,
                                          bool& hybridUsed)
{
    predictedTarget = -1;
    decodeMode = "eeg";
    eegPeak = 0.0;
    eyePeak = 0.0;
    eyeSamples = 0;
    hybridUsed = false;
    if (!m_model.valid) {
        errorMessage = "模型未加载";
        return false;
    }

    QVector<int> channelIndices;
    QList<EEG_PACKET> snapshot;
    {
        QMutexLocker locker(&m_mutex);
        channelIndices = m_decodeChannelIndices;
        snapshot = m_recentData;
    }

    if (channelIndices.size() != m_model.config.num_chans) {
        errorMessage = QString("通道数量与模型不匹配: channels=%1 model=%2")
                           .arg(channelIndices.size())
                           .arg(m_model.config.num_chans);
        return false;
    }

    const qint64 delayMs = static_cast<qint64>(std::llround(m_model.config.len_delay_s * 1000.0));
    const qint64 gazeMs = static_cast<qint64>(std::llround(m_model.config.len_gaze_s * 1000.0));
    const qint64 segmentStart = flickerStartMs + delayMs;
    const qint64 segmentEnd = segmentStart + gazeMs;

    const int expectedSamples = std::max(1, static_cast<int>(std::llround(m_model.config.fs * m_model.config.len_gaze_s)));
    QList<EEG_PACKET> segment;
    auto collectSegment = [&](const QList<EEG_PACKET>& packets) {
        segment.clear();
        segment.reserve(expectedSamples + 8);
        for (int i = 0; i < packets.size(); ++i) {
            const qint64 ts = static_cast<qint64>(packets[i].timestamp);
            if (ts >= segmentStart && ts < segmentEnd) {
                segment.append(packets[i]);
            }
        }
    };

    collectSegment(snapshot);

    // TrialFlicker结束后立刻解码时，窗口尾部数据可能尚未进入缓冲区。
    // 在白色网格阶段短暂等待数据到齐，避免频繁出现样本不足。
    if (segment.size() < expectedSamples) {
        QElapsedTimer waitTimer;
        waitTimer.start();
        const int maxWaitMs = 500;

        while (segment.size() < expectedSamples && waitTimer.elapsed() < maxWaitMs) {
            if (!sleepInterruptible(5)) {
                errorMessage = "实验已停止";
                return false;
            }

            {
                QMutexLocker locker(&m_mutex);
                snapshot = m_recentData;
            }
            collectSegment(snapshot);
        }
    }

    if (segment.size() < expectedSamples) {
        errorMessage = QString("在线切片样本不足: got=%1 need=%2")
                           .arg(segment.size())
                           .arg(expectedSamples);
        return false;
    }

    const int observedSamples = segment.size();
    if (segment.size() > expectedSamples) {
        segment = segment.mid(0, expectedSamples);
    }

    Eigen::MatrixXd epoch(channelIndices.size(), expectedSamples);
    for (int s = 0; s < expectedSamples; ++s) {
        for (int ch = 0; ch < channelIndices.size(); ++ch) {
            epoch(ch, s) = segment[s].voltage.data[channelIndices[ch]];
        }
    }

    SSVEPPreprocessConfig preprocessCfg;
    preprocessCfg.enable = m_model.config.enable_preprocess;
    preprocessCfg.raw_fs = m_model.config.fs;
    preprocessCfg.window_len_s = m_model.config.len_gaze_s;
    preprocessCfg.target_fs = m_model.config.preprocess_fs;
    preprocessCfg.bp_low_hz = m_model.config.preprocess_bp_low_hz;
    preprocessCfg.bp_high_hz = m_model.config.preprocess_bp_high_hz;
    preprocessCfg.enable_notch = m_model.config.preprocess_notch_enable;
    preprocessCfg.notch_hz = m_model.config.preprocess_notch_hz;
    preprocessCfg.notch_q = m_model.config.preprocess_notch_q;

    double effectiveFs = m_model.config.fs;
    std::string preprocessError;
    if (!preprocessSSVEPEpoch(epoch, preprocessCfg, preprocessError, effectiveFs)) {
        errorMessage = QString::fromStdString(preprocessError);
        return false;
    }

    if (epoch.cols() != m_model.numSmpls) {
        errorMessage = QString("预处理后样本数不匹配: got=%1 model=%2")
                           .arg(epoch.cols())
                           .arg(m_model.numSmpls);
        return false;
    }

    const Eigen::VectorXd eegRho = computeEegRhoFromPreprocessedEpoch(epoch);
    QVector<double> scores;
    scores.reserve(eegRho.size());
    for (int i = 0; i < eegRho.size(); ++i) scores.append(eegRho(i));
    int maxSampleGapMs = 0;
    for (int i = 1; i < segment.size(); ++i)
        maxSampleGapMs = std::max(maxSampleGapMs,
            static_cast<int>(segment[i].timestamp - segment[i - 1].timestamp));
    emit trialScoresReady(m_currentTrial + 1, presentedTargetId, segmentStart, segmentEnd,
                          observedSamples, expectedSamples,
                          static_cast<qint64>(segment.first().timestamp),
                          static_cast<qint64>(segment.last().timestamp), maxSampleGapMs, scores);
    eegPeak = maxCoeffOrZero(eegRho);
    const int eegPred = argmax1Based(eegRho);

    if (!m_hybridEnabled) {
        decodeMode = "eeg";
        predictedTarget = eegPred;
        return predictedTarget > 0;
    }

    if (decodeHybridFromWindow(segmentStart,
                               segmentEnd,
                               eegRho,
                               predictedTarget,
                               eyePeak,
                               eyeSamples,
                               errorMessage)) {
        decodeMode = "hybrid";
        hybridUsed = true;
        return predictedTarget > 0;
    }

    // 若眼动窗口无效，降级为EEG判别。
    decodeMode = "eeg_fallback";
    predictedTarget = eegPred;
    if (!errorMessage.isEmpty()) {
        errorMessage = QString("Hybrid降级EEG: %1").arg(errorMessage);
    }
    return predictedTarget > 0;
}

Eigen::VectorXd SSVEPOnlineThread::computeEegRhoFromPreprocessedEpoch(const Eigen::MatrixXd& epoch) const
{
    const int numTargs = m_model.config.num_targs;
    const int numChans = m_model.config.num_chans;
    const int numFbs = m_model.config.num_fbs;
    const int numSmpls = epoch.cols();

    Eigen::VectorXd fbCoefs(numFbs);
    for (int i = 0; i < numFbs; ++i) {
        fbCoefs(i) = std::pow(static_cast<double>(i + 1), -1.25) + 0.25;
    }

    Eigen::MatrixXd r = Eigen::MatrixXd::Zero(numFbs, numTargs);

    for (int fb = 0; fb < numFbs; ++fb) {
        const Eigen::MatrixXd filteredTest = filterbankEpoch(epoch, m_model.trca.fs, fb + 1);

        Eigen::MatrixXd Wfb(numChans, numTargs);
        for (int cls = 0; cls < numTargs; ++cls) {
            const int wOffset = fb * numTargs * numChans + cls * numChans;
            Wfb.col(cls) = m_model.trca.W.block(wOffset, 0, numChans, 1);
        }

        const Eigen::MatrixXd testProj = filteredTest.transpose() * Wfb; // [samples x targets]

        for (int cls = 0; cls < numTargs; ++cls) {
            Eigen::MatrixXd trainData(numChans, numSmpls);
            for (int ch = 0; ch < numChans; ++ch) {
                const int trainOffset = fb * numChans * numSmpls + ch * numSmpls;
                trainData.row(ch) = m_model.trca.trains.block(cls, trainOffset, 1, numSmpls);
            }

            const Eigen::MatrixXd trainProj = trainData.transpose() * Wfb; // [samples x targets]

            const Eigen::VectorXd x = Eigen::Map<const Eigen::VectorXd>(testProj.data(), testProj.size());
            const Eigen::VectorXd y = Eigen::Map<const Eigen::VectorXd>(trainProj.data(), trainProj.size());
            r(fb, cls) = correlation(x, y);
        }
    }

    return fbCoefs.transpose() * r;
}

int SSVEPOnlineThread::decodeFromPreprocessedEpoch(const Eigen::MatrixXd& epoch) const
{
    return argmax1Based(computeEegRhoFromPreprocessedEpoch(epoch));
}

bool SSVEPOnlineThread::decodeHybridFromWindow(qint64 segmentStartMs,
                                               qint64 segmentEndMs,
                                               const Eigen::VectorXd& eegRho,
                                               int& predictedTarget,
                                               double& eyePeak,
                                               int& eyeSamples,
                                               QString& errorMessage)
{
    predictedTarget = -1;
    eyePeak = 0.0;
    eyeSamples = 0;
    if (!m_hybridEnabled || m_eyeTemplates.isEmpty()) {
        errorMessage = "Hybrid模板不可用";
        return false;
    }

    QList<GazeSample> gazeSnapshot;
    {
        QMutexLocker locker(&m_mutex);
        gazeSnapshot = m_recentGaze;
    }

    double sx = 0.0;
    double sy = 0.0;
    int cnt = 0;
    for (int i = 0; i < gazeSnapshot.size(); ++i) {
        const GazeSample& g = gazeSnapshot[i];
        if (g.hostTimestampMs >= segmentStartMs && g.hostTimestampMs < segmentEndMs) {
            if (std::isfinite(g.x) && std::isfinite(g.y)) {
                sx += g.x;
                sy += g.y;
                ++cnt;
            }
        }
    }
    eyeSamples = cnt;

    if (cnt <= 0) {
        errorMessage = "眼动窗口无有效样本";
        return false;
    }

    const double gx = sx / static_cast<double>(cnt);
    const double gy = sy / static_cast<double>(cnt);

    const int n = std::min(static_cast<int>(m_eyeTemplates.size()), static_cast<int>(eegRho.size()));
    if (n <= 0) {
        errorMessage = "眼动模板数量无效";
        return false;
    }

    Eigen::VectorXd eyeInv(n);
    for (int i = 0; i < n; ++i) {
        const double dx = gx - m_eyeTemplates[i].x();
        const double dy = gy - m_eyeTemplates[i].y();
        const double d = std::sqrt(dx * dx + dy * dy);
        eyeInv(i) = 1.0 / (d + 1e-6);
    }
    eyePeak = maxCoeffOrZero(eyeInv);

    Eigen::VectorXd eegView = eegRho.head(n);
    const Eigen::VectorXd fusion = zscore(eegView) * (m_hybridWeightEeg * m_hybridWeightEeg)
        + zscore(eyeInv) * (m_hybridWeightEye * m_hybridWeightEye);

    predictedTarget = argmax1Based(fusion);
    return predictedTarget > 0;
}

double SSVEPOnlineThread::correlation(const Eigen::VectorXd& x, const Eigen::VectorXd& y) const
{
    if (x.size() != y.size() || x.size() <= 1) {
        return 0.0;
    }

    const double xMean = x.mean();
    const double yMean = y.mean();
    const Eigen::ArrayXd xCentered = x.array() - xMean;
    const Eigen::ArrayXd yCentered = y.array() - yMean;

    const double numerator = (xCentered * yCentered).sum();
    const double xNorm = std::sqrt((xCentered * xCentered).sum());
    const double yNorm = std::sqrt((yCentered * yCentered).sum());

    const double denom = xNorm * yNorm;
    if (denom <= 1e-12) {
        return 0.0;
    }

    return numerator / denom;
}

void SSVEPOnlineThread::sendMarker(int code, const QString& description)
{
    sendMarkerAt(code, description, QDateTime::currentMSecsSinceEpoch());
}

void SSVEPOnlineThread::sendMarkerAt(int code,
                                     const QString& description,
                                     qint64 timestampMs)
{
    EventMarker marker;
    marker.code = code;
    marker.timestamp = timestampMs;
    marker.description = description;

    {
        QMutexLocker locker(&m_mutex);
        m_eventMarkers.append(marker);
    }

    emit eventMarkerTriggered(code, marker.timestamp, description);
}

qint64 SSVEPOnlineThread::waitForStimulusOnset(int targetId)
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

bool SSVEPOnlineThread::sleepInterruptible(int durationMs)
{
    if (durationMs <= 0) {
        QMutexLocker locker(&m_mutex);
        return m_isRunning && !m_shouldStop;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        QMutexLocker locker(&m_mutex);
        if (!m_isRunning || m_shouldStop) {
            return false;
        }
        if (m_isPaused) {
            m_pauseCondition.wait(&m_mutex, 100);
            continue;
        }
        locker.unlock();

        const int remaining = durationMs - static_cast<int>(timer.elapsed());
        msleep(std::max(1, std::min(5, remaining)));
    }

    QMutexLocker locker(&m_mutex);
    return m_isRunning && !m_shouldStop;
}

bool SSVEPOnlineThread::loadModelPackage(const QString& modelPackageDir, QString& errorMessage)
{
    const QDir pkgDir(modelPackageDir);
    if (!pkgDir.exists()) {
        errorMessage = QString("模型目录不存在: %1").arg(modelPackageDir);
        return false;
    }

    const QString configPath = pkgDir.filePath("config.json");
    if (!QFileInfo::exists(configPath)) {
        errorMessage = "模型包缺少 config.json";
        return false;
    }

    QFile cfgFile(configPath);
    if (!cfgFile.open(QIODevice::ReadOnly)) {
        errorMessage = QString("无法打开配置文件: %1").arg(configPath);
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(cfgFile.readAll());
    if (!doc.isObject()) {
        errorMessage = "config.json 格式错误";
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonObject storage = root.value("storage").toObject();
    const QJsonObject trainsObj = storage.value("trains").toObject();
    const QJsonObject wObj = storage.value("W").toObject();

    QString trainsFile = trainsObj.value("file").toString();
    QString wFile = wObj.value("file").toString();
    if (trainsFile.isEmpty()) {
        if (QFileInfo::exists(pkgDir.filePath("trains.bin"))) {
            trainsFile = "trains.bin";
        } else {
            trainsFile = "ssvep_trains.bin";
        }
    }
    if (wFile.isEmpty()) {
        if (QFileInfo::exists(pkgDir.filePath("W.bin"))) {
            wFile = "W.bin";
        } else {
            wFile = "ssvep_W.bin";
        }
    }

    const QString trainsPath = pkgDir.filePath(trainsFile);
    const QString wPath = pkgDir.filePath(wFile);
    if (!QFileInfo::exists(trainsPath) || !QFileInfo::exists(wPath)) {
        errorMessage = "模型包缺少EEG参数文件（支持 trains.bin/W.bin 或 ssvep_trains.bin/ssvep_W.bin）";
        return false;
    }

    LoadedModel loaded;
    loaded.config.fs = root.value("sampling_rate_raw").toDouble(1000.0);
    loaded.config.len_gaze_s = root.value("len_gaze_s").toDouble(0.5);
    loaded.config.len_delay_s = root.value("len_delay_s").toDouble(0.13);
    loaded.config.enable_preprocess = root.value("preprocess_enable").toBool(true);
    loaded.config.preprocess_fs = root.value("sampling_rate_model").toDouble(250.0);
    loaded.config.preprocess_bp_low_hz = root.value("preprocess_bp_low_hz").toDouble(7.0);
    loaded.config.preprocess_bp_high_hz = root.value("preprocess_bp_high_hz").toDouble(70.0);
    loaded.config.preprocess_notch_enable = root.value("preprocess_notch_enable").toBool(true);
    loaded.config.preprocess_notch_hz = root.value("preprocess_notch_hz").toDouble(50.0);
    loaded.config.preprocess_notch_q = root.value("preprocess_notch_q").toDouble(30.0);
    loaded.config.num_fbs = root.value("num_fbs").toInt(5);
    loaded.config.num_targs = root.value("num_targs").toInt(40);
    loaded.config.num_chans = root.value("num_chans").toInt(9);
    loaded.config.is_ensemble = true;
    loaded.numSmpls = root.value("num_smpls").toInt(static_cast<int>(std::llround(loaded.config.len_gaze_s * loaded.config.preprocess_fs)));

    const int trainsRows = trainsObj.value("rows").toInt(0);
    const int trainsCols = trainsObj.value("cols").toInt(0);
    const int wRows = wObj.value("rows").toInt(0);
    const int wCols = wObj.value("cols").toInt(0);
    if (trainsRows <= 0 || trainsCols <= 0 || wRows <= 0 || wCols <= 0) {
        errorMessage = "模型维度信息无效";
        return false;
    }

    QString readError;
    if (!readBinaryMatrix(trainsPath, trainsRows, trainsCols, loaded.trca.trains, readError)) {
        errorMessage = readError;
        return false;
    }
    if (!readBinaryMatrix(wPath, wRows, wCols, loaded.trca.W, readError)) {
        errorMessage = readError;
        return false;
    }

    loaded.trca.num_fbs = loaded.config.num_fbs;
    loaded.trca.fs = loaded.config.preprocess_fs;
    loaded.trca.num_targs = loaded.config.num_targs;
    loaded.valid = true;

    // Optional Hybrid assets.
    m_hybridEnabled = false;
    m_eyeTemplates.clear();
    m_hybridWeightEeg = 0.5;
    m_hybridWeightEye = 0.5;

    const QString eyeTplPath = pkgDir.filePath("eye_templates.csv");
    const QString fusionPath = pkgDir.filePath("fusion.json");
    if (enableHybridFusion() && QFileInfo::exists(eyeTplPath) && QFileInfo::exists(fusionPath)) {
        QVector<QPointF> templates;

        QFile eyeFile(eyeTplPath);
        if (!eyeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            errorMessage = QString("无法打开眼动模板文件: %1").arg(eyeTplPath);
            return false;
        }

        QTextStream eyeIn(&eyeFile);
        if (!eyeIn.atEnd()) {
            eyeIn.readLine();
        }
        while (!eyeIn.atEnd()) {
            const QString line = eyeIn.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }
            const QStringList f = line.split(',');
            if (f.size() < 5) {
                continue;
            }
            bool okX = false;
            bool okY = false;
            bool okValid = false;
            const double x = f[1].toDouble(&okX);
            const double y = f[2].toDouble(&okY);
            const int valid = f[4].toInt(&okValid);
            if (okX && okY && okValid && valid != 0) {
                templates.append(QPointF(x, y));
            }
        }

        QFile fusionFile(fusionPath);
        if (!fusionFile.open(QIODevice::ReadOnly)) {
            errorMessage = QString("无法打开融合参数文件: %1").arg(fusionPath);
            return false;
        }
        const QJsonDocument fusionDoc = QJsonDocument::fromJson(fusionFile.readAll());
        if (!fusionDoc.isObject()) {
            errorMessage = "fusion.json 格式错误";
            return false;
        }
        const QJsonObject fusionObj = fusionDoc.object();
        m_hybridWeightEeg = fusionObj.value("weight_eeg").toDouble(0.5);
        m_hybridWeightEye = fusionObj.value("weight_eye").toDouble(0.5);

        if (!templates.isEmpty()) {
            m_eyeTemplates = templates;
            m_hybridEnabled = true;
        }
    }

    m_modelPackageDir = modelPackageDir;
    m_model = loaded;

    if (m_decodeChannels.isEmpty()) {
        m_decodeChannels = QStringList() << "Pz" << "POz" << "O1" << "O2"
                                         << "PO5" << "PO3" << "PO4" << "PO6" << "Oz";
    }
    m_decodeChannelIndices = buildDecodeChannelIndices(m_decodeChannels);
    return true;
}
