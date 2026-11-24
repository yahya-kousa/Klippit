#include "Editor.h"
#include "SaveFolders.h"
#include "Timeline.h"
#include <QtWidgets>
#include <QVideoWidget>
#include <QVideoSink>
#include <QVideoFrame>
#include <QScopeGuard>
#include <algorithm>
#include <utility>
#include "TrimRange.h"

class FrameCanvas : public QWidget {
public:
    QImage image;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        const auto size = image.size().scaled(this->size(), Qt::KeepAspectRatio);
        const QRect area(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(area, image);
    }
};

void Editor::leaveCachedPreview()
{
    requestedFrame.reset();
    stepFrames.clear();
    if (previewStack->currentWidget() == frameCanvas) {
        player.setPosition(playheadUs() / 1000);
        previewStack->setCurrentIndex(0);
        frameCanvas->image = {};
    }
}

static QString timestamp(qint64 ms)
{
    return QString("%1:%2.%3").arg(ms / 60000)
        .arg(ms / 1000 % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
}

qint64 Editor::playheadUs() const
{
    return steppedFrameUs.value_or(player.position() * 1000);
}

void Editor::clearFrameStep()
{
    reverseStepTimer.stop();
    if (forwardPlaying) player.pause();
    if (pendingStep && status->text() == "Finding nearby frames…") status->clear();
    pendingStep = 0;
    heldDirection = 0;
    repeatingStep = forwardPlaying = false;
    leaveCachedPreview();
    steppedFrameUs.reset();
}

void Editor::refresh()
{
    auto cut = losslessRange(keysUs, startUs, endUs);
    markers->setBounds(int(startUs / 1000), int(endUs / 1000));
    times->setText(QString("%1     Requested  %2     Out  %3     Lossless start  %4")
        .arg(timestamp(playheadUs() / 1000), timestamp(startUs / 1000), timestamp(endUs / 1000),
             cut ? timestamp(qRound64(cut->startUs / 1000.0)) : QString("—")));
}

void Editor::open()
{
    if (exporter.isBusy()) return;
    auto file = QFileDialog::getOpenFileName(this, "Open replay", {},
        "Video (*.mp4 *.mkv *.mov);;All files (*)");
    if (!file.isEmpty()) openFile(file);
}

void Editor::openFile(const QString &file)
{
    clearFrameStep();
    if (frameProbe.state() != QProcess::NotRunning) {
        frameProbe.kill();
        frameProbe.waitForFinished();
    }
    frameTimesUs.clear();
    frameWindowCenterUs.reset();
    mediaOriginUs = 0;
    player.stop();
    source = file;
    if (probe.state() != QProcess::NotRunning) {
        probe.kill();
        probe.waitForFinished();
    }
    if (probe.isOpen()) probe.readAllStandardOutput();
    keysUs.clear();
    videoPacketTimesUs.clear();
    edited = previewing = false;
    startUs = endUs = 0;
    timeline->setRange(0, 0);
    updateQueueActions();
    resumeEditing->setEnabled(!file.isEmpty());
    if (file.isEmpty()) {
        player.setSource({});
        title->setText("Open a replay");
        refresh();
        return;
    }
    pages->setCurrentWidget(editPage);
    title->setText(QFileInfo(file).fileName());
    status->clear();
    status->setText("Finding keyframes…");
    probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_packets",
        "-show_entries", "format=start_time:packet=pts_time,flags", "-of", "json", source});
    player.setSource(QUrl::fromLocalFile(file));
    endUs = player.duration() * 1000;
    timeline->setRange(0, int(player.duration()));
    player.setPosition(0);
    refresh();
}

void Editor::updateQueueActions()
{
    queueEntry.reset();
    if (!source.isEmpty()) {
        const ClipQueue queue;
        for (bool completed : {false, true}) {
            for (const auto &entry : queue.entries(completed)) {
                if (QFileInfo(entry.path) == QFileInfo(source)) {
                    queueEntry = entry;
                    clipCompleted = completed;
                    break;
                }
            }
            if (queueEntry) break;
        }
    }
    markDoneButton->setVisible(queueEntry.has_value() && !clipCompleted);
    trashButton->setVisible(queueEntry.has_value());
}

void Editor::markDone()
{
    if (!queueEntry || exporter.isBusy()) return;
    const bool changed = ClipQueue().setCompleted(*queueEntry, true);
    updateQueueActions();
    status->setText(changed ? "Marked done. You can keep the original or move it to the trash."
                            : "Could not mark this clip done. Check Clips to Trim.");
}

