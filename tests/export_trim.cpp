#include "ClipExporter.h"
#include "TrimRange.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTimer>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 6) return 1;
    std::vector<qint64> keys;
    for (const auto &key : args.mid(5)) keys.push_back(key.toLongLong());

    const auto range = losslessRange(keys, 5800000, 10500000);
    const qint64 expectedStart = args[4].toLongLong();
    if (!range || range->startUs != expectedStart || range->durationUs != 10500000 - expectedStart) {
        qCritical() << "Unexpected effective trim range";
        return 1;
    }
    // Marks selected by frame stepping may fall between whole milliseconds.
    const auto stepped = losslessRange({0, 16667}, 16667, 33333);
    if (!stepped || stepped->startUs != 16667 || stepped->durationUs != 16666) {
        qCritical() << "Frame timestamp lost precision";
        return 1;
    }
    QTemporaryDir trims;
    if (!trims.isValid()) return 1;
    const QString base = trims.filePath("match-trim");
    if (ClipExporter::availableStem(base, false) != base ||
        !QFile(base + ".mp4").open(QIODevice::WriteOnly) ||
        ClipExporter::availableStem(base, true) != base ||
        ClipExporter::availableStem(base, false) != base + "-2" ||
        !QFile(base + "-2.mkv").open(QIODevice::WriteOnly) ||
        ClipExporter::availableStem(base, false) != base + "-3") {
        qCritical() << "Unexpected suggested trim filename";
        return 1;
    }
    ClipExporter exporter;
    QObject::connect(&exporter, &ClipExporter::statusChanged, &app, [&](const QString &message){
        if (exporter.isBusy()) return;
        qInfo().noquote() << message;
        app.exit(message == "Saved " + args[2] ? 0 : 1);
    });
    QTimer::singleShot(0, &app, [&]{
        const auto format = args[3] == "mp4" ? ClipExporter::Format::Mp4 : ClipExporter::Format::Mkv;
        exporter.start(args[1], args[2], format, range->startUs, range->durationUs);
    });
    return app.exec();
}
