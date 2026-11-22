#pragma once

#include <QElapsedTimer>
#include <QProcess>
#include <deque>
#include <optional>

inline constexpr qint64 replayTimescale = 90000;

class Replay {
public:
    explicit Replay(QString path, qint64 maxHistoryBytes = 96 * 1024 * 1024);
    bool ingest(const QByteArray &chunk);
    void request();
    bool waitingForFragment() const { return requested; }
    bool saving() const { return writer.state() != QProcess::NotRunning; }

private:
    struct Fragment {
        qint64 start;
        QByteArray data;
    };
    void save();
    QProcess writer;
    QString firstPath;
    QByteArray header, pending, metadata;
    std::optional<qint64> start;
    std::deque<Fragment> ring;
    const qint64 byteLimit;
    qint64 used = 0;
    QElapsedTimer timer;
    bool requested = false, savedOnce = false;
};