void Editor::trashClips(QList<ClipQueue::Entry> clips, bool completed, QWidget *parent)
{
    if (clips.isEmpty() || exporter.isBusy()) return;
    QStringList paths;
    for (const auto &entry : clips) paths.append(QDir::toNativeSeparators(entry.path));
    const QString prompt = clips.size() == 1
        ? tr("Move this original replay to the trash?\n%1").arg(paths.first())
        : tr("Move %1 selected original replays to the trash?").arg(clips.size());
    QMessageBox confirm(QMessageBox::Question, "Trash original replays?", prompt,
                        QMessageBox::Yes | QMessageBox::Cancel, parent);
    confirm.setTextFormat(Qt::PlainText);
    confirm.setInformativeText("Exported trims will be kept.");
    if (clips.size() > 1) confirm.setDetailedText(paths.join('\n'));
    confirm.setDefaultButton(QMessageBox::Cancel);
    if (confirm.exec() != QMessageBox::Yes) return;

    const ClipQueue queue;
    QStringList errors;
    for (const auto &entry : clips) {
        const bool isOpen = !source.isEmpty() && QFileInfo(entry.path) == QFileInfo(source);
        if (isOpen) openFile({}); // Release player and probe handles before moving the file.
        QFile original(entry.path);
        if (!original.moveToTrash()) {
            if (isOpen) openFile(entry.path);
            errors.append(entry.path + " — " + original.errorString());
        } else if (!queue.removeEntry(entry, completed)) {
            errors.append(entry.path + " — moved to trash, but the queue entry could not be removed.");
        } else if (isOpen) {
            status->setText("Original replay moved to the trash.");
        }
    }
    if (!errors.isEmpty()) QMessageBox::warning(parent, "Some clips need attention", errors.join('\n'));
}

void Editor::showClipQueue()
{
    if (exporter.isBusy()) return;

    const ClipQueue queue;
    if (!queue.ensureDirectory()) {
        status->setText("Could not open the Clips to Trim folder.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Clips to Trim");
    dialog.resize(600, 340);
    auto *layout = new QVBoxLayout(&dialog);
    auto *completed = new QCheckBox("Show completed");
    auto *list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto *message = new QLabel;
    message->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *openClip = buttons->addButton("Open Clip", QDialogButtonBox::AcceptRole);
    auto *changeState = buttons->addButton("Mark Done", QDialogButtonBox::ActionRole);
    auto *trash = buttons->addButton("Trash original…", QDialogButtonBox::ActionRole);
    auto *remove = buttons->addButton("Remove from queue", QDialogButtonBox::ActionRole);
    remove->setToolTip("Forget this queue entry and keep any files on disk.");
    layout->addWidget(completed);
    layout->addWidget(list);
    layout->addWidget(message);
    layout->addWidget(buttons);

    QList<ClipQueue::Entry> entries;
    QString selectedPath;
    auto selectedEntries = [&] {
        QList<ClipQueue::Entry> selected;
        for (auto *item : list->selectedItems()) selected.append(entries.at(list->row(item)));
        return selected;
    };
    auto updateButtons = [&] {
        const auto selected = selectedEntries();
        const auto available = std::count_if(selected.begin(), selected.end(),
            [](const auto &entry) { return QFileInfo(entry.path).isFile(); });
        openClip->setEnabled(selected.size() == 1 && available == 1);
        trash->setEnabled(!selected.isEmpty() && available == selected.size());
        trash->setText(selected.size() > 1 ? tr("Trash %1 originals…").arg(selected.size()) : tr("Trash original…"));
        remove->setEnabled(!selected.isEmpty() && (completed->isChecked() || available == 0));
        changeState->setEnabled(selected.size() == 1);
    };
    bool selectFirst = true;
    auto refreshQueue = [&] {
        QSet<QString> selectedIds;
        for (const auto &entry : selectedEntries()) selectedIds.insert(entry.id);
        const QSignalBlocker blocked(list);
        list->clear();
        bool readError = false;
        entries = queue.entries(completed->isChecked(), &readError);
        for (int i = 0; i < entries.size(); ++i) {
            const auto &entry = entries.at(i);
            QString label = QFileInfo(entry.path).fileName() + "   ·   " +
                QLocale().toString(entry.savedAt.toLocalTime(), QLocale::ShortFormat);
            if (!QFileInfo(entry.path).isFile()) label += "   ·   File missing";
            auto *item = new QListWidgetItem(label, list);
            item->setToolTip(entry.path);
            if (selectedIds.contains(entry.id)) item->setSelected(true);
        }
        if (selectFirst && !entries.isEmpty()) list->setCurrentRow(0);
        selectFirst = false;
        changeState->setText(completed->isChecked() ? "Move Back to Queue" : "Mark Done");
        if (readError) message->setText("Some queue entries could not be read.");
        else if (entries.isEmpty())
            message->setText(completed->isChecked() ? "No completed clips." : "No clips waiting to be trimmed.");
        else message->clear();
        updateButtons();
    };
    auto openSelected = [&] {
        const auto selected = selectedEntries();
        if (selected.size() != 1) return;
        const auto path = selected.first().path;
        if (!QFileInfo(path).isFile()) {
            refreshQueue();
            message->setText("This clip file is missing.");
            return;
        }
        selectedPath = path;
        dialog.accept();
    };

    connect(completed, &QCheckBox::toggled, &dialog, [&] { selectFirst = true; refreshQueue(); });
    connect(list, &QListWidget::itemSelectionChanged, &dialog, updateButtons);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, openSelected);
    connect(openClip, &QPushButton::clicked, &dialog, openSelected);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(changeState, &QPushButton::clicked, &dialog, [&] {
        const auto selected = selectedEntries();
        if (selected.size() != 1) return;
        const bool changed = queue.setCompleted(selected.first(), !completed->isChecked());
        refreshQueue();
        if (!changed) message->setText("Could not update this clip's queue status.");
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        const bool done = completed->isChecked();
        QStringList failed;
        for (const auto &entry : selectedEntries()) {
            if ((!done && QFileInfo(entry.path).isFile()) || !queue.removeEntry(entry, done))
                failed.append(entry.path);
        }
        refreshQueue();
        if (!failed.isEmpty())
            QMessageBox::warning(&dialog, "Queue entries kept", failed.join('\n'));
    });
    connect(trash, &QPushButton::clicked, &dialog, [&] {
        trashClips(selectedEntries(), completed->isChecked(), &dialog);
        refreshQueue();
    });
    QFileSystemWatcher watcher;
    const bool watching = watcher.addPath(queue.directory());
    connect(&watcher, &QFileSystemWatcher::directoryChanged, &dialog, refreshQueue);
    refreshQueue();
    if (!watching) message->setText("Reopen Clips to Trim to see new saves.");
    if (dialog.exec() == QDialog::Accepted) openFile(selectedPath);
    else updateQueueActions();
    refreshHome();
}

