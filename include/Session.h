#pragma once
#include <QObject>
#include <QPointer>
#include <memory>
#include "EEG_DataStruct.h"
#include "StimulusWindow.h"
#include "ModelDecoder.h"
#include "SSVEPOfflineThread.h"
#include "SSVEPOnlineThread.h"

// Thin UI adapter. Trial timing, markers, buffering and decoding run in the
// unmodified original experiment engines copied into this project.
class Session final : public QObject {
    Q_OBJECT
public:
    enum class Mode { Offline, Online };
    explicit Session(QObject* parent = nullptr);
    ~Session() override;
    void start(Mode mode, int blocks, std::shared_ptr<const ModelDecoder> decoder = {});
    void appendPacket(const EEG_PACKET& packet);
    void stop();
    bool active() const { return m_running; }
signals:
    void feedbackUpdated(int correct, int total, int target, int prediction, const QString& error);
    void sessionEnded(bool complete, bool online, const QList<EEG_PACKET>& packets,
                      const QList<EventMarker>& markers, int correct, int total);
private:
    template<class Engine> void connectEngine(Engine* engine);
    void showState(ExperimentState state);
    void finish(bool complete, const QList<EEG_PACKET>& packets, const QList<EventMarker>& markers);
    QPointer<StimulusWindow> m_window;
    SSVEPOfflineThread* m_offline = nullptr;
    SSVEPOnlineThread* m_online = nullptr;
    Mode m_mode = Mode::Offline;
    ExperimentState m_state = ExperimentState::Idle;
    bool m_running = false;
    int m_target = -1;
    int m_prediction = -1;
    int m_correct = 0;
    int m_totalDecoded = 0;
};
