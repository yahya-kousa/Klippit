#include "ReplayBuffer.h"
#include "ReplayAppFolders.h"
#include <QCoreApplication>
#include <QFile>
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryDir>
#include <QtEndian>

// Feed the same FFmpeg byte stream that the capture process writes, including
// split box headers. Request two overlapping saves while ingestion continues.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList grouped{"brave", "WindowsTerminal"};
    if (replayAppFolder(replayAppName("C:/Apps/Brave.exe"), grouped) != "Desktop" ||
        replayAppFolder(replayAppName("C:/Apps/Brave.exe"), {}) != "Brave" ||
        replayAppFolder(replayAppName("notepad.exe"), grouped) != "notepad" ||
        replayAppFolder(replayAppName("zelda-recomp.exe"), grouped) != "zelda-recomp" ||
        replayAppFolder(replayAppName("explorer.exe"), {}) != "Desktop") return 1;
    const auto longshot = replayAppName("Longshot.exe");
    const QVariantMap customNames{{"longshot", "Ocarina of Time"}};
    if (replayAppFolder(longshot, {}, customNames) != "Ocarina of Time" ||
        replayAppFolder(longshot, {"LONGSHOT"}, customNames) != "Desktop" ||
        replayAppFolder(longshot, {}, {{"longshot", "../CON"}}) != "___CON" ||
        replayAppFolder(longshot, {}, {{"longshot", "CON"}}) != "_CON") return 1;
    if (argc != 8) return 1;
    QFile input(app.arguments()[1]);
    if (!input.open(QIODevice::ReadOnly)) return 1;
    QTemporaryDir queueDir;
    if (!queueDir.isValid()) return 1;
    Replay replay(app.arguments()[2], app.arguments()[3].toLongLong(),
                  app.arguments()[4].toInt(), ClipQueue(queueDir.path()));
    const int first = app.arguments()[5].toInt();
    const int second = app.arguments()[6].toInt();
    const bool overlapWrites = app.arguments()[7] == "overlap";
    auto waitForSaves = [&] {
        if (!replay.saving()) return;
        QEventLoop loop;
        QTimer monitor;
        QObject::connect(&monitor, &QTimer::timeout, &loop, [&] {
            if (!replay.saving()) loop.quit();
        });
        monitor.start(1);
        loop.exec();
    };
    int fragments = 0;
    while (!input.atEnd()) {
        const auto header = input.read(8);
        if (header.size() != 8) return 1;
        const auto size = qFromBigEndian<quint32>(header.constData());
        if (size < 8 || !replay.ingest(header.left(3)) || !replay.ingest(header.mid(3))) return 1;
        if (header.mid(4) == "mdat" && (++fragments == first || fragments == second)) {
            if (fragments == second) {
                if (!replay.saving()) return 1;
                if (!overlapWrites) {
                    waitForSaves();
                }
            }
            if (app.arguments()[4] == "60") {
                const auto appName = fragments == first ? longshot : QStringLiteral("Brave");
                replay.request(replayAppFolder(appName, grouped, customNames), appName);
            } else {
                replay.request();
            }
        }
        if (!replay.ingest(input.read(size - 8))) return 1;
    }
    waitForSaves();
    if (fragments < second) return 1;

    // Reopen the on-disk queue independently of the recorder instance.
    const ClipQueue queue(queueDir.path());
    const auto pending = queue.entries(false);
    if (pending.size() != 2) return 1;
    for (const auto &entry : pending) {
        if (!entry.unseen || !QFile::exists(entry.path)) return 1;
    }
    const auto entry = pending.first();
    queue.markSeen({entry});
    if (!queue.setCompleted(entry, true)) return 1;
    const ClipQueue reopened(queueDir.path());
    const auto completed = reopened.entries(true);
    if (reopened.entries(false).size() != 1 || completed.size() != 1 ||
        !reopened.entries(false).first().unseen || completed.first().unseen || completed.first().id != entry.id || !QFile::exists(entry.path)) return 1;
    if (!reopened.setCompleted(completed.first(), false)) return 1;
    if (!reopened.entries(true).isEmpty() || reopened.entries(false).size() != 2) return 1;
    return reopened.removeEntry(entry, false) && QFile::exists(entry.path) ? 0 : 1;
}