void Editor::finishHold()
{
    reverseStepTimer.stop();
    if (repeatingStep) {
        requestedFrame.reset();
        pendingStep = 0;
        if (status->text() == "Finding nearby frames…") status->clear();
    }
    if (forwardPlaying) {
        player.pause();
        const auto frame = player.videoSink()->videoFrame();
        if (frame.startTime() >= 0) steppedFrameUs = frame.startTime();
        refresh();
    }
    heldDirection = 0;
    repeatingStep = forwardPlaying = false;
}

bool Editor::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate && heldDirection) finishHold();
    return QWidget::event(event);
}

void Editor::keyPressEvent(QKeyEvent *event)
{
    const int direction = event->key() == Qt::Key_Comma ? -1 :
                          event->key() == Qt::Key_Period ? 1 : 0;
    if (!direction || event->modifiers() != Qt::NoModifier || pages->currentWidget() != editPage ||
        source.isEmpty() || !player.isSeekable()) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (!event->isAutoRepeat()) {
        if (heldDirection) finishHold();
        heldDirection = direction;
        stepFrame(direction);
    } else if (heldDirection == direction) {
        const bool firstRepeat = !repeatingStep;
        repeatingStep = true;
        if (direction > 0 && !forwardPlaying) {
            pendingStep = 0;
            if (status->text() == "Finding nearby frames…") status->clear();
            leaveCachedPreview();
            steppedFrameUs.reset();
            forwardPlaying = true;
            player.play();
        } else if (direction < 0 && firstRepeat) {
            advanceReverseHold();
        }
    }
    event->accept();
}

void Editor::advanceReverseHold()
{
    if (pendingStep || !repeatingStep || heldDirection != -1) return;
    if (requestedFrame) return;
    if (!frameTimesUs.empty() && playheadUs() <= frameTimesUs.front() &&
        !videoPacketTimesUs.empty() && frameTimesUs.front() == videoPacketTimesUs.front())
        return;
    stepFrame(-1);
}

