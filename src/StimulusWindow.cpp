#include "StimulusWindow.h"
#include <QCloseEvent>
#include <QDateTime>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QSurfaceFormat>
#include <QWindow>
#include <algorithm>
#include <cmath>

StimulusWindow::StimulusWindow(QWidget* parent) : QOpenGLWidget(parent)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSwapInterval(1);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(format);
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::Tool);
    setWindowTitle(QStringLiteral("SSVEP 刺激"));
    setFocusPolicy(Qt::StrongFocus);
    connect(this, &QOpenGLWidget::frameSwapped, this, [this] {
        if (m_stage != Stage::Flicker) return;
        if (m_onsetPending) {
            m_onsetPending = false;
            emit stimulusOnset(m_target, QDateTime::currentMSecsSinceEpoch());
        }
        // Same origin as SSVEPStimulusWidget::onFrameSwapped: advance after presentation.
        ++m_frame;
        update();
    });
}

void StimulusWindow::setProgress(int block, int blocks, int trial, int total)
{
    m_block = block; m_blocks = blocks; m_trial = trial; m_total = total;
    update();
}

void StimulusWindow::setAccuracy(int correct, int decoded)
{
    m_correct = correct; m_decoded = decoded;
    update();
}

void StimulusWindow::showStage(Stage stage, int targetId, int predictedId)
{
    m_stage = stage;
    m_target = targetId;
    m_prediction = predictedId;
    if (stage == Stage::Flicker) {
        m_frame = 0;
        m_onsetPending = true;
        const double reportedRefreshRate = QGuiApplication::primaryScreen()->refreshRate();
        const double refreshRate = reportedRefreshRate > 1.0 ? reportedRefreshRate : 60.0;
        m_frameIntervalSec = 1.0 / refreshRate;
    } else {
        m_onsetPending = false;
    }
    update();
}

QRectF StimulusWindow::targetRect(int index) const
{
    // Ported from the original updateGridLayout / resizeGL / worldRectToPixelRect.
    const float aspect = static_cast<float>(width()) / static_cast<float>(height());
    const float worldLeft = aspect >= 1.0f ? -aspect : -1.0f;
    const float worldRight = -worldLeft;
    const float worldBottom = aspect >= 1.0f ? -1.0f : -1.0f / aspect;
    const float worldTop = -worldBottom;
    const int cols = 8, rows = 5;
    const float gapRatio = 0.28f;
    const float worldWidth = worldRight - worldLeft;
    const float worldHeight = worldTop - worldBottom;
    const float availableWidth = worldWidth * 0.78f;
    const float availableHeight = worldHeight * 0.72f;
    const float cellSize = std::min(availableWidth / (cols + gapRatio * (cols - 1)),
                                    availableHeight / (rows + gapRatio * (rows - 1)));
    const float gap = cellSize * gapRatio;
    const float gridWidth = cols * cellSize + (cols - 1) * gap;
    const float gridHeight = rows * cellSize + (rows - 1) * gap;
    const float startX = -gridWidth * 0.5f + cellSize * 0.5f;
    const float startY = gridHeight * 0.5f - cellSize * 0.5f;
    const float x = startX + (index % cols) * (cellSize + gap);
    const float y = startY - (index / cols) * (cellSize + gap);
    const double xScale = width() / static_cast<double>(worldWidth);
    const double yScale = height() / static_cast<double>(worldHeight);
    return QRectF(QPointF((x - 0.5 * cellSize - worldLeft) * xScale,
                          (worldTop - (y + 0.5 * cellSize)) * yScale),
                  QPointF((x + 0.5 * cellSize - worldLeft) * xScale,
                          (worldTop - (y - 0.5 * cellSize)) * yScale));
}

