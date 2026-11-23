#include "Editor.h"
#include "ClipQueue.h"
#include "Timeline.h"
#include <QtWidgets>
#include <QVideoWidget>
#include <algorithm>
#include <utility>
#include "TrimRange.h"

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
    if (pendingStep && status->text() == "Finding video frames…") status->clear();
    pendingStep = 0;
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
    player.stop();
    source = file;
    if (probe.state() != QProcess::NotRunning) {
        probe.kill();
        probe.waitForFinished();
    }
    if (probe.isOpen()) probe.readAllStandardOutput();
    keysUs.clear();
    edited = previewing = false;
    startUs = endUs = 0;
    timeline->setRange(0, 0);
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
    auto *message = new QLabel;
    message->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *openClip = buttons->addButton("Open Clip", QDialogButtonBox::AcceptRole);
    auto *changeState = buttons->addButton("Mark Done", QDialogButtonBox::ActionRole);
    layout->addWidget(completed);
    layout->addWidget(list);
    layout->addWidget(message);
    layout->addWidget(buttons);

    QList<ClipQueue::Entry> entries;
    QString selectedPath;
    auto updateButtons = [&] {
        const int row = list->currentRow();
        openClip->setEnabled(row >= 0 && QFileInfo(entries.at(row).path).isFile());
        changeState->setEnabled(row >= 0);
    };
    auto refreshQueue = [&] {
        const int row = list->currentRow();
        const QString selectedId = row >= 0 ? entries.at(row).id : QString();
        const QSignalBlocker blocked(list);
        list->clear();
        bool readError = false;
        entries = queue.entries(completed->isChecked(), &readError);
        int selection = 0;
        for (int i = 0; i < entries.size(); ++i) {
            const auto &entry = entries.at(i);
            QString label = QFileInfo(entry.path).fileName() + "   ·   " +
                QLocale().toString(entry.savedAt.toLocalTime(), QLocale::ShortFormat);
            if (!QFileInfo(entry.path).isFile()) label += "   ·   File missing";
            auto *item = new QListWidgetItem(label, list);
            item->setToolTip(entry.path);
            if (entry.id == selectedId) selection = i;
        }
        list->setCurrentRow(entries.isEmpty() ? -1 : selection);
        changeState->setText(completed->isChecked() ? "Move Back to Queue" : "Mark Done");
        if (readError) message->setText("Some queue entries could not be read.");
        else if (entries.isEmpty())
            message->setText(completed->isChecked() ? "No completed clips." : "No clips waiting to be trimmed.");
        else message->clear();
        updateButtons();
    };
    auto openSelected = [&] {
        if (list->currentRow() < 0) return;
        const auto &path = entries.at(list->currentRow()).path;
        if (!QFileInfo(path).isFile()) {
            refreshQueue();
            message->setText("This clip file is missing.");
            return;
        }
        selectedPath = path;
        dialog.accept();
    };

    connect(completed, &QCheckBox::toggled, &dialog, refreshQueue);
    connect(list, &QListWidget::currentRowChanged, &dialog, updateButtons);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, openSelected);
    connect(openClip, &QPushButton::clicked, &dialog, openSelected);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(changeState, &QPushButton::clicked, &dialog, [&] {
        const int row = list->currentRow();
        if (row < 0) return;
        const bool changed = queue.setCompleted(entries.at(row), !completed->isChecked());
        refreshQueue();
        if (!changed) message->setText("Could not update this clip's queue status.");
    });
    QFileSystemWatcher watcher;
    const bool watching = watcher.addPath(queue.directory());
    connect(&watcher, &QFileSystemWatcher::directoryChanged, &dialog, refreshQueue);
    refreshQueue();
    if (!watching) message->setText("Reopen Clips to Trim to see new saves.");
    if (dialog.exec() == QDialog::Accepted) openFile(selectedPath);
}

void Editor::stepFrame(int direction)
{
    if (source.isEmpty() || !player.isSeekable()) return;
    previewing = false;
    player.pause();
    if (frameTimesUs.empty()) {
        pendingStep = direction;
        if (frameProbe.state() == QProcess::NotRunning) {
            status->setText("Finding video frames…");
            frameProbe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_frames",
                "-show_entries", "frame=best_effort_timestamp_time:format=start_time",
                "-of", "compact=p=0:nk=0", source});
        }
        return;
    }

    const auto after = std::upper_bound(frameTimesUs.begin(), frameTimesUs.end(), playheadUs());
    if (direction < 0 && after - frameTimesUs.begin() < 2) return;
    if (direction > 0 && after == frameTimesUs.end()) return;
    const auto frame = direction > 0 ? after : std::prev(after, 2);
    steppedFrameUs = *frame;

    // Qt seeks in milliseconds and can render the preceding frame at a
    // boundary. Seek inside the frame while keeping its timestamp for marks.
    const auto next = std::next(frame);
    const qint64 frameSpanUs = next == frameTimesUs.end() ? 8000 : *next - *frame;
    const qint64 insideUs = *frame + std::min<qint64>(frameSpanUs / 2, 4000);
    player.setPosition(std::min(insideUs / 1000, player.duration()));
    const QSignalBlocker block(timeline);
    timeline->setValue(int(*steppedFrameUs / 1000));
    refresh();
}