void Editor::keyReleaseEvent(QKeyEvent *event)
{
    if (heldDirection && event->key() == (heldDirection < 0 ? Qt::Key_Comma : Qt::Key_Period)) {
        if (!event->isAutoRepeat()) finishHold();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void Editor::stepFrame(int direction)
{
    if (source.isEmpty() || !player.isSeekable()) return;
    previewing = false;
    player.pause();
    const auto visible = player.videoSink()->videoFrame();
    const qint64 presentedUs = steppedFrameUs.value_or(visible.startTime() >= 0
        ? visible.startTime() : player.position() * 1000);
    const qint64 positionUs = requestedFrame.value_or(presentedUs);
    // Packet PTS supply a fast hint after a jump. Only a decoded image with
    // the requested timestamp can be shown.
    if (seekAdjacentFrame(positionUs, direction)) return;
    if (frameWindowCenterUs == positionUs) return;
    pendingStep = direction;
    status->setText("Finding nearby frames…");
    if (probe.state() != QProcess::NotRunning || frameProbe.state() != QProcess::NotRunning) return;

    probeFrames(positionUs);
}

void Editor::probeFrames(qint64 positionUs)
{
    if (frameProbe.state() != QProcess::NotRunning || probe.state() != QProcess::NotRunning) return;
    frameProbeCenterUs = positionUs;
    auto fromUs = positionUs - 1000000;
    auto untilUs = positionUs + 1000000;
    // Packet timestamps extend the window across sparse or uneven video.
    // The actual step still uses timestamps from decoded frames.
    const auto packet = std::upper_bound(videoPacketTimesUs.begin(), videoPacketTimesUs.end(), positionUs);
    if (packet - videoPacketTimesUs.begin() >= 2)
        fromUs = std::min(fromUs, *std::prev(packet, 2) - 20000);
    if (packet != videoPacketTimesUs.end())
        untilUs = std::max(untilUs, *packet + 150000);
    const auto seconds = [this](qint64 relativeUs) {
        return QString::number((relativeUs + mediaOriginUs) / 1000000.0, 'f', 6);
    };
    // Omit the start seek at the beginning so ffprobe reads from the first packet.
    const auto interval = (fromUs > 0 ? seconds(fromUs) : QString()) + '%' + seconds(untilUs);
    frameProbe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-read_intervals", interval,
        "-show_frames", "-show_entries", "frame=best_effort_timestamp_time:format=start_time",
        "-of", "compact=p=0:nk=0", source});
}

bool Editor::seekAdjacentFrame(qint64 positionUs, int direction)
{
    const auto &nearby = stepIndex(positionUs);
    const bool atEdge = !nearby.empty() && (direction < 0 ? positionUs <= nearby.front()
                                                         : positionUs >= nearby.back());
    const auto &index = atEdge && !videoPacketTimesUs.empty() ? videoPacketTimesUs : nearby;
    const auto after = std::upper_bound(index.begin(), index.end(), positionUs);
    if (direction < 0 && after - index.begin() < 2) return false;
    if (direction > 0 && after == index.end()) return false;
    const auto frame = direction > 0 ? after : std::prev(after, 2);
    requestedDirection = direction;
    auto image = stepFrames.image(*frame);
    if (image.isNull()) {
        requestedFrame = *frame;
        if (!stepFrames.busy()) loadFrameBatch(*frame, direction);
    } else {
        requestedFrame.reset();
        displayStep(*frame, image);
    }
    return true;
}

const std::vector<qint64> &Editor::stepIndex(qint64 position) const
{
    if (!frameTimesUs.empty() && frameTimesUs.front() <= position && position <= frameTimesUs.back())
        return frameTimesUs;
    return videoPacketTimesUs;
}

void Editor::loadFrameBatch(qint64 target, int direction)
{
    const auto &index = stepIndex(target);
    auto it = std::lower_bound(index.begin(), index.end(), target);
    if (it == index.end() || *it != target) {
        batchTargetUs = target;
        stepFrames.load(source, target, target);
        return;
    }
    const auto behind = direction < 0 ? StepFrames::batchSize - 2 : 2;
    const auto start = it - std::min<std::ptrdiff_t>(it - index.begin(), behind);
    batchTargetUs = target;
    stepFrames.load(source, *start, target);
}

