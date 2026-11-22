#include "Replay.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextStream>

// Let FFmpeg negotiate D3D11 capture surfaces with the actual encoder and driver.
// A listed encoder can still fail to initialize on the selected display adapter.
static bool run(const QStringList &args, int timeoutMs)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process.start("ffmpeg", args);
    if (!process.waitForStarted()) {
        qWarning().noquote() << process.errorString();
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        qWarning("FFmpeg exceeded the recording timeout.");
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser cli;
    cli.setApplicationDescription("Record 15 seconds of one Windows display to a new video-only MKV.");
    cli.addHelpOption();
    cli.addPositionalArgument("output.mkv", "New output file");
    cli.addOptions({
        {"replay", "Keep recent encoded video in RAM (Ctrl+Shift+F9 saves, Ctrl+Shift+F10 stops)."},
        {"encoder", "auto, h264_nvenc, h264_amf, or h264_qsv", "name", "auto"},
        {"display", "DXGI output index", "index", "0"},
        {"fps", "Capture rate in frames per second (1-240)", "fps", "60"},
        {"bitrate", "Target video bitrate in Mbit/s (1-500)", "mbps", "30"},
    });
    cli.process(app);
    const auto paths = cli.positionalArguments();
    const int fps = cli.value("fps").toInt();
    const int bitrate = cli.value("bitrate").toInt();
    bool validDisplay;
    const int display = cli.value("display").toInt(&validDisplay);
    if (paths.size() != 1 || !paths.first().endsWith(".mkv", Qt::CaseInsensitive) ||
        fps < 1 || fps > 240 || bitrate < 1 || bitrate > 500 || !validDisplay || display < 0)
        cli.showHelp(2);

    const QFileInfo output(paths.first());
    if (output.exists()) {
        qWarning("Choose a new output filename.");
        return 1;
    }
    QStringList candidates{"h264_nvenc", "h264_amf", "h264_qsv"};
    const QString requested = cli.value("encoder");
    if (requested != "auto") {
        if (!candidates.contains(requested)) cli.showHelp(2);
        candidates = {requested};
    }
    const QString capture = QString("ddagrab=output_idx=%1:framerate=%2:dup_frames=1")
                                .arg(display).arg(fps);
    const QStringList input{"-nostdin", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
                            "-i", capture, "-an"};
    QString encoder;
    QStringList args;
    for (const auto &candidate : candidates) {
        args = input + QStringList{"-c:v", candidate, "-b:v", QString::number(bitrate) + "M",
                                  "-g", QString::number(fps), "-fps_mode", "passthrough"};
        // Preserve desktop capture timing between nominal frame ticks.
        if (candidate == "h264_nvenc") args << "-enc_time_base" << "demux";
        if (run(args + QStringList{"-frames:v", "1", "-f", "null", "-"}, 10000)) {
            encoder = candidate;
            break;
        }
    }
    if (encoder.isEmpty()) {
        qWarning("No tested encoder could capture this display. Check the display index, graphics driver and FFmpeg build.");
        return 1;
    }
    if (cli.isSet("replay")) return recordReplay(args, output.absoluteFilePath());
    QTemporaryDir staging(output.dir().filePath(".klippit-record-XXXXXX"));
    if (!staging.isValid()) return 1;
    const QString clip = staging.filePath("recording.mkv");
    QElapsedTimer timer;
    timer.start();
    if (!run(args + QStringList{"-t", "15", clip}, 45000)) return 1;
    QFile file(clip);
    if (!file.rename(output.absoluteFilePath())) {
        qWarning().noquote() << file.errorString();
        return 1;
    }
    const QJsonObject result{{"platform", QSysInfo::prettyProductName()}, {"file", output.absoluteFilePath()},
        {"capture", capture}, {"encoder", encoder}, {"target_mbps", bitrate}, {"target_fps", fps},
        {"requested_seconds", 15}, {"record_and_finalize_ms", timer.elapsed()}};
    QTextStream(stdout) << QJsonDocument(result).toJson();
    return 0;
}