void Editor::mark(bool first)
{
    if (player.duration() <= 0) return;
    const auto position = playheadUs();
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
    saveClip(input.dir().filePath(input.completeBaseName() + "-trim"), false);
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
    } else QWidget::closeEvent(event);
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

Editor::Editor()
    : timeline(new TimelineSlider),
      markers(new ClipMarkers(timeline)),
      title(new QLabel("Open a replay")),
      times(new QLabel),
      status(new QLabel)
{
    setWindowTitle("Klippit");
    resize(960, 640);
    auto *layout = new QVBoxLayout(this);
    auto *video = new QVideoWidget;
    layout->addWidget(title);
    layout->addWidget(video, 1);
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
        connect(new QShortcut(key, this), &QShortcut::activated, this, fn);
    };
    action("Open   Ctrl+O", QKeySequence::Open, [this]{ open(); });
    auto *queueButton = new QPushButton("Clips to Trim");
    queueButton->setFocusPolicy(Qt::NoFocus);
    buttons->addWidget(queueButton);
    connect(queueButton, &QPushButton::clicked, this, [this]{ showClipQueue(); });
    action("Play / pause   K", Qt::Key_K, [this]{
        previewing = false;
        if (player.playbackState() == QMediaPlayer::PlayingState) player.pause();
        else { clearFrameStep(); player.play(); refresh(); }
    });
    connect(new QShortcut(Qt::Key_Comma, this), &QShortcut::activated, this, [this]{ stepFrame(-1); });
    connect(new QShortcut(Qt::Key_Period, this), &QShortcut::activated, this, [this]{ stepFrame(1); });
    timeline->setToolTip("Use , and . to step backward or forward one frame.");
    action("Set in   I", Qt::Key_I, [this]{ mark(true); });
    action("Set out   O", Qt::Key_O, [this]{ mark(false); });
    action("Preview   Space", Qt::Key_Space, [this]{ preview(); });
    action("Export   Enter", Qt::Key_Return, [this]{ exportClip(); });
    connect(&probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error){
        if (error == QProcess::FailedToStart) status->setText("Could not start ffprobe. Check PATH.");
    });
    connect(&probe, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit){
        auto doc = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        if (code != 0 || exit != QProcess::NormalExit) {
            status->setText("Keyframe indexing failed. " + QString::fromUtf8(probe.readAllStandardError()).right(400));
            return;
        }
        auto origin = doc["format"].toObject()["start_time"].toString().toDouble();
        for (auto value : doc["packets"].toArray()) {
            auto packet = value.toObject();
            auto flags = packet["flags"].toString();
            if (!flags.contains('K') || flags.contains('D')) continue;
            bool ok = false;
            auto seconds = packet["pts_time"].toString().toDouble(&ok);
            if (ok && seconds >= origin) keysUs.push_back(qRound64((seconds - origin) * 1000000));
        }
        std::sort(keysUs.begin(), keysUs.end());
        if (!keysUs.empty() && !edited) startUs = keysUs.front();
        if (frameProbe.state() == QProcess::NotRunning)
            status->setText(keysUs.empty() ? "No usable video keyframes found." : "Keyframes ready.");
        refresh();
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
        qint64 originUs = 0;
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
            else if (name == "best_effort_timestamp_time") frameTimesUs.push_back(qRound64(seconds * 1000000));
        }
        for (auto &frame : frameTimesUs) frame -= originUs;
        std::sort(frameTimesUs.begin(), frameTimesUs.end());
        frameTimesUs.erase(frameTimesUs.begin(), std::lower_bound(frameTimesUs.begin(), frameTimesUs.end(), 0));
        frameTimesUs.erase(std::unique(frameTimesUs.begin(), frameTimesUs.end()), frameTimesUs.end());
        if (frameTimesUs.empty()) {
            if (direction) status->setText("No usable video timestamps found for frame stepping.");
        } else if (direction) {
            status->clear();
            stepFrame(direction);
        }
    });
    player.setVideoOutput(video);
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
    connect(timeline, &QSlider::valueChanged, this, [this](int p){
        clearFrameStep();
        previewing = false;
        player.setPosition(p);
    });
    connect(&player, &QMediaPlayer::errorOccurred, this,
        [this](QMediaPlayer::Error, const QString &message){ status->setText(message); });
    connect(&exporter, &ClipExporter::statusChanged, status, &QLabel::setText);
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
