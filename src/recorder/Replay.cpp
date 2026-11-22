#include "Replay.h"
#include "ReplayBuffer.h"
#include "Loopback.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <windows.h>
#include <conio.h>
#include <io.h>
#include <cstdio>
#endif

int recordReplay(QStringList args, const QString &firstPath, bool desktop)
{
    Replay replay(firstPath);
    QProcess capture;
    bool valid = true;
    QObject::connect(&capture, &QProcess::readyReadStandardOutput, [&]{
        while (capture.bytesAvailable() && valid)
            valid = replay.ingest(capture.read(256 * 1024));
        if (!valid) { qWarning("Invalid replay fragment"); capture.kill(); }
    });
    args << "-movflags" << "empty_moov+frag_keyframe+default_base_moof"
         << "-video_track_timescale" << QString::number(replayTimescale) << "-f" << "mp4" << "pipe:1";
    QProcess helper;
    if (!startCapture(capture, args, desktop ? &helper : nullptr)) return 1;
    QTimer controls;
#ifdef Q_OS_WIN
    if (!RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F9) ||
        !RegisterHotKey(nullptr, 2, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_END)) {
        qWarning("Could not register replay hotkeys");
        capture.kill();
        capture.waitForFinished();
        UnregisterHotKey(nullptr, 1);
        return 1;
    }
#endif
    qInfo("Replay active: Ctrl+Shift+F9 saves, Ctrl+Shift+End stops. You can also press Q in this console.");
    bool stopping = false;
#ifdef Q_OS_WIN
    // Read only from an interactive terminal, including one shared with the benchmark runner.
    const bool consoleInput = _isatty(_fileno(stdin)) != 0;
#endif
    QObject::connect(&controls, &QTimer::timeout, [&]{
#ifdef Q_OS_WIN
        if (!stopping && consoleInput && _kbhit()) {
            const int key = _getch();
            if (key == 0 || key == 0xe0) _getch(); // Consume extended key scan code.
            else if (key == 'q' || key == 'Q') {
                stopping = true;
                qInfo("Console stop received.");
            }
        }
#endif
        if (desktop && !stopping && helper.state() == QProcess::NotRunning &&
            (helper.exitStatus() != QProcess::NormalExit || helper.exitCode() != 0)) {
            valid = false;
            capture.kill();
        }
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
            if (msg->wParam == 2 && !stopping) {
                stopping = true;
                qInfo("Stop hotkey received.");
            }
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
    if (desktop && helper.state() != QProcess::NotRunning && !helper.waitForFinished(1000)) {
        helper.kill();
        helper.waitForFinished();
    }
    return result;
}
