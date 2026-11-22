#include "ReplayBuffer.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <QTemporaryDir>
#include <QtEndian>
#include <utility>

namespace {
// Video is mapped first. Use the first traf (video clock) for each keyframe-aligned fragment.
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

Replay::Replay(QString path, qint64 maxHistoryBytes, int historySeconds, ClipQueue clipQueue)
    : queue(std::move(clipQueue)), firstPath(std::move(path)), byteLimit(maxHistoryBytes),
      historyTicks(historySeconds * replayTimescale)
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
                   ring.back().start - ring[1].start >= historyTicks)) {
                used -= ring.front().data.size();
                ring.pop_front();
            }
            if (waitingForFragment()) queueSave();
        }
    }
    return true;
}

void Replay::report(const char *event, const QString &detail)
{
    const QJsonObject message{{"event", QString::fromLatin1(event)}, {"detail", detail}};
    QTextStream(stdout) << QJsonDocument(message).toJson(QJsonDocument::Compact) << Qt::endl;
}

void Replay::request()
{
    if (waitingForFragment()) {
        report("clip_coalesced", QStringLiteral("Wait for the next video fragment before saving again."));
        return;
    }
    requestTimer.start();
}

void Replay::queueSave()
{
    Save snapshot;
    snapshot.timer = requestTimer;
    requestTimer.invalidate();
    auto first = ring.begin();
    const qint64 availableTicks = ring.back().start - ring.front().start;
    if (availableTicks < historyTicks && !previousBoundary)
        qInfo() << "Replay history available before the final fragment:"
                << double(availableTicks) / replayTimescale << "seconds.";

    // Start at the first complete GOP after the previous clip's final GOP.
    // Fragment boundaries keep compressed video decodable without overlap.
    while (first != ring.end() && previousBoundary && first->start <= *previousBoundary)
        ++first;
    for (auto fragment = first; fragment != ring.end(); ++fragment) {
        snapshot.fragments.append(fragment->data);
        snapshot.bytes += fragment->data.size();
    }
    if (snapshot.fragments.isEmpty()) {
        report("clip_coalesced", QStringLiteral("No new video fragment is available."));
        return;
    }
    if (snapshot.bytes > byteLimit - saveBytes) {
        report("clip_rejected", QStringLiteral("Save queue memory limit reached. Try again after the current save."));
        return;
    }
    snapshot.firstFragment = first->start;
    snapshot.lastFragment = ring.back().start;
    previousBoundary = snapshot.lastFragment;
    saveBytes += snapshot.bytes;
    saves.push_back(std::move(snapshot));
    if (saves.size() == 1) writeNext();
}

void Replay::finishSave(bool success, const QString &path)
{
    const auto &snapshot = saves.front();
    if (success) {
        publishedBoundary = snapshot.lastFragment;
        const auto elapsed = snapshot.timer.elapsed();
        const bool queued = queue.add(path);
        if (!queued) qWarning() << "Clip saved, but could not add it to Clips to Trim:" << path;
        qInfo() << "Saved" << path << "in" << elapsed << "ms";
        const QJsonObject message{{"event", "clip_saved"}, {"path", path}, {"elapsed_ms", elapsed},
                                  {"queued", queued}, {"first_fragment_ms", snapshot.firstFragment / 90},
                                  {"last_fragment_ms", snapshot.lastFragment / 90}};
        QTextStream(stdout) << QJsonDocument(message).toJson(QJsonDocument::Compact) << Qt::endl;
    } else {
        qWarning() << "Save failed" << path;
        report("clip_failed", path);
        // When no later range depends on this boundary, a new press can
        // recover the failed range while its fragments still remain in RAM.
        if (saves.size() == 1) previousBoundary = publishedBoundary;
    }
    saveBytes -= snapshot.bytes;
    saves.pop_front();
    if (!saves.empty()) QTimer::singleShot(0, &writer, [this]{ writeNext(); });
}

void Replay::writeNext()
{
    const QFileInfo first(firstPath);
    QString path = firstPath;
    if (filenames++ || QFileInfo::exists(path)) {
        path = first.dir().filePath(first.completeBaseName() + "-" +
            QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss-zzz") + "-" +
            QUuid::createUuid().toString(QUuid::Id128).left(8) + ".mkv");
    }
    QTemporaryDir stage(first.dir().filePath(".klippit-save-XXXXXX"));
    if (!stage.isValid()) {
        finishSave(false, path);
        return;
    }
    const QString temp = stage.filePath("clip.mkv");
    const QString dir = stage.path();
    const auto connection = QObject::connect(&writer, &QProcess::finished, &writer,
        [this, path, temp, dir](int code, QProcess::ExitStatus status){
            const bool published = status == QProcess::NormalExit && code == 0 && QFile::rename(temp, path);
            if (!published) QFile::remove(temp);
            QDir().rmdir(dir);
            finishSave(published, path);
        }, Qt::SingleShotConnection);
    writer.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    writer.start("ffmpeg", {"-nostdin","-v","error","-f","mp4","-i","pipe:0",
                            "-map","0:v:0","-map","0:a?","-c","copy",temp});
    if (!writer.waitForStarted()) {
        QObject::disconnect(connection);
        QDir().rmdir(dir);
        qWarning() << writer.errorString();
        finishSave(false, path);
        return;
    }
    stage.setAutoRemove(false);
    writer.write(header);
    for (const auto &fragment : saves.front().fragments) writer.write(fragment);
    writer.closeWriteChannel();
}