void Editor::displayStep(qint64 target, const QImage &image)
{
    steppedFrameUs = target;
    frameCanvas->image = image;
    previewStack->setCurrentWidget(frameCanvas);
    frameCanvas->update();
    const QSignalBlocker block(timeline);
    timeline->setValue(int(target / 1000));
    refresh();
    if (heldDirection == -1 && repeatingStep) reverseStepTimer.start(16);

    const int direction = requestedDirection;
    const auto &index = stepIndex(target);
    const auto it = std::lower_bound(index.begin(), index.end(), target);
    if (!stepFrames.busy()) {
        for (int distance = 1; distance <= StepFrames::batchSize / 2; ++distance) {
            const auto ahead = it - index.begin() + direction * distance;
            if (ahead < 0 || ahead >= std::ssize(index)) break;
            if (!stepFrames.image(index[ahead]).isNull()) continue;
            loadFrameBatch(index[ahead], direction);
            break;
        }
    }
}

void Editor::mark(bool first)
{
    if (player.duration() <= 0) return;
    auto position = playheadUs();
    if (forwardPlaying) {
        const auto frame = player.videoSink()->videoFrame();
        if (frame.startTime() >= 0) position = frame.startTime();
    }
    if ((first && position >= endUs) || (!first && position <= startUs)) {
        status->setText("The out point must follow the in point.");
        return;
    }
    if (first) startUs = position;
    else endUs = position;
    edited = true;
    previewing = false;
    status->clear();
    refresh();
}

void Editor::preview()
{
    auto cut = losslessRange(keysUs, startUs, endUs);
    if (!cut || endUs <= startUs) return;
    if (player.playbackState() == QMediaPlayer::PlayingState) {
        player.pause();
        previewing = false;
        return;
    }
    clearFrameStep();
    player.setPosition(qRound64(cut->startUs / 1000.0));
    previewing = true;
    player.play();
}

void Editor::exportClip()
{
    auto cut = losslessRange(keysUs, startUs, endUs);
    if (exporter.isBusy() || endUs <= startUs) return;
    if (!cut) {
        status->setText("Export needs an indexed keyframe at or before the in point.");
        return;
    }
    exportStartUs = cut->startUs;
    exportDurationUs = cut->durationUs;
    QFileInfo input(source);
    const auto folder = saveFolders().trims;
    const bool usePreferredFolder = !folder.isEmpty() && QFileInfo(folder).isDir();
    if (!folder.isEmpty() && !usePreferredFolder)
        status->setText("The preferred trim folder is unavailable. Choose a destination for this export.");
    const QDir exportDir = usePreferredFolder ? QDir(folder) : input.dir();
    saveClip(exportDir.filePath(input.completeBaseName() + "-trim"), false);
}

void Editor::saveClip(const QString &suggestedName, bool mkvOnly)
{
    const QString mp4Filter = "MP4 (*.mp4)";
    const QString mkvFilter = "Matroska (*.mkv)";
    QString selected = mkvOnly ? mkvFilter : mp4Filter;
    const QString filters = mkvOnly ? mkvFilter : mp4Filter + ";;" + mkvFilter;
    destination = QFileDialog::getSaveFileName(this, "Export clip", suggestedName, filters, &selected);
    if (destination.isEmpty()) return;
    const bool mp4 = selected == mp4Filter;
    const QString suffix = mp4 ? ".mp4" : ".mkv";
    // The format dropdown owns the extension, including when the user switches formats.
    if (destination.endsWith(".mp4", Qt::CaseInsensitive) ||
        destination.endsWith(".mkv", Qt::CaseInsensitive))
        destination.chop(4);
    destination += suffix;
    exporter.start(source, destination, mp4 ? ClipExporter::Format::Mp4 : ClipExporter::Format::Mkv,
        exportStartUs, exportDurationUs);
}

void Editor::closeEvent(QCloseEvent *event)
{
    if (exporter.isBusy()) {
        status->setText("Export in progress. Close the window after it finishes.");
        event->ignore();
    } else {
        clearFrameStep();
        QWidget::closeEvent(event);
    }
}

void Editor::showAndActivate()
{
    if (isMinimized()) showNormal();
    else show();
    auto *window = QApplication::activeModalWidget();
    if (!window) window = this;
    window->raise();
    window->activateWindow();
}

void Editor::addSettingsAction(QAction *action)
{
    settingsMenu->addAction(action);
}

void Editor::setReplayActions(QAction *state, QAction *start, QAction *stop)
{
    startReplay->setDefaultAction(start);
    stopReplay->setDefaultAction(stop);
    startReplay->show();
    stopReplay->show();
    const auto updateStatus = [this, state] { replayStatus->setText(state->text()); };
    connect(state, &QAction::changed, this, updateStatus);
    updateStatus();
}

