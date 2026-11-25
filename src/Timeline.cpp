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

#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>

ClipMarkers::ClipMarkers(TimelineSlider *timeline) : slider(timeline)
{
    setFixedHeight(60);
    setToolTip(tr("Drag to seek or move a trim handle. Scroll to zoom."));
    connect(&decoder, &QProcess::readyReadStandardOutput, this, [this] {
        for (char sample : decoder.readAllStandardOutput()) {
            peak = std::max(peak, std::abs(int(qint8(sample))));
            if (++samples == 80) {
                peaks.append(char(std::min(127, peak)));
                peak = samples = 0;
            }
        }
        update();
    });
}

void ClipMarkers::load(const QString &file, const std::vector<qint64> &keyframes)
{
    if (decoder.state() != QProcess::NotRunning) {
        decoder.kill();
        decoder.waitForFinished();
    }
    decoder.readAllStandardOutput();
    keys = &keyframes;
    peaks.clear();
    peak = samples = viewStart = 0;
    setZoom(1);
    if (file.isEmpty()) return;
    decoder.setStandardErrorFile(QProcess::nullDevice());
    decoder.start("ffmpeg", {"-nostdin", "-v", "error", "-i", file, "-map", "0:a:0",
        "-vn", "-ac", "1", "-af", "aresample=4000:async=1:first_pts=0",
        "-f", "s8", "pipe:1"});
}

int ClipMarkers::span() const { return std::max(1, slider->maximum() / zoom); }

int ClipMarkers::timeAt(int x) const
{
    return std::clamp(viewStart + int(qint64(span()) * std::clamp(x, 0, width()) /
        std::max(1, width())), 0, slider->maximum());
}

void ClipMarkers::setZoom(int factor)
{
    zoom = std::clamp(factor, 1, 32);
    viewStart = std::clamp(slider->value() - span()/2, 0, std::max(0, slider->maximum()-span()));
    emit zoomChanged(zoom);
    update();
}

void ClipMarkers::setBounds(int first, int last)
{
    in = first;
    out = last;
    if (drag == None && (slider->value() < viewStart || slider->value() > viewStart + span()))
        setZoom(zoom);
    update();
}

void ClipMarkers::paintEvent(QPaintEvent *)
{
    if (!slider->maximum()) return;
    QPainter p(this);
    const auto accent = palette().color(QPalette::Highlight);
    auto x = [&](int ms){ return qreal(ms-viewStart)*width()/span(); };
    p.fillRect(0,24,width(),32,palette().base());
    auto tint = accent;
    tint.setAlpha(65);
    p.fillRect(QRectF(x(in),24,x(out)-x(in),32),tint);
    for (int pixel=0; pixel<width(); ++pixel) {
        int start = timeAt(pixel)/20, end = std::max(start+1,timeAt(pixel+1)/20);
        if (start >= peaks.size()) break;
        auto amplitude = *std::max_element(peaks.cbegin()+start, peaks.cbegin()+std::min<qsizetype>(end,peaks.size()));
        int h = 1 + amplitude/11;
        p.setPen(timeAt(pixel)>=in && timeAt(pixel)<=out ? palette().color(QPalette::Text) : palette().color(QPalette::Mid));
        p.drawLine(pixel,40-h,pixel,40+h);
    }
    p.setPen(palette().color(QPalette::PlaceholderText));
    if (keys) {
        auto it = std::lower_bound(keys->begin(),keys->end(),qint64(viewStart)*1000);
        for (; it!=keys->end() && *it/1000<=viewStart+span(); ++it)
            p.drawLine(QPointF(x(*it/1000),54),QPointF(x(*it/1000),59));
    }
    p.setPen(QPen(accent,3));
    for (int ms : {in,out}) {
        qreal edge=std::clamp(x(ms),1.,qreal(width()-1));
        if (ms<viewStart || ms>viewStart+span()) continue;
        p.drawLine(QPointF(edge,24),QPointF(edge,55));
        p.drawLine(QPointF(edge,24),QPointF(edge+(ms==in?6:-6),24));
    }
    const int ms=slider->value(), pos=int(x(ms));
    const QString label=QString("%1:%2.%3").arg(ms/60000).arg(ms/1000%60,2,10,QChar('0')).arg(ms%1000,3,10,QChar('0'));
    const int w=fontMetrics().horizontalAdvance(label)+12;
    QRect pill(std::clamp(pos-w/2,0,std::max(0,width()-w)),0,w,22);
    p.setPen(accent);
    p.drawLine(pos,21,pos,57);
    p.setBrush(accent);
    p.drawRoundedRect(pill,4,4);
    p.setPen(palette().color(QPalette::HighlightedText));
    p.drawText(pill,Qt::AlignCenter,label);
}

void ClipMarkers::moveTo(int x)
{
    const int ms=timeAt(x);
    if (drag==Seek) slider->setValue(ms);
    else if (drag!=None) emit boundaryMoved(drag==In,ms);
}

void ClipMarkers::mousePressEvent(QMouseEvent *e)
{
    if (e->button()!=Qt::LeftButton || !slider->maximum()) return;
    const int ms=timeAt(e->position().x());
    const int tolerance=std::max(1,int(qint64(span())*7/std::max(7,width())));
    drag=e->position().y()<23 ? Seek : std::abs(ms-in)<=tolerance ? In : std::abs(ms-out)<=tolerance ? Out : Seek;
    moveTo(e->position().x());
}

void ClipMarkers::mouseMoveEvent(QMouseEvent *e) { if (drag!=None) moveTo(e->position().x()); }
void ClipMarkers::mouseReleaseEvent(QMouseEvent *) { drag=None; }
void ClipMarkers::wheelEvent(QWheelEvent *e)
{
    if (e->angleDelta().y()) setZoom(e->angleDelta().y()>0 ? zoom*2 : zoom/2);
    e->accept();
}
