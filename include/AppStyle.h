#pragma once
#include <QProxyStyle>
#include <QStyleFactory>

// Keep keyboard focus functional while replacing the native dotted focus rectangle.
class AppStyle final : public QProxyStyle {
public:
    AppStyle() : QProxyStyle(QStyleFactory::create("windowsvista")) {}
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget = nullptr) const override
    {
        if (element != PE_FrameFocusRect)
            QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};
