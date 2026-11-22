#pragma once

#include "ClipQueue.h"
#include <QElapsedTimer>
#include <QProcess>
#include <QList>
#include <deque>
#include <optional>

inline constexpr qint64 replayTimescale = 90000;

class Replay {
public:
    explicit Replay(QString path, qint64 maxHistoryBytes = 96 * 1024 * 1024,
                    int historySeconds = 15, ClipQueue clipQueue = ClipQueue());
    bool ingest(const QByteArray &chunk);
    void request();
    bool waitingForFragment() const { return requestTimer.isValid(); }
    bool hasHistory() const { return !ring.empty(); }
    bool saving() const { return !saves.empty(); }

private:
    struct Fragment {
        qint64 start;
        QByteArray data;
    };
    struct Save {
        QList<QByteArray> fragments;
        QElapsedTimer timer;
        QElapsedTimer readyTimer;
        qint64 peakWriterBytes = 0;
        qint64 enqueueNs = 0;
        qint64 bytes = 0;
        qint64 firstFragment = 0;
        qint64 lastFragment = 0;
    };
    void queueSave();
    void writeNext();
    void finishSave(bool success, const QString &path);
    void report(const char *event, const QString &detail);

    QProcess writer;
    ClipQueue queue;
    QString firstPath;
    QByteArray header, pending, metadata;
    std::optional<qint64> start;
    std::deque<Fragment> ring;
    const qint64 byteLimit;
    const qint64 historyTicks;
    std::deque<Save> saves;
    std::optional<qint64> previousBoundary, publishedBoundary;
    qint64 saveBytes = 0;
    quint64 filenames = 0;
    qint64 used = 0;
    QElapsedTimer requestTimer;
};
