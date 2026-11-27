#include "ClipExporter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

ClipExporter::ClipExporter(QObject *parent) : QObject(parent)
{
    connect(&writer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error){
        if (error == QProcess::FailedToStart) {
            staging.reset();
            emit statusChanged(writer.errorString());
        }
    });
    connect(&writer, &QProcess::finished, this, [this](int code, QProcess::ExitStatus state){
        if (!staging) return;
        const QString error = QString::fromUtf8(writer.readAllStandardError()).trimmed();
        QString message;
        const bool unsupported = (state != QProcess::NormalExit || code != 0) &&
            format == Format::Mp4 &&
            error.contains("codec not currently supported in container", Qt::CaseInsensitive);
        if (state == QProcess::NormalExit && code == 0) {
            QFile output(staging->filePath("clip"));
            if (output.rename(destination)) message = "Saved " + destination;
            else message = "Could not save clip. " + output.errorString();
        } else {
            message = "Export failed. " + error.right(400);
        }
        // Release the finished job before notifying callers so a retry can start.
        staging.reset();
        emit statusChanged(message);
        if (unsupported) emit mp4Unsupported();
    });
}

QString ClipExporter::availableStem(const QString &base, bool mkvOnly)
{
    QString candidate = base;
    for (int number = 2; QFileInfo::exists(candidate + ".mkv") ||
                         (!mkvOnly && QFileInfo::exists(candidate + ".mp4")); ++number) {
        candidate = base + '-' + QString::number(number);
    }
    return candidate;
}

bool ClipExporter::isBusy() const
{
    return bool(staging);
}

void ClipExporter::start(const QString &source, const QString &output, Format outputFormat,
                         qint64 startUs, qint64 durationUs)
{
    if (isBusy()) return;
    if (QFileInfo::exists(output)) {
        emit statusChanged("Choose a new filename for the export.");
        return;
    }
    destination = output;
    format = outputFormat;
    staging = std::make_unique<QTemporaryDir>(
        QFileInfo(destination).dir().filePath(".replay-export-XXXXXX"));
    if (!staging->isValid()) {
        staging.reset();
        emit statusChanged("Cannot write to this folder.");
        return;
    }
    emit statusChanged("Exporting…");
    writer.start("ffmpeg", {"-nostdin", "-v", "error",
        "-ss", QString::number(startUs / 1000000.0, 'f', 6),
        "-i", source, "-t", QString::number(durationUs / 1000000.0, 'f', 6),
        "-map", "0:v:0", "-map", "0:a?", "-c", "copy",
        "-f", format == Format::Mp4 ? "mp4" : "matroska", staging->filePath("clip")});
}
