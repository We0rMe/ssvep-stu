#include "MainWindow.h"
#include "ImpedancePanel.h"
#include "CsvWriter.h"
#include "AppPathResolver.h"
#include "SSVEPModelTrainer.h"
#include <QAction>
#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#if SSVEP_WITH_EEGO
#include "EEGThread.h"
#endif

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("SSVEP"));
    resize(1360, 900);
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("workspace"));
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(20);
    auto* leftPane = new QFrame(central);
    leftPane->setObjectName(QStringLiteral("panel"));
    auto* left = new QVBoxLayout(leftPane);
    left->setContentsMargins(12, 12, 12, 16);
    left->setSpacing(10);
    m_impedance = new ImpedancePanel;
    m_impedanceButton = new QPushButton(QStringLiteral("开始阻抗检测"));
    m_impedanceButton->setObjectName(QStringLiteral("secondaryButton"));
    m_impedanceButton->setMinimumHeight(44);
    left->addWidget(m_impedance, 1);
    left->addWidget(m_impedanceButton);
    layout->addWidget(leftPane, 2);

    auto* rightPane = new QFrame(central);
    rightPane->setObjectName(QStringLiteral("panel"));
    auto* right = new QVBoxLayout(rightPane);
    right->setContentsMargins(30, 24, 30, 24);
    right->setSpacing(12);
    m_hardwareStatus = new QLabel;
    m_modelStatus = new QLabel(QStringLiteral("模型：未加载"));
    m_modelStatus->setWordWrap(true);
    m_resultStatus = new QLabel(QStringLiteral("在线正确率：--"));
    m_resultStatus->setWordWrap(true);
    m_resultStatus->setObjectName(QStringLiteral("accuracyStatus"));
    m_subjectInput = new QLineEdit(QStringLiteral("student01"));
    m_subjectInput->setPlaceholderText(QStringLiteral("被试编号（字母、数字、下划线）"));
    m_blocksInput = new QSpinBox;
    m_blocksInput->setRange(2, 8);
    m_blocksInput->setValue(3);
    m_blocksInput->setSuffix(QStringLiteral(" 轮"));
    m_offlineButton = new QPushButton(QStringLiteral("离线训练"));
    m_onlineButton = new QPushButton(QStringLiteral("在线测试"));
    m_offlineButton->setObjectName(QStringLiteral("primaryButton"));
    m_onlineButton->setObjectName(QStringLiteral("secondaryButton"));
    for (auto* button : {m_offlineButton, m_onlineButton}) {
        button->setMinimumHeight(50);
    }
    right->addStretch();
    auto* subjectLabel = new QLabel(QStringLiteral("被试编号"));
    subjectLabel->setObjectName(QStringLiteral("fieldLabel"));
    right->addWidget(subjectLabel);
    right->addWidget(m_subjectInput);
    auto* blocksLabel = new QLabel(QStringLiteral("训练轮数"));
    blocksLabel->setObjectName(QStringLiteral("fieldLabel"));
    right->addWidget(blocksLabel);
    right->addWidget(m_blocksInput);
    right->addWidget(m_hardwareStatus);
    right->addWidget(m_modelStatus);
    right->addSpacing(12);
    right->addWidget(m_offlineButton);
    right->addWidget(m_onlineButton);
    right->addWidget(m_resultStatus);
    right->addStretch();
    rightPane->setMaximumWidth(390);
    layout->addWidget(rightPane, 1);
    setCentralWidget(central);
    setStyleSheet(R"(
        QMainWindow, QWidget#workspace { background: #f5f5f7; color: #1d1d1f; font-family: "Microsoft YaHei UI"; }
        QFrame#panel { background: white; border: 1px solid #e5e5e8; border-radius: 18px; }
        QLabel { color: #1d1d1f; font-size: 14px; }
        QLabel#sectionTitle { font-size: 17px; font-weight: 600; }
        QLabel#fieldLabel { font-size: 13px; color: #606068; }
        QLineEdit, QSpinBox { background: white; color: #1d1d1f; border: 1px solid #d8d8dd;
                              border-radius: 9px; padding: 8px 12px; min-height: 28px; font-size: 15px; }
        QLineEdit:focus, QSpinBox:focus { border: 2px solid #0066cc; }
        QPushButton { font-size: 16px; font-weight: 600; border-radius: 22px; padding: 8px 18px; }
        QPushButton#primaryButton { color: white; background: #0066cc; border: 1px solid #0066cc; }
        QPushButton#primaryButton:pressed { background: #004f9e; }
        QPushButton#secondaryButton { color: #0066cc; background: white; border: 1px solid #0066cc; }
        QPushButton#secondaryButton:pressed { background: #edf5ff; }
        QPushButton:disabled { color: #96969b; background: #eeeeef; border: 1px solid #dedee1; }
        QPushButton#primaryButton:focus, QPushButton#secondaryButton:focus { border: 2px solid #0066cc; }
        QMessageBox QPushButton { background: white; color: #0066cc; border: 1px solid #c9d5e4;
                                  border-radius: 8px; min-width: 72px; min-height: 28px; padding: 6px 14px; }
        QMessageBox QPushButton:focus { border: 2px solid #0066cc; }
        QLabel#accuracyStatus { background: #e9f7ee; color: #19743c; border-radius: 10px;
                                padding: 14px; font-size: 16px; font-weight: 600; }
        QMenuBar { background: white; color: #1d1d1f; border-bottom: 1px solid #e5e5e8; padding: 5px 15px; }
        QMenuBar::item { padding: 6px 12px; border-radius: 7px; }
        QMenuBar::item:selected { background: #f5f5f7; }
    )");

#if SSVEP_WITH_EEGO
    m_hardwareStatus->setText(QStringLiteral("设备：待连接"));
#else
    m_hardwareStatus->setText(QStringLiteral("设备：未启用"));
#endif
    m_hardwareStatus->setWordWrap(true);

    auto* preview = menuBar()->addAction(QStringLiteral("刺激预览"));
    connect(preview, &QAction::triggered, this, [this] {
        if (m_busy || m_session) return;
        auto* window = new StimulusWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->showFullScreen();
        window->startPreview();
    });
    auto* load = menuBar()->addAction(QStringLiteral("载入已有模型"));
    connect(load, &QAction::triggered, this, [this] {
        if (m_busy || m_session) return;
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 *.pkg 模型目录"), resolveProjectRootDir().filePath("model"));
        if (dir.isEmpty()) return;
        ModelDecoder decoder;
        QString error;
        if (!decoder.load(dir, error)) { QMessageBox::warning(this, QStringLiteral("模型无效"), error); return; }
        m_modelPath = dir;
        m_modelStatus->setText(QStringLiteral("模型：已加载"));
        m_modelStatus->setToolTip(dir);
    });

    connect(m_impedanceButton, &QPushButton::clicked, this, [this] {
        if (m_impedance->detecting()) {
            m_impedance->stopDetection();
            m_impedanceButton->setText(QStringLiteral("开始阻抗检测"));
            return;
        }
        QString error;
        if (!m_impedance->startDetection(error)) {
            QMessageBox::warning(this, QStringLiteral("阻抗检测失败"), error);
            return;
        }
        m_impedanceButton->setText(QStringLiteral("停止阻抗检测"));
    });
    connect(m_impedance, &ImpedancePanel::detectionFailed, this, [this](const QString& error) {
        m_impedanceButton->setText(QStringLiteral("开始阻抗检测"));
        QMessageBox::warning(this, QStringLiteral("阻抗检测中断"), error);
    });
    connect(m_offlineButton, &QPushButton::clicked, this, [this] { startSession(Session::Mode::Offline); });
    connect(m_onlineButton, &QPushButton::clicked, this, [this] { startSession(Session::Mode::Online); });
}

MainWindow::~MainWindow()
{
    if (m_session) m_session->stop();
    m_impedance->stopDetection();
#if SSVEP_WITH_EEGO
    if (m_eeg) { m_eeg->stopAcquisition(); m_eeg->wait(); delete m_eeg; }
#endif
    if (m_worker) m_worker->wait();
}

QString MainWindow::subjectId() const
{
    const QString id = m_subjectInput->text().trimmed();
    return QRegularExpression("^[A-Za-z0-9_-]+$").match(id).hasMatch() ? id : QString();
}

void MainWindow::setBusy(bool busy)
{
    m_busy = busy;
    m_offlineButton->setEnabled(!busy);
    m_onlineButton->setEnabled(!busy);
    m_impedanceButton->setEnabled(!busy);
    m_subjectInput->setEnabled(!busy);
    m_blocksInput->setEnabled(!busy);
}

void MainWindow::startSession(Session::Mode mode)
{
    if (m_busy || m_session) return;
    if (subjectId().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("编号无效"), QStringLiteral("请填写字母、数字、下划线或短横线组成的被试编号"));
        return;
    }
    std::shared_ptr<ModelDecoder> decoder;
    if (mode == Session::Mode::Online) {
        decoder = std::make_shared<ModelDecoder>();
        QString error;
        if (m_modelPath.isEmpty() || !decoder->load(m_modelPath, error)) {
            QMessageBox::warning(this, QStringLiteral("无法在线测试"), m_modelPath.isEmpty() ? QStringLiteral("请先完成离线训练或从菜单载入模型") : error);
            return;
        }
    }
#if SSVEP_WITH_EEGO
    m_impedance->stopDetection();
    m_impedanceButton->setText(QStringLiteral("开始阻抗检测"));
    setBusy(true);
    m_hardwareStatus->setText(QStringLiteral("设备：正在连接 eego 放大器…"));
    m_eeg = new EEGThread(this);
    m_eeg->setChannels(ssvepChannelNames());
    auto started = std::make_shared<bool>(false);
    connect(m_eeg, &EEGThread::connectionChanged, this, [this, mode, decoder, started](bool connected, const QString& message) {
        m_hardwareStatus->setText(QStringLiteral("设备：") + message);
        if (connected && !*started) {
            *started = true;
            QTimer::singleShot(250, this, [this, mode, decoder] {
                if (!m_eeg || !m_eeg->isDeviceConnected()) return;
                m_session = new Session(this);
                connect(m_eeg, &EEGThread::newDataAvailable, m_session, &Session::appendPacket);
                connect(m_session, &Session::sessionEnded, this, &MainWindow::onSessionEnded);
                connect(m_session, &Session::feedbackUpdated, this, [this](int correct, int total, int target, int prediction, const QString& error) {
                    m_resultStatus->setText(QStringLiteral("在线正确率：%1/%2 = %3%\n目标 %4 → 识别 %5%6")
                        .arg(correct).arg(total).arg(100.0 * correct / total, 0, 'f', 1).arg(target)
                        .arg(prediction > 0 ? QString::number(prediction) : QStringLiteral("失败"))
                        .arg(error.isEmpty() ? QString() : QStringLiteral("（") + error + QStringLiteral("）")));
                });
                m_session->start(mode, mode == Session::Mode::Offline ? m_blocksInput->value() : 1, decoder);
            });
        } else if (!connected && m_session && m_session->active()) {
            m_session->stop();
        } else if (!connected && !m_session && m_busy) {
            setBusy(false);
        }
    });
    connect(m_eeg, &EEGThread::errorOccurred, this, [this](const QString& error) {
        m_hardwareStatus->setText(QStringLiteral("设备错误：") + error);
        if (m_session) m_session->stop();
        else {
            if (m_eeg) { m_eeg->stopAcquisition(); m_eeg->wait(); m_eeg->deleteLater(); m_eeg = nullptr; }
            setBusy(false);
        }
    });
    if (!m_eeg->startAcquisition(1000)) {
        m_hardwareStatus->setText(QStringLiteral("设备：采集线程启动失败"));
        delete m_eeg; m_eeg = nullptr;
        setBusy(false);
    }
#else
    Q_UNUSED(mode);
    Q_UNUSED(decoder);
    QMessageBox::information(this, QStringLiteral("需要脑电设备"),
        QStringLiteral("真实离线训练和在线测试需要 eego SDK 与放大器。可先在菜单中预览 40 目标刺激。"));
#endif
}

void MainWindow::onSessionEnded(bool complete, bool online, const QList<EEG_PACKET>& packets,
                                const QList<EventMarker>& markers, int correct, int total)
{
#if SSVEP_WITH_EEGO
    if (m_eeg) {
        disconnect(m_eeg, &EEGThread::newDataAvailable, m_session, &Session::appendPacket);
        m_eeg->stopAcquisition();
        m_eeg->wait();
        m_eeg->deleteLater();
        m_eeg = nullptr;
    }
#endif
    if (m_session) { m_session->deleteLater(); m_session = nullptr; }
    if (packets.isEmpty()) {
        m_hardwareStatus->setText(QStringLiteral("未采集到数据"));
        setBusy(false);
        return;
    }
    const QString id = subjectId();
    const QString modeName = online ? QStringLiteral("online") : QStringLiteral("offline");
    const QString path = resolveProjectRootDir().filePath(QStringLiteral("data/%1/%2/%3_%4.csv")
        .arg(id, modeName, modeName, QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")));
    struct SaveResult { bool saved = false; bool trained = false; QString error; QString modelPath; double accuracy = 0; };
    auto result = std::make_shared<SaveResult>();
    m_hardwareStatus->setText(QStringLiteral("正在保存数据并处理模型…"));
    m_worker = QThread::create([=] {
        result->saved = writeSessionCsv(path, packets, markers, result->error);
        if (!result->saved || online || !complete) return;
        SSVEPModelTrainer trainer;
        SSVEPTrainingReport report;
        SSVEPTrainingConfig config;
        std::string error;
        result->trained = trainer.trainFromCsv(path.toStdString(), id.toStdString(), config, report, error);
        result->accuracy = report.mean_accuracy;
        result->modelPath = QString::fromStdString(report.output_pkg_dir);
        if (!result->trained) result->error = QString::fromStdString(error);
    });
    connect(m_worker, &QThread::finished, this, [this, result, path, complete, online, correct, total] {
        m_worker->deleteLater(); m_worker = nullptr;
        if (!result->saved) {
            m_hardwareStatus->setText(QStringLiteral("数据保存失败：") + result->error);
        } else if (!online && complete && result->trained) {
            m_modelPath = result->modelPath;
            m_modelStatus->setText(QStringLiteral("模型：已训练  ·  交叉验证 %1%").arg(result->accuracy, 0, 'f', 1));
            m_modelStatus->setToolTip(m_modelPath);
            m_hardwareStatus->setText(QStringLiteral("训练完成，数据已保存"));
        } else if (!online && complete) {
            m_hardwareStatus->setText(QStringLiteral("数据已保存，但模型训练失败：") + result->error);
        } else {
            m_hardwareStatus->setText(QStringLiteral("数据已保存"));
        }
        if (online && total > 0)
            m_resultStatus->setText(QStringLiteral("在线正确率：%1/%2 = %3%")
                .arg(correct).arg(total).arg(100.0 * correct / total, 0, 'f', 1));
        setBusy(false);
    });
    m_worker->start();
}
