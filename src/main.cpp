#include <QtWidgets>
#include <QShortcut>
#include <QKeySequence>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <memory>
#include <algorithm>
#include <iterator>
#include <optional>
#include <vector>

static QString timestamp(qint64 ms)
{
    return QString("%1:%2.%3").arg(ms / 60000)
        .arg(ms / 1000 % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
}

class TimelineSlider : public QSlider {
public:
    TimelineSlider() : QSlider(Qt::Horizontal) {}

    int handleCenterAt(int ms) const
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        option.sliderPosition = ms;
        return style()->subControlRect(QStyle::CC_Slider, &option,
            QStyle::SC_SliderHandle, this).center().x();
    }
};

class ClipMarkers : public QWidget {
    TimelineSlider *slider;
    int in = 0, out = 0;

public:
    explicit ClipMarkers(TimelineSlider *timeline) : slider(timeline)
    {
        setFixedHeight(16);
        setToolTip("Clip boundaries: green marks the requested in point, orange marks the out point.");
    }

    void setBounds(int first, int last)
    {
        if (in == first && out == last) return;
        in = first;
        out = last;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
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
};

class Editor : public QWidget {
    QMediaPlayer player;
    QAudioOutput audio;
    QProcess writer;
    QProcess probe;
    std::vector<qint64> keysUs;

    std::optional<qint64> cutStartUs() const
    {
        auto after = std::upper_bound(keysUs.begin(), keysUs.end(), start * 1000);
        if (after == keysUs.begin()) return std::nullopt;
        return *std::prev(after);
    }
    TimelineSlider *timeline = new TimelineSlider;
    ClipMarkers *markers = new ClipMarkers(timeline);
    QLabel *title = new QLabel("Open a replay");
    QLabel *times = new QLabel;
    QLabel *status = new QLabel;
    QString source, destination;
    qint64 exportStartUs = 0, exportDurationUs = 0;
    std::unique_ptr<QTemporaryDir> staging;
    qint64 start = 0, end = 0;
    bool previewing = false, edited = false;

    void refresh()
    {
        auto cut = cutStartUs();
        markers->setBounds(int(start), int(end));
        times->setText(QString("%1     Requested  %2     Out  %3     Lossless start  %4")
            .arg(timestamp(player.position()), timestamp(start), timestamp(end),
                 cut ? timestamp(qRound64(*cut / 1000.0)) : QString("—")));
    }

    void open()
    {
        if (staging) return;
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

    void mark(bool first)
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

    void preview()
    {
        auto cut = cutStartUs();
        if (!cut || end <= start) return;
        if (player.playbackState() == QMediaPlayer::PlayingState) {
            player.pause();
            previewing = false;
            return;
        }
        player.setPosition(qRound64(*cut / 1000.0));
        previewing = true;
        player.play();
    }

    void exportClip()
    {
        auto cut = cutStartUs();
        if (staging || end <= start) return;
        if (!cut) {
            status->setText("Export needs an indexed keyframe at or before the in point.");
            return;
        }
        exportStartUs = *cut;
        exportDurationUs = end * 1000 - *cut;
        QFileInfo input(source);
        saveClip(input.dir().filePath(input.completeBaseName() + "-trim"), false);
    }

    void saveClip(const QString &suggestedName, bool mkvOnly)
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
        if (QFileInfo::exists(destination)) {
            status->setText("Choose a new filename for the export.");
            return;
        }
        staging = std::make_unique<QTemporaryDir>(
            QFileInfo(destination).dir().filePath(".replay-export-XXXXXX"));
        if (!staging->isValid()) {
            staging.reset();
            status->setText("Cannot write to this folder.");
            return;
        }
        status->setText("Exporting…");
        writer.start("ffmpeg", {"-nostdin", "-v", "error", "-ss", QString::number(exportStartUs / 1000000.0, 'f', 6),
            "-i", source, "-t", QString::number(exportDurationUs / 1000000.0, 'f', 6),
            "-map", "0:v:0", "-map", "0:a?", "-c", "copy", "-f", mp4 ? "mp4" : "matroska", staging->filePath("clip")});
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (staging) {
            status->setText("Export in progress. Close the window after it finishes.");
            event->ignore();
        } else QWidget::closeEvent(event);
    }

public:
    Editor()
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
        connect(&writer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err){
            if (err == QProcess::FailedToStart) {
                status->setText(writer.errorString());
                staging.reset();
            }
        });
        connect(&writer, &QProcess::finished, this, [this](int code, QProcess::ExitStatus state){
            if (!staging) return;
            const QString error = QString::fromUtf8(writer.readAllStandardError()).trimmed();
            if (state == QProcess::NormalExit && code == 0) {
                QFile output(staging->filePath("clip"));
                if (output.rename(destination)) status->setText("Saved " + destination);
                else status->setText("Could not save clip. " + output.errorString());
            } else {
                status->setText("Export failed. " + error.right(400));
                if (destination.endsWith(".mp4") &&
                    error.contains("codec not currently supported in container", Qt::CaseInsensitive)) {
                    QMessageBox box(QMessageBox::Information, "MP4 cannot carry this track",
                        "A selected track cannot be copied into MP4. Save the same range as MKV to keep all tracks.",
                        QMessageBox::Cancel, this);
                    auto *retry = box.addButton("Save as MKV", QMessageBox::AcceptRole);
                    box.exec();
                    if (box.clickedButton() == retry) {
                        const QString base = destination.chopped(4);
                        staging.reset();
                        saveClip(base, true);
                        return;
                    }
                }
            }
            staging.reset();
        });
        refresh();
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Editor editor;
    editor.show();
    return app.exec();
}
