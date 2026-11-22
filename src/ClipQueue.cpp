#include "ClipQueue.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <utility>

ClipQueue::ClipQueue(QString directory) : root(std::move(directory))
{}

QString ClipQueue::storageDirectory()
{
    const auto data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return data.isEmpty() ? QString() : QDir(data).filePath("Klippit/clip-queue");
}

bool ClipQueue::ensureDirectory() const
{
    return !root.isEmpty() && QDir().mkpath(root);
}

QString ClipQueue::entryPath(const QString &id, bool completed) const
{
    return QDir(root).filePath(id + (completed ? ".done.json" : ".pending.json"));
}

bool ClipQueue::add(const QString &path) const
{
    const QFileInfo clip(path);
    if (!clip.isFile() || !ensureDirectory()) return false;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject entry{{"path", clip.absoluteFilePath()},
                            {"saved_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    const auto bytes = QJsonDocument(entry).toJson(QJsonDocument::Compact);
    QSaveFile file(entryPath(id, false));
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QList<ClipQueue::Entry> ClipQueue::entries(bool completed, bool *readError) const
{
    if (readError) *readError = false;
    QList<Entry> result;
    if (root.isEmpty()) return result;
    const QString suffix = completed ? ".done.json" : ".pending.json";
    for (const auto &name : QDir(root).entryList({"*" + suffix}, QDir::Files)) {
        const auto id = name.chopped(suffix.size());
        if (QUuid::fromString(id).isNull()) continue;
        QFile file(entryPath(id, completed));
        // Another editor may have changed the state since the directory scan.
        if (!file.open(QIODevice::ReadOnly) && !file.exists()) continue;
        const auto entry = QJsonDocument::fromJson(file.isOpen() ? file.readAll() : QByteArray()).object();
        const auto path = entry["path"].toString();
        const auto savedAt = QDateTime::fromString(entry["saved_at"].toString(), Qt::ISODateWithMs);
        if (!QDir::isAbsolutePath(path) || !savedAt.isValid()) {
            if (readError) *readError = true;
            continue;
        }
        result.append({id, path, savedAt});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        if (a.savedAt != b.savedAt) return a.savedAt > b.savedAt;
        return a.id < b.id;
    });
    return result;
}

bool ClipQueue::setCompleted(const Entry &entry, bool completed) const
{
    return !root.isEmpty() && !QUuid::fromString(entry.id).isNull() &&
           QFile::rename(entryPath(entry.id, !completed), entryPath(entry.id, completed));
}
