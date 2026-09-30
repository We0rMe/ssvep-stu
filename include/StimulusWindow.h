#pragma once

#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <array>
#include "Targets.h"

class StimulusWindow final : public QOpenGLWidget {
    Q_OBJECT
public:
    enum class Stage { Instruction, Cue, Flicker, Rest, Feedback, BlockRest, Finished, Blank };
    explicit StimulusWindow(QWidget* parent = nullptr);
    void showStage(Stage stage, int targetId = -1, int predictedId = -1);
    void setProgress(int block, int blocks, int trial, int total);
    void setAccuracy(int correct, int decoded);
    void setOnlineMode(bool online) { m_online = online; }
    void startPreview();
signals:
    void continueRequested();
    void stopRequested();
    void stimulusOnset(int targetId, qint64 timestampMs);
protected:
    void paintGL() override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
private:
    QRectF targetRect(int index) const;
    std::array<StimulusTarget, 40> m_targets = makeTargets();
    Stage m_stage = Stage::Instruction;
    int m_target = -1;
    int m_prediction = -1;
    int m_block = 0;
    int m_blocks = 0;
    int m_trial = 0;
    int m_total = 0;
    int m_correct = 0;
    int m_decoded = 0;
    qint64 m_frame = 0;
    double m_frameIntervalSec = 1.0 / 60.0;
    bool m_onsetPending = false;
    bool m_allowClose = false;
    bool m_online = false;
    bool m_preview = false;
    QTimer m_previewTimer;
};
