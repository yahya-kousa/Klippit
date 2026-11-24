#include "Replay.h"
#include "ReplayBuffer.h"
#include "Loopback.h"
#include "ReplayHotkey.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTimer>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <windows.h>
#include "../ReplayAppFolders.h"
#include <QSettings>
#include <QDir>
#include <conio.h>
#include <io.h>
#include <cstdio>

namespace {
constexpr int saveHotkeyId = 1;
constexpr int stopHotkeyId = 2;
constexpr int alternateSaveHotkeyId = 3;

QString foregroundApp()
{
    const HWND window = GetForegroundWindow();
    DWORD pid = 0;
    if (!window || !GetWindowThreadProcessId(window, &pid)) return "Desktop";
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return "Desktop";
    const auto closeProcess = qScopeGuard([process] { CloseHandle(process); });
    wchar_t path[32768];
    DWORD length = 32768;
    if (!QueryFullProcessImageNameW(process, 0, path, &length)) return "Desktop";
    const QString executable = QDir::fromNativeSeparators(QString::fromWCharArray(path, length));
    return replayAppName(executable);
}

bool registerSaveHotkey(int id, const ReplayHotkey &hotkey)
{
    UINT modifiers = MOD_NOREPEAT;
    if (hotkey.modifiers & ReplayHotkey::Ctrl) modifiers |= MOD_CONTROL;
    if (hotkey.modifiers & ReplayHotkey::Alt) modifiers |= MOD_ALT;
    if (hotkey.modifiers & ReplayHotkey::Shift) modifiers |= MOD_SHIFT;
    return RegisterHotKey(nullptr, id, modifiers, hotkey.key);
}
}
#endif

