#pragma once

#include <QWidget>
#include <QTimer>
#include <QVector>
#include <QString>
#include <memory>
#include <QMap>
#include <QPixmap>
#include <QRectF>

class QDialog;
class QLabel;

#if SSVEP_WITH_EEGO
namespace eemagine { namespace sdk { class factory; class amplifier; class stream; } }
#endif

class ImpedancePanel final : public QWidget {
    Q_OBJECT
public:
    explicit ImpedancePanel(QWidget* parent = nullptr);
    ~ImpedancePanel() override;
    bool startDetection(QString& error);
    void stopDetection();
    bool detecting() const { return m_detecting; }
signals:
    void detectionFailed(const QString& message);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    void poll();
    void updateElectrode(const QString& name, double value);
    std::unique_ptr<QDialog> m_canvas;
    QMap<QString, QLabel*> m_electrodeLabels;
    QMap<QString, QLabel*> m_valueLabels;
    QLabel* m_head = nullptr;
    QRectF m_contentRect;
    QMap<QString, QPixmap> m_images;
    QTimer m_timer;
    QVector<double> m_values;
    bool m_detecting = false;
#if SSVEP_WITH_EEGO
    eemagine::sdk::factory* m_factory = nullptr;
    eemagine::sdk::amplifier* m_amplifier = nullptr;
    eemagine::sdk::stream* m_stream = nullptr;
#endif
};
