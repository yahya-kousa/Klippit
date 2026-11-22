#include "Replay.h"
#include "ReplayBuffer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <windows.h>
#endif

int recordReplay(QStringList args, const QString &firstPath)
{
    Replay replay(firstPath);
    QProcess capture;
    capture.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    bool valid = true;
    QObject::connect(&capture, &QProcess::readyReadStandardOutput, [&]{
        while (capture.bytesAvailable() && valid)
            valid = replay.ingest(capture.read(256 * 1024));
        if (!valid) { qWarning("Invalid replay fragment"); capture.kill(); }
    });
    args << "-movflags" << "empty_moov+frag_keyframe+default_base_moof"
         << "-video_track_timescale" << QString::number(replayTimescale) << "-f" << "mp4" << "pipe:1";
    capture.start("ffmpeg", args);
    if (!capture.waitForStarted()) { qWarning() << capture.errorString(); return 1; }
    QTimer controls;
#ifdef Q_OS_WIN
    if (!RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F9) ||
        !RegisterHotKey(nullptr, 2, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F10)) {
        qWarning("Could not register replay hotkeys");
        capture.kill();
        capture.waitForFinished();
        UnregisterHotKey(nullptr, 1);
        return 1;
    }
#endif
    qInfo("Replay active: Ctrl+Shift+F9 saves, Ctrl+Shift+F10 stops.");
    bool stopping = false;
    QObject::connect(&controls, &QTimer::timeout, [&]{
        if (stopping && !replay.waitingForFragment() && capture.state() == QProcess::Running)
            capture.kill();
        if (capture.state() == QProcess::NotRunning && !replay.saving()) {
            QCoreApplication::exit(valid && (stopping || capture.exitCode() == 0) ? 0 : 1);
        }
    });
    controls.start(50);
#ifdef Q_OS_WIN
    class Keys : public QAbstractNativeEventFilter {
    public:
        Replay &replay;

        bool &stopping;
        Keys(Replay &r, bool &s) : replay(r), stopping(s) {}
        bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override {
            auto *msg = static_cast<MSG *>(message);
            if (msg->message != WM_HOTKEY) return false;
            if (msg->wParam == 1 && !stopping) replay.request();
            if (msg->wParam == 2) stopping = true;
            return true;
        }
    } keys(replay, stopping);
    QCoreApplication::instance()->installNativeEventFilter(&keys);
#endif
    const int result = QCoreApplication::exec();
#ifdef Q_OS_WIN
    QCoreApplication::instance()->removeNativeEventFilter(&keys);
    UnregisterHotKey(nullptr, 1);
    UnregisterHotKey(nullptr, 2);
#endif
    return result;
}
