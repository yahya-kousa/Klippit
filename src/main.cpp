#include "Editor.h"
#include "AppStyle.h"
#include <QApplication>
#include <QIcon>
#include <QTimer>
#include <QWindow>
#include <cstdio>
#ifdef Q_OS_WIN
#include "ReplayTray.h"
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QElapsedTimer>
#include <iterator>
#include <QThread>
#include <QScopeGuard>
#include <windows.h>
#ifdef KLIPPIT_PACKAGED
#include <QSettings>
#include <shlobj.h>
#endif

namespace {
constexpr qint64 maxLaunchBytes = 128 * 1024;
constexpr int launchTimeoutMs = 3000;

QString launchEndpoint()
{
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    return QStringLiteral("Klippit.Launch.%1").arg(session);
}

#ifdef KLIPPIT_PACKAGED
constexpr auto openWithKey = "HKEY_CURRENT_USER\\Software\\Classes\\Applications\\klippit.exe";

void registerOpenWith(void *, const char *)
{
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
    if (!length || length >= std::size(path)) return;
    const QString executable = QString::fromWCharArray(path, length);
    QSettings registration(openWithKey, QSettings::NativeFormat);
    registration.setValue("FriendlyAppName", "Klippit");
    registration.setValue("shell/open/command/Default", '"' + executable + "\" \"%1\"");
    for (const auto type : {".mp4", ".mov", ".mkv"})
        registration.setValue(QString("SupportedTypes/") + type, QStringLiteral(""));
    registration.sync();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void unregisterOpenWith(void *, const char *)
{
    QSettings registration(openWithKey, QSettings::NativeFormat);
    registration.remove("");
    registration.sync();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
#endif

bool forwardLaunch(const QString &path)
{
    const QByteArray request = path.toUtf8() + '\n';
    if (request.size() > maxLaunchBytes) return false;

    QLocalSocket socket;
    QElapsedTimer elapsed;
    elapsed.start();
    do {
        socket.connectToServer(launchEndpoint());
        if (socket.waitForConnected(100)) break;
        socket.abort();
        QThread::msleep(50);
    } while (elapsed.elapsed() < launchTimeoutMs);
    if (socket.state() != QLocalSocket::ConnectedState) return false;

    // The launcher can transfer Explorer's foreground permission to the editor.
    ULONG serverProcess = 0;
    if (GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(socket.socketDescriptor()), &serverProcess))
        AllowSetForegroundWindow(serverProcess);

    if (socket.write(request) != request.size()) return false;
    while (socket.bytesToWrite() && socket.waitForBytesWritten(launchTimeoutMs)) {}
    if (socket.bytesToWrite()) return false;
    if (!socket.bytesAvailable() && !socket.waitForReadyRead(launchTimeoutMs)) return false;
    return socket.read(1) == "1";
}

void receiveLaunch(QLocalSocket &socket, Editor &editor)
{
    if (!socket.canReadLine()) {
        if (socket.bytesAvailable() >= maxLaunchBytes) socket.abort();
        return;
    }

    const QByteArray request = socket.readLine(maxLaunchBytes + 1);
    if (!request.endsWith('\n') || request.contains('\0')) {
        socket.abort();
        return;
    }
    const QString path = QString::fromUtf8(request.chopped(1));
    socket.write("1", 1);
    socket.disconnectFromServer();

    QTimer::singleShot(0, &editor, [&editor, path] {
        if (path.isEmpty()) editor.showAndActivate();
        else editor.openExternalFile(path);
    });
}

bool listenForLaunches(QLocalServer &server, Editor &editor)
{
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QObject::connect(&server, &QLocalServer::newConnection, &editor, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            socket->setReadBufferSize(maxLaunchBytes);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(launchTimeoutMs, socket, [socket] { socket->abort(); });
            QObject::connect(socket, &QLocalSocket::readyRead, &editor, [socket, &editor] {
                receiveLaunch(*socket, editor);
            });
            receiveLaunch(*socket, editor);
        }
    });
    return server.listen(launchEndpoint());
}
}
#endif

#ifdef KLIPPIT_PACKAGED
#include "AppUpdate.h"
#include <Velopack.hpp>
#endif

int main(int argc, char **argv)
{
    const bool smoke = argc == 2 && QByteArrayView(argv[1]) == "--smoke-test-editor";
    if (smoke) std::fputs("Starting editor smoke test\n", stderr);
#ifdef Q_OS_WIN
#ifdef KLIPPIT_PACKAGED
    // Installer hooks must run before the single-instance check.
    Velopack::VelopackApp::Build().SetAutoApplyOnStartup(false)
        .OnAfterInstall(registerOpenWith).OnAfterUpdate(registerOpenWith)
        .OnBeforeUninstall(unregisterOpenWith).Run();
    if (argc == 2) {
        const QByteArrayView argument(argv[1]);
        if (argument == "--check-update") return checkAppUpdate(false);
        if (argument == "--prepare-update") return checkAppUpdate(true);
    }
#endif
#endif
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto arguments = app.arguments();
    QString file;
    if (arguments.size() == 2 && !arguments[1].startsWith('-')) {
        const QFileInfo input(arguments[1]);
        if (input.isFile()) file = input.absoluteFilePath();
    }
    bool signIn = false;
    for (int i = 1; i < argc; ++i)
        signIn |= QByteArrayView(argv[i]) == "--sign-in";

    // Creating the named mutex is atomic, including when two launches race.
    const HANDLE instance = CreateMutexW(nullptr, FALSE, L"Local\\Klippit.Editor");
    const DWORD instanceStatus = GetLastError();
    const auto release = qScopeGuard([instance] { if (instance) CloseHandle(instance); });
    if (!instance) {
        if (smoke) return 1;
        MessageBoxW(nullptr, L"Could not initialize Klippit's window.", L"Klippit", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (instanceStatus == ERROR_ALREADY_EXISTS) {
        if (smoke) return 1;
        if (signIn) return 0;
        if (forwardLaunch(file)) return 0;
        MessageBoxW(nullptr, L"Could not reach the running Klippit instance. Quit Klippit from the tray and try again.",
                    L"Klippit", MB_OK | MB_ICONWARNING);
        return 1;
    }
#endif

    app.setWindowIcon(QIcon(":/appearance/frog.png"));
    applyAppStyle(app);
#ifdef Q_OS_WIN
    // A deployed copy can keep its FFmpeg tools beside the executables.
    const auto appFolder = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const QString path = appFolder + ';' + qEnvironmentVariable("PATH");
    SetEnvironmentVariableW(L"PATH", reinterpret_cast<LPCWSTR>(path.utf16()));
#endif
    if (smoke) std::fputs("Qt application initialized\n", stderr);
    Editor editor;
#ifdef Q_OS_WIN
    QLocalServer launchServer;
    if (!listenForLaunches(launchServer, editor)) {
        if (!smoke) MessageBoxW(nullptr, L"Could not initialize Klippit's launch receiver.",
                                L"Klippit", MB_OK | MB_ICONERROR);
        return 1;
    }
    ReplayTray replayTray(editor);
    if (!signIn || !replayTray.isAvailable()) editor.show();
    if (!file.isEmpty()) editor.openExternalFile(file);
#else
    editor.show();
#endif
    if (smoke) {
        QTimer::singleShot(0, &editor, [&editor] {
            const auto *window = editor.windowHandle();
            if (!editor.isVisible() || !window || !window->isVisible() || !window->winId()) {
                std::fputs("Editor window was not created and shown\n", stderr);
                QCoreApplication::exit(1);
                return;
            }
            std::fputs("Editor window ready\n", stderr);
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}
