#include "Timeline.h"
#include <QPainter>
#include <QStyleOptionSlider>
#include <QStyle>

TimelineSlider::TimelineSlider() : QSlider(Qt::Horizontal) {}

int TimelineSlider::handleCenterAt(int ms) const
{
    QStyleOptionSlider option;
    initStyleOption(&option);
    option.sliderPosition = ms;
    return style()->subControlRect(QStyle::CC_Slider, &option,
        QStyle::SC_SliderHandle, this).center().x();
}

ClipMarkers::ClipMarkers(TimelineSlider *timeline) : slider(timeline)
{
    setFixedHeight(16);
    setToolTip("Clip boundaries: green marks the requested in point, orange marks the out point.");
}

void ClipMarkers::setBounds(int first, int last)
{
    if (in == first && out == last) return;
    in = first;
    out = last;
    update();
}

void ClipMarkers::paintEvent(QPaintEvent *)
{
    if (out <= in || slider->maximum() <= slider->minimum()) return;
    const int left = slider->handleCenterAt(in);
    const int right = slider->handleCenterAt(out);
    const int direction = left <= right ? 1 : -1;
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawLine(left, 8, right, 8);
    auto flag = [&](int x, int inward, QColor color, int y) {
        painter.setPen(QPen(palette().color(QPalette::WindowText), 1));
        painter.setBrush(color);
        painter.drawPolygon(QPolygon{QPoint(x, y), QPoint(x + inward * 7, y + 3),
                                     QPoint(x, y + 6)});
        painter.drawLine(x, y + 6, x, height() - 1);
    };
    flag(left, direction, QColor("#36b47d"), 0);
    flag(right, -direction, QColor("#f49a43"), 7);
}
