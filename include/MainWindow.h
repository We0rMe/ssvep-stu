#pragma once

#include <QMainWindow>
#include <QPointer>
#include <QThread>
#include <memory>
#include "Session.h"

class ImpedancePanel;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
#if SSVEP_WITH_EEGO
class EEGThread;
#endif

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
private:
    void startSession(Session::Mode mode);
    void onSessionEnded(bool complete, bool online, const QList<EEG_PACKET>& packets,
                        const QList<EventMarker>& markers, int correct, int total);
    void setBusy(bool busy);
    QString subjectId() const;
    ImpedancePanel* m_impedance = nullptr;
    QLabel* m_hardwareStatus = nullptr;
    QLabel* m_modelStatus = nullptr;
    QLabel* m_resultStatus = nullptr;
    QLineEdit* m_subjectInput = nullptr;
    QSpinBox* m_blocksInput = nullptr;
    QPushButton* m_impedanceButton = nullptr;
    QPushButton* m_offlineButton = nullptr;
    QPushButton* m_onlineButton = nullptr;
    Session* m_session = nullptr;
    QThread* m_worker = nullptr;
    QString m_modelPath;
    bool m_busy = false;
#if SSVEP_WITH_EEGO
    EEGThread* m_eeg = nullptr;
#endif
};