int recordReplay(QStringList args, const QString &firstPath, bool desktop, int historySeconds, qint64 bufferBytes, bool managed, const QString &saveHotkey)
{
    QString activeHotkey = saveHotkey;
#ifdef Q_OS_WIN
    auto hotkey = parseReplayHotkey(saveHotkey);
    if (!hotkey) return 2;
    int activeHotkeyId = saveHotkeyId;
    if (!registerSaveHotkey(activeHotkeyId, *hotkey)) {
        const DWORD error = GetLastError();
        QTextStream(stderr) << "Could not register save hotkey " << saveHotkey
                            << ". Choose another shortcut. Windows error " << error << Qt::endl;
        return 1;
    }
    const auto releaseSave = qScopeGuard([&] { UnregisterHotKey(nullptr, activeHotkeyId); });
    if (!RegisterHotKey(nullptr, stopHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_END)) {
        QTextStream(stderr) << "Could not register stop hotkey Ctrl+Shift+End." << Qt::endl;
        return 1;
    }
    const auto releaseStop = qScopeGuard([] { UnregisterHotKey(nullptr, stopHotkeyId); });
    auto changeHotkey = [&](const QString &requested) {
        const auto replacement = parseReplayHotkey(requested);
        QString detail;
        if (!replacement) {
            detail = "Invalid save hotkey.";
        } else if (replacement != hotkey) {
            const int newId = activeHotkeyId == saveHotkeyId ? alternateSaveHotkeyId : saveHotkeyId;
            if (registerSaveHotkey(newId, *replacement)) {
                UnregisterHotKey(nullptr, activeHotkeyId);
                activeHotkeyId = newId;
                hotkey = replacement;
            } else {
                detail = QString("Windows could not register %1. Still using %2.")
                    .arg(requested, activeHotkey);
            }
        }
        if (detail.isEmpty()) activeHotkey = requested;
        const QJsonObject event{{"event", detail.isEmpty() ? "save_hotkey_changed" : "save_hotkey_rejected"},
                                {"requested", requested}, {"save_hotkey", activeHotkey}, {"detail", detail}};
        QTextStream(stdout) << QJsonDocument(event).toJson(QJsonDocument::Compact) << Qt::endl;
    };
#endif
    Replay replay(firstPath, bufferBytes, historySeconds);
    QProcess capture;
    bool valid = true;
    QObject::connect(&capture, &QProcess::readyReadStandardOutput, [&]{
        while (capture.bytesAvailable() && valid)
            valid = replay.ingest(capture.read(256 * 1024));
        if (!valid) { qWarning("Invalid replay fragment"); capture.kill(); }
    });
    // A failed video input must stop the pipeline even when audio is still live.
    args.prepend("-xerror");
    args << "-movflags" << "empty_moov+frag_keyframe+default_base_moof"
         << "-video_track_timescale" << QString::number(replayTimescale) << "-f" << "mp4" << "pipe:1";
    QProcess helper;
    if (!startCapture(capture, args, desktop ? &helper : nullptr)) return 1;
    QTimer controls;
    bool readyReported = false;
    qInfo().noquote() << QString("Replay active: %1 saves, Ctrl+Shift+End stops.").arg(saveHotkey);
    if (!managed) qInfo("You can also press Q in this console.");
    bool stopping = false;
    bool stoppedCapture = false;
    QElapsedTimer stopWait;
#ifdef Q_OS_WIN
    // Read only from an interactive terminal, including one shared with the benchmark runner.
    const bool consoleInput = !managed && _isatty(_fileno(stdin)) != 0;
    const HANDLE controllerInput = managed ? GetStdHandle(STD_INPUT_HANDLE) : nullptr;
    QByteArray commands;
#endif
    QObject::connect(&controls, &QTimer::timeout, [&]{
#ifdef Q_OS_WIN
        // The tray owns the write end. EOF also stops capture if the tray exits.
        if (!stopping && managed) {
            DWORD available = 0;
            if (!PeekNamedPipe(controllerInput, nullptr, 0, nullptr, &available, nullptr)) {
                stopping = true;
            } else if (available > 0) {
                char bytes[128];
                DWORD read = 0;
                if (ReadFile(controllerInput, bytes, qMin(available, DWORD(sizeof(bytes))), &read, nullptr)) {
                    commands.append(bytes, read);
                    qsizetype delimiter;
                    while ((delimiter = commands.indexOf('\n')) >= 0) {
                        if (commands.startsWith("hotkey "))
                            changeHotkey(QString::fromUtf8(commands.constData() + 7, delimiter - 7));
                        commands.remove(0, delimiter + 1);
                    }
                    if (commands.size() > 128) commands.clear();
                } else {
                    stopping = true;
                }
            }
        }
        if (!stopping && consoleInput && _kbhit()) {
            const int key = _getch();
            if (key == 0 || key == 0xe0) _getch(); // Consume extended key scan code.
            else if (key == 'q' || key == 'Q') {
                stopping = true;
                qInfo("Console stop received.");
            }
        }
#endif
        if (managed && !stopping && valid && !readyReported && replay.hasHistory() &&
            capture.state() == QProcess::Running) {
            readyReported = true;
            const QJsonObject event{{"event", "replay_ready"}, {"save_hotkey", activeHotkey}};
            QTextStream(stdout) << QJsonDocument(event).toJson(QJsonDocument::Compact) << Qt::endl;
        }
        if (desktop && !stopping && helper.state() == QProcess::NotRunning &&
            (helper.exitStatus() != QProcess::NormalExit || helper.exitCode() != 0)) {
            valid = false;
            capture.kill();
        }
        if (stopping && capture.state() == QProcess::Running) {
            if (!stopWait.isValid()) stopWait.start();
            if (!replay.waitingForFragment() || stopWait.hasExpired(5000)) {
                stoppedCapture = true;
                capture.kill();
            }
        }
        if (capture.state() == QProcess::NotRunning && !replay.saving()) {
            const bool success = valid && (stoppedCapture ||
                (capture.exitStatus() == QProcess::NormalExit && capture.exitCode() == 0));
            if (replay.waitingForFragment()) {
                const QString detail = "Replay stopped before the requested clip could be saved.";
                QTextStream(stderr) << detail << Qt::endl;
                if (managed) {
                    const QJsonObject event{{"event", "clip_failed"}, {"detail", detail}};
                    QTextStream(stdout) << QJsonDocument(event).toJson(QJsonDocument::Compact) << Qt::endl;
                }
            }
            if (!success) {
                QTextStream(stderr) << "Replay capture failed. See FFmpeg diagnostics above." << Qt::endl;
            }
            QCoreApplication::exit(success && !replay.waitingForFragment() ? 0 : 1);
        }
    });
    controls.start(50);
#ifdef Q_OS_WIN
    class Keys : public QAbstractNativeEventFilter {
    public:
        Replay &replay;

        bool &stopping;
        int &activeHotkeyId;
        bool groupByApp;
        Keys(Replay &r, bool &s, int &id, bool group)
            : replay(r), stopping(s), activeHotkeyId(id), groupByApp(group) {}
        bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override {
            auto *msg = static_cast<MSG *>(message);
            if (msg->message != WM_HOTKEY) return false;
            if (msg->wParam == static_cast<WPARAM>(activeHotkeyId) && !stopping) {
                if (groupByApp) {
                    const auto app = foregroundApp();
                    const QSettings settings("Klippit", "Klippit");
                    replay.request(replayAppFolder(app, settings.value(desktopAppsKey).toStringList(),
                                                  settings.value(appFolderNamesKey).toMap()), app);
                } else {
                    replay.request();
                }
            }
            if (msg->wParam == stopHotkeyId && !stopping) {
                stopping = true;
                qInfo("Stop hotkey received.");
            }
            return true;
        }
    } keys(replay, stopping, activeHotkeyId, managed);
    QCoreApplication::instance()->installNativeEventFilter(&keys);
#endif
    const int result = QCoreApplication::exec();
#ifdef Q_OS_WIN
    QCoreApplication::instance()->removeNativeEventFilter(&keys);
#endif
    if (desktop && helper.state() != QProcess::NotRunning && !helper.waitForFinished(1000)) {
        helper.kill();
        helper.waitForFinished();
    }
    return result;
}
