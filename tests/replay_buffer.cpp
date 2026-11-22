#include "ReplayBuffer.h"
#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <QtEndian>

// Feed the same FFmpeg byte stream that the capture process writes, including
// split box headers. Request two overlapping saves while ingestion continues.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 4) return 1;
    QFile input(app.arguments()[1]);
    if (!input.open(QIODevice::ReadOnly)) return 1;
    Replay replay(app.arguments()[2], app.arguments()[3].toLongLong());
    int fragments = 0;
    while (!input.atEnd()) {
        const auto header = input.read(8);
        if (header.size() != 8) return 1;
        const auto size = qFromBigEndian<quint32>(header.constData());
        if (size < 8 || !replay.ingest(header.left(3)) || !replay.ingest(header.mid(3))) return 1;
        if (header.mid(4) == "mdat" && (++fragments == 18 || fragments == 22)) {
            if (fragments == 22) {
                while (replay.saving()) { app.processEvents(); QThread::msleep(1); }
            }
            replay.request();
        }
        if (!replay.ingest(input.read(size - 8))) return 1;
        app.processEvents();
    }
    while (replay.saving()) { app.processEvents(); QThread::msleep(1); }
    return fragments >= 22 ? 0 : 1;
}
