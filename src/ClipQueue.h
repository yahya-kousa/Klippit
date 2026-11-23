#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

class ClipQueue {
public:
    struct Entry {
        QString id;
        QString path;
        QDateTime savedAt;
    };

    explicit ClipQueue(QString directory = storageDirectory());
    const QString &directory() const { return root; }
    bool ensureDirectory() const;
    bool add(const QString &path) const;
    QList<Entry> entries(bool completed, bool *readError = nullptr) const;
    bool setCompleted(const Entry &entry, bool completed) const;

    bool removeCompleted(const Entry &entry) const;

private:
    static QString storageDirectory();
    QString entryPath(const QString &id, bool completed) const;
    QString root;
};
