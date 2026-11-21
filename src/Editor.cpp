#include "Editor.h"
#include "Timeline.h"
#include <QtWidgets>
#include <QVideoWidget>
#include <algorithm>
#include "TrimRange.h"

static QString timestamp(qint64 ms)
{
    return QString("%1:%2.%3").arg(ms / 60000)
        .arg(ms / 1000 % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
}

void Editor::refresh()
{
    auto cut = losslessRange(keysUs, start, end);
    markers->setBounds(int(start), int(end));
    times->setText(QString("%1     Requested  %2     Out  %3     Lossless start  %4")
        .arg(timestamp(player.position()), timestamp(start), timestamp(end),
             cut ? timestamp(qRound64(cut->startUs / 1000.0)) : QString("—")));
}

void Editor::open()
{
    if (exporter.isBusy()) return;
    auto file = QFileDialog::getOpenFileName(this, "Open replay", {},
        "Video (*.mp4 *.mkv *.mov);;All files (*)");
    if (file.isEmpty()) return;
    player.stop();
    source = file;
    if (probe.state() != QProcess::NotRunning) {
        probe.kill();
        probe.waitForFinished();
    }
    if (probe.isOpen()) probe.readAllStandardOutput();
    keysUs.clear();
    edited = previewing = false;
    start = end = 0;
    timeline->setRange(0, 0);
    title->setText(QFileInfo(file).fileName());
    status->clear();
    status->setText("Finding keyframes…");
    probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_packets",
        "-show_entries", "format=start_time:packet=pts_time,flags", "-of", "json", source});
    player.setSource(QUrl::fromLocalFile(file));
    end = player.duration();
    timeline->setRange(0, int(end));
    player.setPosition(0);
    refresh();
}

void Editor::mark(bool first)
{
    if (player.duration() <= 0) return;
    auto p = player.position();
    if ((first && p >= end) || (!first && p <= start)) {
        status->setText("The out point must follow the in point.");
        return;
    }
    if (first) start = p;
    else end = p;
    edited = true;
    previewing = false;
    status->clear();
    refresh();
}

void Editor::preview()
{
    auto cut = losslessRange(keysUs, start, end);
    if (!cut || end <= start) return;
    if (player.playbackState() == QMediaPlayer::PlayingState) {
        player.pause();
        previewing = false;
        return;
    }
    player.setPosition(qRound64(cut->startUs / 1000.0));
    previewing = true;
    player.play();
}

void Editor::exportClip()
{
    auto cut = losslessRange(keysUs, start, end);
    if (exporter.isBusy() || end <= start) return;
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
    action("Play / pause   K", Qt::Key_K, [this]{
        previewing = false;
        if (player.playbackState() == QMediaPlayer::PlayingState) player.pause();
        else player.play();
    });
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
        if (!keysUs.empty() && !edited) start = (keysUs.front() + 999) / 1000;
        status->setText(keysUs.empty() ? "No usable video keyframes found." : "Keyframes ready.");
        refresh();
    });
    player.setVideoOutput(video);
    player.setAudioOutput(&audio);
    connect(&player, &QMediaPlayer::durationChanged, this, [this](qint64 d){
        timeline->setRange(0, int(d));
        if (!edited) end = d;
        refresh();
    });
    connect(&player, &QMediaPlayer::positionChanged, this, [this](qint64 p){
        if (!timeline->isSliderDown()) {
            QSignalBlocker block(timeline);
            timeline->setValue(int(p));
        }
        if (previewing && p >= end) {
            previewing = false;
            player.pause();
            player.setPosition(end);
        }
        refresh();
    });
    connect(timeline, &QSlider::valueChanged, this, [this](int p){
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
