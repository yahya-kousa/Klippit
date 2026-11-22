#include "ReplayBuffer.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtEndian>
#include <utility>

namespace {
// The capture muxer emits a single track, with tfdt nested in moof/traf.
std::optional<qint64> decodeTime(QByteArrayView boxes)
{
    while (boxes.size() >= 8) {
        const auto size = qFromBigEndian<quint32>(boxes.data());
        if (size < 8 || size > boxes.size()) break;
        const auto type = boxes.sliced(4, 4);
        const auto body = boxes.sliced(8, size - 8);
        if (type == "traf") return decodeTime(body);
        if (type == "tfdt" && body.size() >= 8) {
            if (body[0] == 0) return qFromBigEndian<quint32>(body.data() + 4);
            if (body[0] == 1 && body.size() >= 12)
                return qFromBigEndian<quint64>(body.data() + 4);
        }
        boxes = boxes.sliced(size);
    }
    return {};
}

} // namespace

Replay::Replay(QString path, qint64 maxHistoryBytes)
    : firstPath(std::move(path)), byteLimit(maxHistoryBytes)
{}

bool Replay::ingest(const QByteArray &chunk)
{
    pending += chunk;
    while (pending.size() >= 8) {
        const auto size = qFromBigEndian<quint32>(pending.constData());
        if (size < 8 || size > 96 * 1024 * 1024) return false;
        if (size > pending.size()) break;
        QByteArray box = pending.left(size);
        pending.remove(0, size);
        const auto type = box.mid(4, 4);
        if (type == "ftyp" || type == "moov") {
            header += box;
        } else if (type == "moof") {
            start = decodeTime(QByteArrayView(box).sliced(8));
            if (!start || !metadata.isEmpty()) return false;
            metadata = std::move(box);
        } else if (type == "mdat") {
            if (!start || metadata.isEmpty()) return false;
            metadata += box;
            if (metadata.size() > byteLimit) {
                qWarning("One encoded fragment exceeded the replay memory limit.");
                return false;
            }
            used += metadata.size();
            ring.push_back({*start, std::move(metadata)});
            metadata.clear();
            start.reset();
            while (used > byteLimit || (ring.size() > 1 &&
                   ring.back().start - ring[1].start >= 15 * replayTimescale)) {
                used -= ring.front().data.size();
                ring.pop_front();
            }
            if (requested) save();
        }
    }
    return true;
}

void Replay::request()
{
    if (saving() || requested) return;
    requested = true; // Include the in-progress GOP when its next keyframe arrives.
    timer.start();
}

void Replay::save()
{
    requested = false;
    const QFileInfo first(firstPath);
    QString path = firstPath;
    if (savedOnce || QFileInfo::exists(path)) {
        path = first.dir().filePath(first.completeBaseName() + "-" +
            QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss-zzz") + ".mkv");
    }
    // Only an explicit save touches disk. Publish once the muxer succeeds.
    QTemporaryDir stage(first.dir().filePath(".klippit-save-XXXXXX"));
    if (!stage.isValid()) { qWarning("Cannot stage replay"); return; }
    stage.setAutoRemove(false);
    const QString temp = stage.filePath("clip.mkv");
    const QString dir = stage.path();
    auto connection = QObject::connect(&writer, &QProcess::finished, &writer,
        [this, path, temp, dir](int code, QProcess::ExitStatus status){
            if (status == QProcess::NormalExit && code == 0 && QFile::rename(temp, path)) {
                savedOnce = true;
                qInfo() << "Saved" << path << "in" << timer.elapsed() << "ms";
            } else {
                qWarning() << "Save failed" << path;
            }
            QFile::remove(temp);
            QDir().rmdir(dir);
        }, Qt::SingleShotConnection);
    writer.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    writer.start("ffmpeg", {"-nostdin","-v","error","-f","mp4","-i","pipe:0",
                            "-map","0:v:0","-c","copy",temp});
    if (!writer.waitForStarted()) {
        QObject::disconnect(connection);
        QDir().rmdir(dir);
        qWarning() << writer.errorString();
        return;
    }
    writer.write(header);
    for (const auto &fragment : ring) writer.write(fragment.data);
    writer.closeWriteChannel();
}
