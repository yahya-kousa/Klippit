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
        bool unseen = false;
    };

    explicit ClipQueue(QString directory = storageDirectory());
    const QString &directory() const { return root; }
    bool ensureDirectory() const;
    bool add(const QString &path) const;
    QList<Entry> entries(bool completed, bool *readError = nullptr) const;
    void markSeen(const QList<Entry> &entries) const;
    bool setCompleted(const Entry &entry, bool completed) const;

    bool removeEntry(const Entry &entry, bool completed) const;

private:
    static QString storageDirectory();
    QString entryPath(const QString &id, bool completed) const;
    QString root;
};