void StimulusWindow::paintGL()
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const bool gridVisible = m_stage == Stage::Cue || m_stage == Stage::Flicker ||
        m_stage == Stage::Feedback || (m_stage == Stage::Rest && m_online);
    if (gridVisible) {
        const double elapsedSeconds = static_cast<double>(m_frame) * m_frameIntervalSec;
        for (int i = 0; i < 40; ++i) {
            const auto& target = m_targets[static_cast<size_t>(i)];
            painter.setPen(Qt::NoPen);
            if (m_stage == Stage::Cue && target.id == m_target) {
                painter.setBrush(QColor(220, 0, 0));
            } else if (m_stage == Stage::Feedback && target.id == m_prediction) {
                // Student project requirement: green feedback, with no character labels.
                painter.setBrush(QColor(0, 190, 85));
            } else {
                float brightness = 1.0f;
                if (m_stage == Stage::Flicker) {
                    constexpr double kTwoPi = 6.28318530717958647692;
                    const double value = 0.5 * (1.0 + std::sin(kTwoPi * target.frequency * elapsedSeconds + target.phase));
                    brightness = static_cast<float>(std::max(0.0, std::min(1.0, value)));
                }
                const int gray = static_cast<int>(brightness * 255.0f);
                painter.setBrush(QColor(gray, gray, gray));
            }
            painter.drawRect(targetRect(i));
        }
    }

    if (m_stage == Stage::Instruction || m_stage == Stage::BlockRest || m_stage == Stage::Finished) {
        painter.setPen(Qt::white);
        QFont heading(QStringLiteral("Microsoft YaHei UI"));
        heading.setPixelSize(qMax(24, height() / 22));
        heading.setBold(true);
        painter.setFont(heading);
        const QString title = m_stage == Stage::Instruction
            ? (m_preview ? QStringLiteral("刺激预览") : m_online ? QStringLiteral("在线测试") : QStringLiteral("离线训练"))
            : m_stage == Stage::BlockRest ? QStringLiteral("本轮完成") : QStringLiteral("已结束");
        painter.drawText(QRectF(width() * .1, height() * .25, width() * .8, height() * .1), Qt::AlignCenter, title);
        heading.setBold(false);
        heading.setPixelSize(qMax(18, height() / 36));
        painter.setFont(heading);
        QString body;
        if (m_stage == Stage::Instruction)
            body = m_online ? QStringLiteral("注视红色方块提示的目标，闪烁结束后查看绿色识别结果。\n\n按空格开始，Esc 退出")
                            : QStringLiteral("注视红色方块提示的目标。\n提示 0.5 秒 → 闪烁 1 秒 → 黑屏 0.5 秒\n\n按空格开始，Esc 退出");
        else if (m_stage == Stage::BlockRest)
            body = QStringLiteral("休息后按空格继续，Esc 退出");
        painter.drawText(QRectF(width() * .08, height() * .40, width() * .84, height() * .36), Qt::AlignCenter, body);
    }
    if (m_online && m_decoded > 0 && m_stage != Stage::Instruction && m_stage != Stage::Blank) {
        QFont feedback(QStringLiteral("Microsoft YaHei UI"));
        feedback.setPixelSize(qMax(16, height() / 45));
        painter.setFont(feedback);
        painter.setPen(QColor(0, 190, 85));
        painter.drawText(QRectF(0, height() * .92, width(), height() * .06), Qt::AlignCenter,
            QStringLiteral("在线正确率：%1/%2 = %3%").arg(m_correct).arg(m_decoded).arg(100.0 * m_correct / m_decoded, 0, 'f', 1));
    }
}

void StimulusWindow::startPreview()
{
    if (m_preview) return;
    m_preview = true;
    m_allowClose = true;
    m_previewTimer.setSingleShot(true);
    connect(this, &StimulusWindow::stopRequested, this, &QWidget::close);
    connect(this, &StimulusWindow::continueRequested, this, [this] {
        if (m_stage != Stage::Instruction) return;
        showStage(Stage::Cue, 1);
        m_previewTimer.start(500);
    });
    connect(this, &StimulusWindow::stimulusOnset, this, [this] { m_previewTimer.start(1000); });
    connect(&m_previewTimer, &QTimer::timeout, this, [this] {
        const int target = m_target;
        if (m_stage == Stage::Cue) showStage(Stage::Flicker, target);
        else if (m_stage == Stage::Flicker) { showStage(Stage::Rest, target); m_previewTimer.start(500); }
        else if (m_stage == Stage::Rest) { showStage(Stage::Cue, target % 40 + 1); m_previewTimer.start(500); }
    });
    showStage(Stage::Instruction);
    activateWindow();
    setFocus();
}

void StimulusWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) { emit stopRequested(); return; }
    if (event->key() == Qt::Key_Space && (m_stage == Stage::Instruction || m_stage == Stage::BlockRest)) {
        emit continueRequested(); return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void StimulusWindow::closeEvent(QCloseEvent* event)
{
    m_previewTimer.stop();
    if (!m_allowClose && m_stage != Stage::Finished) emit stopRequested();
    event->accept();
}
