#pragma once
#include "ClipExporter.h"
#include "ClipQueue.h"
#include <QWidget>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QProcess>
#include <QElapsedTimer>
#include <optional>
#include <vector>

class TimelineSlider;
class ClipMarkers;
class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QToolButton;

class Editor : public QWidget {
    QMediaPlayer player;
    QAudioOutput audio;
    ClipExporter exporter;
    QProcess probe;
    QProcess frameProbe;
    std::vector<qint64> keysUs, videoPacketTimesUs, frameTimesUs;
    std::optional<qint64> steppedFrameUs;
    std::optional<qint64> frameWindowCenterUs;
    qint64 frameProbeCenterUs = 0, mediaOriginUs = 0;
    int pendingStep = 0;
    int heldDirection = 0;
    bool forwardPlaying = false, repeatingStep = false;
    QElapsedTimer stepSeek;
    void finishHold();
    void probeFrames(qint64 centerUs);

    TimelineSlider *timeline;
    ClipMarkers *markers;
    QLabel *title;
    QLabel *times;
    QLabel *status;
    QPushButton *markDoneButton;
    QPushButton *trashButton;
    QMenu *settingsMenu;
    QStackedWidget *pages;
    QWidget *homePage;
    QWidget *editPage;
    QLabel *replayStatus = nullptr;
    QToolButton *startReplay = nullptr;
    QToolButton *stopReplay = nullptr;
    QPushButton *homeQueue = nullptr;
    QPushButton *resumeEditing = nullptr;
    std::optional<ClipQueue::Entry> queueEntry;
    bool clipCompleted = false;
    QString source, destination;
    qint64 exportStartUs = 0, exportDurationUs = 0;
    qint64 startUs = 0, endUs = 0;
    bool previewing = false, edited = false;

    qint64 playheadUs() const;
    void refresh();
    void clearFrameStep();
    void stepFrame(int direction);
    bool seekAdjacentFrame(qint64 positionUs, int direction);

    void updateQueueActions();
    void setupHome();
    void refreshHome();
    void markDone();
    void trashClips(QList<ClipQueue::Entry> clips, bool completed, QWidget *parent);
    void open();
    void openFile(const QString &file);

    void mark(bool first);

    void preview();

    void exportClip();

    void saveClip(const QString &suggestedName, bool mkvOnly);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

public:
    Editor();
    void addSettingsAction(QAction *action);
    void setReplayActions(QAction *state, QAction *start, QAction *stop);
    void showClipQueue();
    void showAndActivate();
};