void Editor::setupHome()
{
    auto *layout = new QVBoxLayout(homePage);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    auto *heading = new QLabel("Klippit");
    auto headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 10);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    replayStatus = new QLabel("Replay capture is unavailable on this device.");
    replayStatus->setWordWrap(true);
    layout->addWidget(replayStatus);
    auto *replayControls = new QHBoxLayout;
    startReplay = new QToolButton;
    stopReplay = new QToolButton;
    for (auto *button : {startReplay, stopReplay}) {
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->hide();
        replayControls->addWidget(button);
    }
    replayControls->addStretch();
    layout->addLayout(replayControls);

    homeQueue = new QPushButton;
    connect(homeQueue, &QPushButton::clicked, this, &Editor::showClipQueue);
    auto *openFileButton = new QPushButton("Open file…");
    connect(openFileButton, &QPushButton::clicked, this, &Editor::open);
    resumeEditing = new QPushButton("Resume editing");
    connect(resumeEditing, &QPushButton::clicked, this, [this] { pages->setCurrentWidget(editPage); });
    auto *clipControls = new QHBoxLayout;
    for (auto *button : {homeQueue, openFileButton, resumeEditing}) clipControls->addWidget(button);
    clipControls->addStretch();
    layout->addLayout(clipControls);
    layout->addStretch();

    auto *watcher = new QFileSystemWatcher(this);
    const ClipQueue queue;
    if (queue.ensureDirectory()) watcher->addPath(queue.directory());
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &Editor::refreshHome);
    refreshHome();
}

void Editor::refreshHome()
{
    const auto pending = ClipQueue().entries(false).size();
    homeQueue->setText(QString("Clips to Trim (%1)").arg(pending));
    resumeEditing->setEnabled(!source.isEmpty());
}

