#pragma once
#include "ClipExporter.h"
#include "StepFrames.h"
#include "ClipQueue.h"
#include <QWidget>
#include <QKeySequence>
#include <array>
#include <functional>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <optional>
#include <vector>

class QListWidget;
class FrameCanvas;
class TimelineSlider;
class ClipMarkers;
class QLabel;
class QPushButton;
class QShortcut;
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
    StepFrames stepFrames;
    QStackedWidget *previewStack;
    FrameCanvas *frameCanvas;
    QSize stepPreviewSize() const;
    QTimer previewResizeTimer;
    std::optional<qint64> requestedFrame;
    qint64 batchTargetUs = 0;
    void leaveCachedPreview();
    const std::vector<qint64> &stepIndex(qint64 position) const;
    void loadFrameBatch(qint64 target, int direction);
    void displayStep(qint64 target, const QImage &image);
    int requestedDirection = 0;
    QTimer reverseStepTimer;
    QElapsedTimer reverseStepTime;

    enum Shortcut { PlayPause, StepBack, StepForward, MarkIn, MarkOut, Preview, Export, Open, ShortcutCount };
    using Shortcuts = std::array<QKeySequence, ShortcutCount>;
    Shortcuts shortcuts;
    std::array<QShortcut *, ShortcutCount> shortcutActions{};
    std::array<QPushButton *, ShortcutCount> shortcutButtons{};
    static Shortcuts defaultShortcuts();
    static QString shortcutError(const Shortcuts &keys);
    void applyShortcuts();
    int stepDirection(const QKeyEvent *event) const;
    void handleStepPress(QKeyEvent *event);
    void advanceReverseHold();
    void finishHold();
    void probeFrames(qint64 centerUs);

    TimelineSlider *timeline;
    ClipMarkers *markers;
    QLabel *title;
    QLabel *times;
    QLabel *status;
    QPushButton *markDoneButton;
    QPushButton *trashButton;
    QPushButton *settingsButton;
    std::function<void(const QString &)> replayHotkeyChanged;
    QStackedWidget *pages;
    QWidget *homePage;
    QWidget *editPage;
    QLabel *replayStatus = nullptr;
    QToolButton *startReplay = nullptr;
    QToolButton *stopReplay = nullptr;
    QListWidget *recent;
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
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool event(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

public:
    Editor();
    void showSettings();
    void setReplayHotkeyHandler(std::function<void(const QString &)> handler);
    void setSettingsEnabled(bool enabled);
    void setReplayActions(QAction *state, QAction *start, QAction *stop);
    void showClipQueue();
    void showAndActivate();
};
