#pragma once
#include <QProcess>
#include <QSlider>
#include <QWidget>
#include <vector>

class TimelineSlider : public QSlider {
public:
    TimelineSlider();
    int handleCenterAt(int ms) const;
};

class ClipMarkers : public QWidget {
    Q_OBJECT
    TimelineSlider *slider;
    QProcess decoder;
    QByteArray peaks;
    int samples = 0, peak = 0;
    int in = 0, out = 0, zoom = 1, viewStart = 0;
    enum Drag { None, Seek, In, Out } drag = None;
    const std::vector<qint64> *keys = nullptr;
    int span() const;
    int timeAt(int x) const;
    void moveTo(int x);
public:
    explicit ClipMarkers(TimelineSlider *timeline);
    void load(const QString &file, const std::vector<qint64> &keyframes);
    void setBounds(int first, int last);
    void setZoom(int factor);
signals:
    void zoomChanged(int factor);
    void boundaryMoved(bool first, int ms);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
};
