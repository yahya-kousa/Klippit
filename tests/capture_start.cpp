#include "Loopback.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

// Exercise the same process-to-process pipe as the Windows recorder with
// synthetic inputs. The internal helper inherits stdout connected to FFmpeg.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--internal-loopback")) {
        QFile marker(qEnvironmentVariable("KLIPPIT_TEST_HELPER_MARKER"));
        if (!marker.open(QIODevice::WriteOnly)) return 1;
        marker.close();
        return QProcess::execute("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
                                 "sine=duration=0.2", "-f", "wav", "pipe:1"});
    }

    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    const QString marker = dir.filePath("started");
    qputenv("KLIPPIT_TEST_HELPER_MARKER", marker.toUtf8());
    for (bool validInput : {false, true}) {
        QProcess capture, helper;
        const QStringList args{"-nostdin", "-hide_banner", "-nostats", "-loglevel", "info",
                               "-f", "lavfi", "-i", validInput ? "color=d=0.2" : "no_such_filter",
                               "-f", "wav", "-i", "pipe:0", "-map", "0:v", "-map", "1:a",
                               "-f", "null", "-"};
        if (startCapture(capture, args, &helper) != validInput) return 1;
        if (validInput && (!capture.waitForFinished(10000) || capture.exitCode() != 0)) return 1;
        if (helper.state() != QProcess::NotRunning && !helper.waitForFinished(5000)) return 1;
        if (QFile::exists(marker) != validInput) return 1;
    }
    return 0;
}
