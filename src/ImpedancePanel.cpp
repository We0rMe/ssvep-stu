#include "ImpedancePanel.h"
#include "EEG_DataStruct.h"
#include "ui_Impedance.h"
#include <QDialog>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <algorithm>
#include <cmath>

#if SSVEP_WITH_EEGO
#define EEGO_SDK_BIND_DYNAMIC
#include "eemagine/sdk/factory.h"
#endif

ImpedancePanel::ImpedancePanel(QWidget* parent)
    : QWidget(parent), m_canvas(new QDialog), m_values(EEG_CHANNEL_COUNT + 2, -1.0)
{
    setMinimumSize(580, 560);
    Ui::Impedance original;
    original.setupUi(m_canvas.get());
    m_canvas->setFixedSize(1200, 1080);
    original.centralwidget->setGeometry(m_canvas->rect());
    original.DetectBtn->hide();
    original.BackBtn->hide();
    m_canvas->setStyleSheet("QDialog, QWidget#centralwidget { background: white; }");
    // Reuse original image assets, fonts and every label rectangle without re-layout.
    for (auto* label : original.centralwidget->findChildren<QLabel*>()) {
        const QString name = label->objectName();
        if (name.endsWith("_num")) {
            m_valueLabels.insert(name.left(name.size() - 4), label);
            label->setText(QStringLiteral("inf"));
        } else if (name != "head" && !name.isEmpty()) {
            m_electrodeLabels.insert(name, label);
            label->setStyleSheet("border-image: url(:/images/images/circle.png);");
        }
    }
    m_canvas->ensurePolished();
    m_head = m_canvas->findChild<QLabel*>(QStringLiteral("head"));
    m_contentRect = m_head->geometry();
    for (auto* label : m_electrodeLabels) m_contentRect = m_contentRect.united(label->geometry());
    for (auto* label : m_valueLabels) m_contentRect = m_contentRect.united(label->geometry());
    m_contentRect.adjust(-16, -16, 16, 16);
    for (const QString& file : {QStringLiteral("Sensor_positions.png"), QStringLiteral("circle.png"),
         QStringLiteral("circle_green.png"), QStringLiteral("circle_yellow.png"), QStringLiteral("circle_red.png")})
        m_images.insert(file, QPixmap(QStringLiteral(":/images/images/") + file));
    connect(&m_timer, &QTimer::timeout, this, &ImpedancePanel::poll);
}

ImpedancePanel::~ImpedancePanel() { stopDetection(); }

bool ImpedancePanel::startDetection(QString& error)
{
#if SSVEP_WITH_EEGO
    if (m_detecting) return true;
    try {
        m_factory = new eemagine::sdk::factory("eego-SDK.dll");
        m_amplifier = m_factory->getAmplifier();
        m_stream = m_amplifier->OpenImpedanceStream();
        m_detecting = true;
        m_timer.start(100);
        return true;
    } catch (const std::exception& ex) {
        error = QString::fromLocal8Bit(ex.what());
        stopDetection();
        return false;
    }
#else
    error = QStringLiteral("此构建未启用 eego SDK；可先使用刺激预览");
    return false;
#endif
}

void ImpedancePanel::stopDetection()
{
    m_timer.stop();
    m_detecting = false;
#if SSVEP_WITH_EEGO
    delete m_stream; m_stream = nullptr;
    delete m_amplifier; m_amplifier = nullptr;
    delete m_factory; m_factory = nullptr;
#endif
    update();
}

void ImpedancePanel::updateElectrode(const QString& name, double value)
{
    auto* label = m_electrodeLabels.value(name, nullptr);
    auto* valueLabel = m_valueLabels.value(name, nullptr);
    if (valueLabel)
        valueLabel->setText(std::isfinite(value) && value >= 0.0 && value <= 200.0
            ? QString::number(value, 'f', 1) : QStringLiteral("inf"));
    if (label) {
        const QString image = value < 20.0 ? "circle_green.png"
                            : value < 50.0 ? "circle_yellow.png" : "circle_red.png";
        label->setStyleSheet(QStringLiteral("border-image: url(:/images/images/%1);").arg(image));
    }
}

void ImpedancePanel::poll()
{
#if SSVEP_WITH_EEGO
    if (!m_stream) return;
    try {
        const auto buffer = m_stream->getData();
        if (buffer.getSampleCount() <= 0) return;
        const int sample = buffer.getSampleCount() - 1;
        for (int i = 0; i < qMin(EEG_CHANNEL_COUNT + 2, static_cast<int>(buffer.getChannelCount())); ++i) {
            m_values[i] = buffer.getSample(i, sample) / 1000.0;
            updateElectrode(electrodeMap.value(i), m_values[i]);
        }
        update();
    } catch (const std::exception& ex) {
        const QString error = QString::fromLocal8Bit(ex.what());
        stopDetection();
        emit detectionFailed(error);
    }
#endif
}

void ImpedancePanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const qreal scale = std::min(width() / m_contentRect.width(), height() / m_contentRect.height());
    const QPointF offset((width() - m_contentRect.width() * scale) / 2.0,
                         (height() - m_contentRect.height() * scale) / 2.0);
    const auto mapped = [&](const QRect& source) {
        return QRectF(offset + (QPointF(source.topLeft()) - m_contentRect.topLeft()) * scale,
                      QSizeF(source.size()) * scale);
    };
    const auto drawImage = [&](const QRectF& destination, const QString& file) {
        const auto& pixmap = m_images[file];
        painter.drawPixmap(destination, pixmap, QRectF(pixmap.rect()));
    };
    drawImage(mapped(m_head->geometry()), QStringLiteral("Sensor_positions.png"));
    for (auto it = m_electrodeLabels.cbegin(); it != m_electrodeLabels.cend(); ++it) {
        auto* label = it.value();
        QString file = QStringLiteral("circle.png");
        for (const QString& color : {QStringLiteral("green"), QStringLiteral("yellow"), QStringLiteral("red")})
            if (label->styleSheet().contains("circle_" + color + ".png")) file = "circle_" + color + ".png";
        const QRectF area = mapped(label->geometry());
        drawImage(area, file);
        QFont font(QStringLiteral("Microsoft YaHei UI"));
        font.setBold(true);
        font.setPixelSize(qBound(11, qRound(17 * scale), 17));
        painter.setFont(font);
        painter.setPen(Qt::black);
        painter.drawText(area, Qt::AlignCenter, label->text());
    }
    for (auto* label : m_valueLabels) {
        QFont font(QStringLiteral("Microsoft YaHei UI"));
        font.setPixelSize(qBound(10, qRound(15 * scale), 15));
        painter.setFont(font);
        painter.setPen(QColor(45, 45, 48));
        QRectF area = mapped(label->geometry());
        area.setHeight(qMax(area.height(), qreal(painter.fontMetrics().height())));
        painter.drawText(area, Qt::AlignHCenter | Qt::AlignTop, label->text());
    }
}
