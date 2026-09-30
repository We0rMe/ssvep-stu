#include "Session.h"
#include "CsvWriter.h"

Session::Session(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<EEG_PACKET>("EEG_PACKET");
    qRegisterMetaType<QList<EEG_PACKET>>("QList<EEG_PACKET>");
    qRegisterMetaType<QList<EventMarker>>("QList<EventMarker>");
}

Session::~Session()
{
    if (m_offline) { m_offline->stopExperiment(SSVEPOfflineThread::ErrorStop); m_offline->wait(); }
    if (m_online) { m_online->stopExperiment(SSVEPOnlineThread::ErrorStop); m_online->wait(); }
}

template<class Engine> void Session::connectEngine(Engine* engine)
{
    connect(engine, &Engine::experimentStateChanged, this, [this](int state) {
        if (m_running) showState(static_cast<ExperimentState>(state));
    });
    connect(engine, &Engine::stimulusStateChanged, this, [this](int target, bool active, double) {
        if (!m_running || !m_window) return;
        m_target = target;
        if (active) m_window->showStage(StimulusWindow::Stage::Flicker, target);
    });
    connect(m_window, &StimulusWindow::stimulusOnset, engine, &Engine::confirmStimulusOnset);
    connect(m_window, &StimulusWindow::continueRequested, this, [this, engine] {
        if (m_running && (m_state == ExperimentState::Instruction || m_state == ExperimentState::BlockRest))
            engine->onExperimentStateChanged(static_cast<int>(ExperimentState::TrialCue));
    });
    connect(engine, &Engine::dataReadyForSaving, this,
        [this](const QList<EEG_PACKET>& packets, const QList<EventMarker>& markers) {
            finish(true, packets, markers);
        });
    connect(engine, &QThread::finished, this, [this, engine] {
        if (m_running) finish(false, engine->getCollectedData(), engine->getMarkers());
    });
}

void Session::start(Mode mode, int blocks, std::shared_ptr<const ModelDecoder> decoder)
{
    if (m_running) return;
    m_mode = mode;
    m_correct = m_totalDecoded = 0;
    m_running = true;
    m_window = new StimulusWindow;
    m_window->setOnlineMode(mode == Mode::Online);
    m_window->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_window, &StimulusWindow::stopRequested, this, &Session::stop);
    // Use the original 40-target protocol, including its randomization and timings.
    const auto stimuli = SSVEPOfflineThread::createDefaultJFPMStimuli();
    if (mode == Mode::Offline) {
        m_offline = new SSVEPOfflineThread(this);
        m_offline->setEyeValidationEnabled(false);
        m_offline->setExperimentParams(stimuli, blocks);
        connectEngine(m_offline);
    } else {
        m_online = new SSVEPOnlineThread(this); // Original base class disables hybrid fusion.
        QString error;
        if (!decoder || !m_online->setModelPackage(decoder->packageDir(), error)) {
            finish(false, {}, {});
            return;
        }
        m_online->setDecodeChannels(ssvepChannelNames());
        m_online->setExperimentParams(stimuli, blocks);
        connectEngine(m_online);
        connect(m_online, &SSVEPOnlineThread::feedbackReady, this, [this](int predicted) {
            if (!m_running || !m_window) return;
            m_prediction = predicted;
            m_window->showStage(StimulusWindow::Stage::Feedback, m_target, predicted);
        });
        connect(m_online, &SSVEPOnlineThread::trialDecoded, this,
            [this](int, int target, int predicted, double, double, bool ok,
                   const QString& error, const QString&, double, double, int, bool) {
                if (!m_running) return;
                ++m_totalDecoded;
                if (ok && target == predicted) ++m_correct;
                if (m_window) m_window->setAccuracy(m_correct, m_totalDecoded);
                emit feedbackUpdated(m_correct, m_totalDecoded, target, predicted, error);
            });
    }
    m_window->showFullScreen();
    m_window->setFocus();
    if (m_offline) m_offline->startExperiment();
    else m_online->startExperiment();
}

void Session::appendPacket(const EEG_PACKET& packet)
{
    if (!m_running) return;
    if (m_offline) m_offline->handleEEGData(packet);
    if (m_online) m_online->handleEEGData(packet);
}

void Session::showState(ExperimentState state)
{
    if (!m_window) return;
    m_state = state;
    using Stage = StimulusWindow::Stage;
    switch (state) {
    case ExperimentState::Instruction: m_window->showStage(Stage::Instruction); break;
    case ExperimentState::TrialCue: m_prediction = -1; m_window->showStage(Stage::Cue, m_target); break;
    case ExperimentState::TrialFlicker: break; // Active-target signal starts the first frame.
    case ExperimentState::TrialRest: m_window->showStage(Stage::Rest); break;
    case ExperimentState::TrialFeedback: m_window->showStage(Stage::Feedback, m_target, -1); break;
    case ExperimentState::BlockRest: m_window->showStage(Stage::BlockRest); break;
    case ExperimentState::Completed: m_window->showStage(Stage::Finished); break;
    default: m_window->showStage(Stage::Blank); break;
    }
}

void Session::stop()
{
    if (!m_running) return;
    if (m_offline) {
        m_offline->stopExperiment(SSVEPOfflineThread::EscapePressed);
        m_offline->wait();
        finish(false, m_offline->getCollectedData(), m_offline->getMarkers());
    } else if (m_online) {
        m_online->stopExperiment(SSVEPOnlineThread::EscapePressed);
        m_online->wait();
        finish(false, m_online->getCollectedData(), m_online->getMarkers());
    }
}

void Session::finish(bool complete, const QList<EEG_PACKET>& packets, const QList<EventMarker>& markers)
{
    if (!m_running) return;
    m_running = false;
    if (m_offline) m_offline->wait();
    if (m_online) m_online->wait();
    if (m_window) {
        m_window->showStage(StimulusWindow::Stage::Finished);
        m_window->close();
    }
    emit sessionEnded(complete, m_mode == Mode::Online, packets, markers, m_correct, m_totalDecoded);
}
