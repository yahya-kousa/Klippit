#include "Replay.h"
#include "Loopback.h"
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
static bool run(const QStringList &args, int timeoutMs, bool desktop = false)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    QProcess helper;
    if (desktop && !startLoopback(helper, process)) return false;
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
    if (desktop && helper.state() != QProcess::NotRunning && !helper.waitForFinished(1000)) {
        helper.kill();
        helper.waitForFinished();
        return false;
    }
    if (desktop && (helper.exitStatus() != QProcess::NormalExit || helper.exitCode() != 0)) return false;
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && QByteArrayView(argv[1]) == "--internal-loopback") return streamLoopback();
    QCoreApplication app(argc, argv);
    QCommandLineParser cli;
    cli.setApplicationDescription("Record 15 seconds of one Windows display to a new MKV.");
    cli.addHelpOption();
    cli.addPositionalArgument("output.mkv", "New output file");
    cli.addOptions({
        {"replay", "Keep recent encoded video in RAM (Ctrl+Shift+F9 saves, Ctrl+Shift+End or console Q stops)."},
        {"desktop-audio", "Capture the default Windows playback device."},
        {"mic", "Include this DirectShow microphone along with desktop audio.", "device"},
        {"separate-mic", "Store desktop and microphone on separate tracks."},
        {"audio-device", "Capture sound from the named Windows DirectShow recording device.", "name"},
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

    const bool mic = cli.isSet("mic");
    const bool desktop = cli.isSet("desktop-audio") || mic;
    const bool separate = cli.isSet("separate-mic");
    if ((desktop && cli.isSet("audio-device")) || (mic && cli.value("mic").trimmed().isEmpty()) ||
        (separate && !mic)) cli.showHelp(2);

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
                            "-i", capture};
    QString encoder;
    QStringList args;
    for (const auto &candidate : candidates) {
        args = QStringList{"-c:v", candidate, "-b:v", QString::number(bitrate) + "M",
                                  "-g", QString::number(fps), "-fps_mode:v", "passthrough"};
        // Preserve desktop capture timing between nominal frame ticks.
        if (candidate == "h264_nvenc") args << "-enc_time_base:v" << "demux";
        if (run(input + args + QStringList{"-frames:v", "1", "-f", "null", "-"}, 10000)) {
            encoder = candidate;
            break;
        }
    }
    if (encoder.isEmpty()) {
        qWarning("No tested encoder could capture this display. Check the display index, graphics driver and FFmpeg build.");
        return 1;
    }
    const QString audioDevice = cli.value("audio-device");
    QStringList inputs = input;
    if (desktop || cli.isSet("audio-device")) {
        if (desktop) {
            inputs << "-thread_queue_size" << "128" << "-probesize" << "32" << "-ignore_length" << "1"
                   << "-f" << "wav" << "-i" << "pipe:0";
        } else {
            if (audioDevice.trimmed().isEmpty()) cli.showHelp(2);
            inputs << "-f" << "dshow" << "-i" << "audio=" + audioDevice;
        }
        if (mic) inputs << "-thread_queue_size" << "128" << "-f" << "dshow"
                        << "-i" << "audio=" + cli.value("mic");
        inputs << "-map" << "0:v:0";
        if (mic && !separate) {
            inputs << "-filter_complex" << "[1:a]aformat=channel_layouts=stereo[d];[2:a]aformat=channel_layouts=stereo[m];"
                      "[d][m]amix=inputs=2:duration=first[mixed]"
                   << "-map" << "[mixed]";
        } else {
            inputs << "-map" << "1:a:0";
            if (mic) inputs << "-map" << "2:a:0";
        }
        args << "-c:a" << "aac" << "-ar" << "48000" << "-ac" << "2" << "-b:a" << "192k";
    } else {
        args << "-an";
    }
    args = inputs + args;
    if (cli.isSet("replay")) return recordReplay(args, output.absoluteFilePath(), desktop);
    QTemporaryDir staging(output.dir().filePath(".klippit-record-XXXXXX"));
    if (!staging.isValid()) return 1;
    const QString clip = staging.filePath("recording.mkv");
    QElapsedTimer timer;
    timer.start();
    if (!run(args + QStringList{"-t", "15", clip}, 45000, desktop)) return 1;
    QFile file(clip);
    if (!file.rename(output.absoluteFilePath())) {
        qWarning().noquote() << file.errorString();
        return 1;
    }
    const QJsonObject result{{"platform", QSysInfo::prettyProductName()}, {"file", output.absoluteFilePath()},
        {"desktop_audio", desktop}, {"mic", cli.value("mic")}, {"separate_mic", separate},
        {"audio_device", audioDevice}, {"capture", capture}, {"encoder", encoder}, {"target_mbps", bitrate}, {"target_fps", fps},
        {"requested_seconds", 15}, {"record_and_finalize_ms", timer.elapsed()}};
    QTextStream(stdout) << QJsonDocument(result).toJson();
    return 0;
}
