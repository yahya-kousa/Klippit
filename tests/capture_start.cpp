#include "Loopback.h"

#include <QCoreApplication>
#include <QFile>
#include <cstdio>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif
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
        QFile fixture(qEnvironmentVariable("KLIPPIT_TEST_WAV"));
        QFile pipe;
#ifdef Q_OS_WIN
        if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 1;
#endif
        if (!fixture.open(QIODevice::ReadOnly) || !pipe.open(stdout, QIODevice::WriteOnly))
            return 1;
        const auto wave = fixture.readAll();
        return !wave.isEmpty() && pipe.write(wave) == wave.size() && pipe.flush() ? 0 : 1;
    }

    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    // A seekable output lets FFmpeg put the actual data length in the WAV header.
    const QString wav = dir.filePath("tone.wav");
    if (QProcess::execute("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
                                    "sine=duration=2", wav}) != 0) return 1;
    qputenv("KLIPPIT_TEST_WAV", wav.toUtf8());
    const QString marker = dir.filePath("started");
    qputenv("KLIPPIT_TEST_HELPER_MARKER", marker.toUtf8());
    for (bool validInput : {false, true}) {
        QProcess capture, helper;
        const QStringList args{"-nostdin", "-hide_banner", "-nostats", "-loglevel", "info",
                               "-f", "lavfi", "-i", validInput ? "color=d=0.2" : "no_such_filter",
                               "-probesize", "32", "-ignore_length", "1", "-f", "wav", "-i", "pipe:0", "-map", "0:v", "-map", "1:a",
                               "-t", "0.2", "-abort_on", "empty_output_stream", "-f", "null", "-"};
        if (startCapture(capture, args, &helper) != validInput) {
            std::fprintf(stderr, "Unexpected startup result for %s video input.\n",
                         validInput ? "valid" : "invalid");
            return 1;
        }
        if (validInput) {
            // The recorder consumes a live pipe. Finish on media time even if the
            // producer still has samples to write and stdin has not reached EOF.
            const bool finished = capture.waitForFinished(10000) ||
                                  capture.state() == QProcess::NotRunning;
            if (!finished || capture.exitStatus() != QProcess::NormalExit || capture.exitCode() != 0) {
                std::fprintf(stderr, "Capture FFmpeg failed: finished=%d code=%d error=%s\n",
                             int(finished), capture.exitCode(), qPrintable(capture.errorString()));
                return 1;
            }
            if (helper.state() != QProcess::NotRunning && !helper.waitForFinished(1000) &&
                helper.state() != QProcess::NotRunning) {
                helper.kill();
                if (!helper.waitForFinished()) return 1;
            }
        }
        if (QFile::exists(marker) != validInput) {
            std::fputs("Unexpected helper startup marker.\n", stderr);
            return 1;
        }
    }
    return 0;
}
