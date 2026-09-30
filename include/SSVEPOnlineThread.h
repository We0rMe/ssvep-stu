#ifndef SSVEPONLINETHREAD_H
#define SSVEPONLINETHREAD_H

#include <QElapsedTimer>
#include <QList>
#include <QMutex>
#include <QPointF>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

#include "EEG_DataStruct.h"
#include "SSVEPModelTrainer.h"
#include "SSVEPOfflineThread.h"

class SSVEPOnlineThread : public QThread
{
    Q_OBJECT

public:
    enum StopReason {
        UserStop,
        EscapePressed,
        NormalFinish,
        ErrorStop
    };

    explicit SSVEPOnlineThread(QObject* parent = nullptr);
    ~SSVEPOnlineThread() override;

    virtual void setExperimentParams(const QVector<SSVEPStimulus>& stimuli, int repetitions);
    virtual bool setModelPackage(const QString& modelPackageDir, QString& errorMessage);
    virtual void setDecodeChannels(const QStringList& channels);

    virtual void startExperiment();
    virtual void pauseExperiment();
    virtual void stopExperiment(StopReason reason = UserStop);

    virtual void handleEEGData(const EEG_PACKET& packet);
    virtual void handleGazeData(double x, double y, qint64 hostTimestampMs);
    void confirmStimulusOnset(int targetId, qint64 timestampMs);

    bool isHybridAssetsLoaded() const;

    QList<EEG_PACKET> getCollectedData() const;
    QList<EventMarker> getMarkers() const;

    void onExperimentStateChanged(int state) { m_currentState = state; }

signals:
    void stimulusStateChanged(int targetId, bool isActive, double frequency);
    void experimentStateChanged(int state);
    void eventMarkerTriggered(int code, qint64 timestamp, const QString& description);
    void experimentProgressUpdated(int currentTrial, int totalTrials);
    void feedbackReady(int targetId);
    void trialDecoded(int trialIndex,
                      int targetId,
                      int predictedId,
                      double trialElapsedMs,
                      double decodeElapsedMs,
                      bool decodeOk,
                      const QString& errorMessage,
                      const QString& decodeMode,
                      double eegPeak,
                      double eyePeak,
                      int eyeSamples,
                      bool hybridEnabled);
    void trialScoresReady(int trialIndex, int presentedTargetId, qint64 windowStartMs, qint64 windowEndMs,
                          int observedSamples, int usedSamples, qint64 firstSampleMs,
                          qint64 lastSampleMs, int maxSampleGapMs, const QVector<double>& eegScores);
    void experimentFinished();
    void dataReadyForSaving(const QList<EEG_PACKET>& eegData, const QList<EventMarker>& markers);
    void experimentInterrupted(const QList<EEG_PACKET>& eegData, const QList<EventMarker>& markers);

protected:
    void run() override;

    virtual bool enableHybridFusion() const;

private:
    struct LoadedModel {
        TRCAModel trca;
        SSVEPTrainingConfig config;
        int numSmpls = 0;
        bool valid = false;
    };

    bool loadModelPackage(const QString& modelPackageDir, QString& errorMessage);
    bool decodeEpochAround(qint64 flickerStartMs,
                           int presentedTargetId,
                           int& predictedTarget,
                           QString& errorMessage,
                           QString& decodeMode,
                           double& eegPeak,
                           double& eyePeak,
                           int& eyeSamples,
                           bool& hybridUsed);
    bool decodeHybridFromWindow(qint64 segmentStartMs,
                                qint64 segmentEndMs,
                                const Eigen::VectorXd& eegRho,
                                int& predictedTarget,
                                double& eyePeak,
                                int& eyeSamples,
                                QString& errorMessage);
    Eigen::VectorXd computeEegRhoFromPreprocessedEpoch(const Eigen::MatrixXd& epoch) const;
    int decodeFromPreprocessedEpoch(const Eigen::MatrixXd& epoch) const;
    double correlation(const Eigen::VectorXd& x, const Eigen::VectorXd& y) const;

    void initializeExperiment();
    void executeNextTrial();
    void sendMarker(int code, const QString& description);
    void sendMarkerAt(int code, const QString& description, qint64 timestampMs);
    qint64 waitForStimulusOnset(int targetId);
    bool sleepInterruptible(int durationMs);

    QVector<SSVEPStimulus> m_stimuli;
    int m_repetitions;
    int m_currentTrial;
    int m_totalTrials;
    int m_currentBlock;

    bool m_isRunning;
    bool m_isPaused;
    bool m_shouldStop;
    mutable QMutex m_mutex;
    QWaitCondition m_pauseCondition;
    bool m_stimulusOnsetPending = false;
    int m_pendingStimulusTarget = -1;
    qint64 m_confirmedStimulusOnsetMs = -1;

    QList<EEG_PACKET> m_recentData;
    struct GazeSample {
        qint64 hostTimestampMs = 0;
        double x = 0.0;
        double y = 0.0;
    };
    QList<GazeSample> m_recentGaze;
    QList<EEG_PACKET> m_collectedData;
    QList<EventMarker> m_eventMarkers;
    int m_currentState;

    QString m_modelPackageDir;
    QStringList m_decodeChannels;
    QVector<int> m_decodeChannelIndices;
    bool m_hybridEnabled = false;
    QVector<QPointF> m_eyeTemplates;
    double m_hybridWeightEeg = 0.5;
    double m_hybridWeightEye = 0.5;
    LoadedModel m_model;
};

#endif
