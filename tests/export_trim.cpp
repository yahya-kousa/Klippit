#include "ClipExporter.h"
#include "TrimRange.h"
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 6) return 1;
    std::vector<qint64> keys;
    for (const auto &key : args.mid(5)) keys.push_back(key.toLongLong());

    const auto range = losslessRange(keys, 5800, 10500);
    const qint64 expectedStart = args[4].toLongLong();
    if (!range || range->startUs != expectedStart || range->durationUs != 10500000 - expectedStart) {
        qCritical() << "Unexpected effective trim range";
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
