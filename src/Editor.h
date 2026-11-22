#pragma once
#include "ClipExporter.h"
#include <QWidget>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QProcess>
#include <vector>

class TimelineSlider;
class ClipMarkers;
class QLabel;

class Editor : public QWidget {
    QMediaPlayer player;
    QAudioOutput audio;
    ClipExporter exporter;
    QProcess probe;
    std::vector<qint64> keysUs;

    TimelineSlider *timeline;
    ClipMarkers *markers;
    QLabel *title;
    QLabel *times;
    QLabel *status;
    QString source, destination;
    qint64 exportStartUs = 0, exportDurationUs = 0;
    qint64 start = 0, end = 0;
    bool previewing = false, edited = false;

    void refresh();

    void open();
    void openFile(const QString &file);

    void mark(bool first);

    void preview();

    void exportClip();

    void saveClip(const QString &suggestedName, bool mkvOnly);

protected:
    void closeEvent(QCloseEvent *event) override;

public:
    Editor();
    void showClipQueue();
};
