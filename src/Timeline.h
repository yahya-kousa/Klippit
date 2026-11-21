#pragma once
#include <QSlider>
#include <QWidget>

class TimelineSlider : public QSlider {
public:
    TimelineSlider();
    int handleCenterAt(int ms) const;
};

class ClipMarkers : public QWidget {
    TimelineSlider *slider;
    int in = 0, out = 0;

public:
    explicit ClipMarkers(TimelineSlider *timeline);
    void setBounds(int first, int last);

protected:
    void paintEvent(QPaintEvent *) override;
};