Editor::Editor()
    : timeline(new TimelineSlider),
      markers(new ClipMarkers(timeline)),
      title(new QLabel("Open a replay")),
      times(new QLabel),
      status(new QLabel),
      markDoneButton(new QPushButton("Mark done")),
      trashButton(new QPushButton("Trash original…")),
      settingsMenu(new QMenu(this)),
      pages(new QStackedWidget(this)),
      homePage(new QWidget),
      editPage(new QWidget)
{
    setWindowTitle("Klippit");
    resize(960, 640);
    auto *windowLayout = new QVBoxLayout(this);
    auto *navigation = new QHBoxLayout;
    auto *homeButton = new QPushButton("Back to Home");
    navigation->addWidget(homeButton);
    navigation->addStretch();
    auto *settingsButton = new QPushButton("Settings");
    settingsButton->setMenu(settingsMenu);
    navigation->addWidget(settingsButton);
    settingsMenu->addAction("Save folders…", this, [this] { editSaveFolders(*this); });
    windowLayout->addLayout(navigation);
    windowLayout->addWidget(pages, 1);
    setupHome();
    pages->addWidget(homePage);
    pages->addWidget(editPage);
    homeButton->hide();
    connect(pages, &QStackedWidget::currentChanged, this, [this, homeButton] {
        homeButton->setVisible(pages->currentWidget() == editPage);
    });
    connect(homeButton, &QPushButton::clicked, this, [this] {
        if (exporter.isBusy()) {
            status->setText("Export in progress. Return to Home after it finishes.");
            return;
        }
        previewing = false;
        player.pause();
        clearFrameStep();
        refreshHome();
        pages->setCurrentWidget(homePage);
    });
    connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, this, &Editor::open);

    auto *layout = new QVBoxLayout(editPage);
    auto *video = new QVideoWidget;
    auto *heading = new QHBoxLayout;
    heading->addWidget(title, 1);
    heading->addWidget(markDoneButton);
    heading->addWidget(trashButton);
    layout->addLayout(heading);
    updateQueueActions();
    connect(markDoneButton, &QPushButton::clicked, this, &Editor::markDone);
    connect(trashButton, &QPushButton::clicked, this, [this] {
        if (queueEntry) trashClips({*queueEntry}, clipCompleted, this);
    });
    previewStack = new QStackedWidget;
    frameCanvas = new FrameCanvas;
    previewStack->addWidget(video);
    previewStack->addWidget(frameCanvas);
    layout->addWidget(previewStack, 1);
    auto *track = new QVBoxLayout;
    track->setSpacing(0);
    track->addWidget(markers);
    track->addWidget(timeline);
    layout->addLayout(track);
    layout->addWidget(times);
    auto *buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    layout->addWidget(status);
    status->setWordWrap(true);
    auto action = [this, buttons](QString label, QKeySequence key, auto fn) {
        auto *button = new QPushButton(label);
        button->setFocusPolicy(Qt::NoFocus);
        buttons->addWidget(button);
        connect(button, &QPushButton::clicked, this, fn);
        connect(new QShortcut(key, editPage), &QShortcut::activated, this, fn);
    };
    auto *openButton = new QPushButton("Open   Ctrl+O");
    openButton->setFocusPolicy(Qt::NoFocus);
    buttons->addWidget(openButton);
    connect(openButton, &QPushButton::clicked, this, &Editor::open);
    auto *queueButton = new QPushButton("Clips to Trim");
    queueButton->setFocusPolicy(Qt::NoFocus);
    buttons->addWidget(queueButton);
    connect(queueButton, &QPushButton::clicked, this, [this]{ showClipQueue(); });
    action("Play / pause   K", Qt::Key_K, [this]{
        previewing = false;
        if (player.playbackState() == QMediaPlayer::PlayingState) player.pause();
        else { clearFrameStep(); player.play(); refresh(); }
    });
    timeline->setToolTip("Tap , or . to step one frame. Hold , to step backward or . to play until release.");
    action("Set in   I", Qt::Key_I, [this]{ mark(true); });
    action("Set out   O", Qt::Key_O, [this]{ mark(false); });
    action("Preview   Space", Qt::Key_Space, [this]{ preview(); });
    action("Export   Enter", Qt::Key_Return, [this]{ exportClip(); });
    connect(&probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error){
        if (error == QProcess::FailedToStart) status->setText("Could not start ffprobe. Check PATH.");
    });
    connect(&probe, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit){
        const auto resumeStep = qScopeGuard([this] {
            if (pendingStep && frameProbe.state() == QProcess::NotRunning)
                stepFrame(std::exchange(pendingStep, 0));
        });
        auto doc = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        if (code != 0 || exit != QProcess::NormalExit) {
            status->setText("Keyframe indexing failed. " + QString::fromUtf8(probe.readAllStandardError()).right(400));
            return;
        }
        auto origin = doc["format"].toObject()["start_time"].toString().toDouble();
        mediaOriginUs = qRound64(origin * 1000000);
        for (auto value : doc["packets"].toArray()) {
            auto packet = value.toObject();
            auto flags = packet["flags"].toString();
            if (flags.contains('D')) continue;
            bool ok = false;
            auto seconds = packet["pts_time"].toString().toDouble(&ok);
            if (!ok || seconds < origin) continue;
            const auto timeUs = qRound64(seconds * 1000000) - mediaOriginUs;
            videoPacketTimesUs.push_back(timeUs);
            if (flags.contains('K')) keysUs.push_back(timeUs);
        }
        std::sort(videoPacketTimesUs.begin(), videoPacketTimesUs.end());
        videoPacketTimesUs.erase(std::unique(videoPacketTimesUs.begin(), videoPacketTimesUs.end()),
                                 videoPacketTimesUs.end());
        std::sort(keysUs.begin(), keysUs.end());
        if (!keysUs.empty() && !edited) startUs = keysUs.front();
        if (frameProbe.state() == QProcess::NotRunning)
            status->setText(keysUs.empty() ? "No usable video keyframes found." : "Keyframes ready.");
        refresh();
        if (!pendingStep) probeFrames(playheadUs());
    });
    connect(&frameProbe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        pendingStep = 0;
        status->setText("Could not start ffprobe to index video frames.");
    });
    connect(&frameProbe, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        const int direction = std::exchange(pendingStep, 0);
        if (code != 0 || exit != QProcess::NormalExit) {
            if (direction) status->setText("Could not index video frames.");
            return;
        }
        qint64 originUs = mediaOriginUs;
        std::vector<qint64> frames;
        while (frameProbe.canReadLine()) {
            const QByteArray line = frameProbe.readLine();
            const auto delimiter = line.indexOf('|');
            const auto field = QByteArrayView(line).first(delimiter < 0 ? line.size() : delimiter);
            const auto separator = field.indexOf('=');
            if (separator < 0) continue;
            const auto name = field.first(separator);
            bool ok = false;
            const double seconds = field.sliced(separator + 1).toDouble(&ok);
            if (!ok) continue;
            if (name == "start_time") originUs = qRound64(seconds * 1000000);
            else if (name == "best_effort_timestamp_time") frames.push_back(qRound64(seconds * 1000000));
        }
        for (auto &frame : frames) frame -= originUs;
        const auto extent = std::minmax_element(frames.begin(), frames.end());
        if (frames.empty() || frameTimesUs.empty() || *extent.first > frameTimesUs.back() ||
            *extent.second < frameTimesUs.front()) frameTimesUs.clear();
        frameTimesUs.insert(frameTimesUs.end(), frames.begin(), frames.end());
        std::sort(frameTimesUs.begin(), frameTimesUs.end());
        frameTimesUs.erase(frameTimesUs.begin(), std::lower_bound(frameTimesUs.begin(), frameTimesUs.end(), 0));
        frameTimesUs.erase(std::unique(frameTimesUs.begin(), frameTimesUs.end()), frameTimesUs.end());
        // Bound retained history when stepping through a long recording.
        frameTimesUs.erase(frameTimesUs.begin(), std::lower_bound(frameTimesUs.begin(),
            frameTimesUs.end(), std::min(frameProbeCenterUs, playheadUs()) - 5000000));
        frameTimesUs.erase(std::upper_bound(frameTimesUs.begin(), frameTimesUs.end(),
            std::max(frameProbeCenterUs, playheadUs()) + 5000000), frameTimesUs.end());
        if (frameTimesUs.empty()) frameWindowCenterUs.reset();
        else frameWindowCenterUs = frameProbeCenterUs;
        if (!direction) return;
        status->clear();
        if (qAbs(playheadUs() - frameProbeCenterUs) > 1000000 &&
            (frameTimesUs.empty() || playheadUs() < frameTimesUs.front() || playheadUs() > frameTimesUs.back()))
            stepFrame(direction);
        else if (frameTimesUs.empty()) status->setText("No video frames found near the playhead.");
        else seekAdjacentFrame(playheadUs(), direction);
    });
    reverseStepTimer.setTimerType(Qt::PreciseTimer);
    reverseStepTimer.setSingleShot(true);
    connect(&reverseStepTimer, &QTimer::timeout, this, &Editor::advanceReverseHold);
    player.setVideoOutput(video);
    connect(&stepFrames, &StepFrames::loaded, this, [this] {
        if (!requestedFrame) return;
        const auto target = *requestedFrame;
        const auto image = stepFrames.image(target);
        if (!image.isNull()) {
            requestedFrame.reset();
            displayStep(target, image);
        } else if (batchTargetUs != target) {
            loadFrameBatch(target, requestedDirection);
        } else {
            requestedFrame.reset();
            const auto position = playheadUs();
            if (frameProbeCenterUs != position || frameWindowCenterUs != position) {
                pendingStep = requestedDirection;
                probeFrames(position);
            } else {
                status->setText("Could not decode the selected preview frame.");
            }
        }
    });
    player.setAudioOutput(&audio);
    connect(&player, &QMediaPlayer::durationChanged, this, [this](qint64 d){
        timeline->setRange(0, int(d));
        if (!edited) endUs = d * 1000;
        refresh();
    });
    connect(&player, &QMediaPlayer::positionChanged, this, [this](qint64 p){
        if (!timeline->isSliderDown()) {
            QSignalBlocker block(timeline);
            timeline->setValue(int(steppedFrameUs ? *steppedFrameUs / 1000 : p));
        }
        if (previewing && p * 1000 >= endUs) {
            previewing = false;
            player.pause();
            player.setPosition((endUs + 999) / 1000);
        }
        refresh();
    });
    connect(timeline, &QSlider::sliderReleased, this, [this] { probeFrames(playheadUs()); });
    connect(timeline, &QSlider::valueChanged, this, [this](int p){
        clearFrameStep();
        previewing = false;
        player.setPosition(p);
    });
    connect(&player, &QMediaPlayer::errorOccurred, this,
        [this](QMediaPlayer::Error, const QString &message){ status->setText(message); });
    connect(&exporter, &ClipExporter::statusChanged, this, [this](const QString &message) {
        status->setText(message);
        markDoneButton->setEnabled(!exporter.isBusy());
        trashButton->setEnabled(!exporter.isBusy());
    });
    connect(&exporter, &ClipExporter::mp4Unsupported, this, [this]{
        QMessageBox box(QMessageBox::Information, "MP4 cannot carry this track",
            "A selected track cannot be copied into MP4. Save the same range as MKV to keep all tracks.",
            QMessageBox::Cancel, this);
        auto *retry = box.addButton("Save as MKV", QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() == retry) saveClip(destination.chopped(4), true);
    });
    refresh();
}
