#include "Editor.h"
#include <QApplication>
#include <QTimer>
#include <QWindow>
#include <cstdio>
#ifdef Q_OS_WIN
#include "ReplayTray.h"
#include <QAbstractNativeEventFilter>
#include <QDir>
#include <QScopeGuard>
#include <windows.h>

namespace {
class ReopenEditor : public QAbstractNativeEventFilter {
public:
    ReopenEditor(Editor &editor, UINT message)
        : editor(editor), message(message), window(reinterpret_cast<HWND>(editor.winId())) {}

    bool nativeEventFilter(const QByteArray &, void *event, qintptr *result) override
    {
        const auto *msg = static_cast<const MSG *>(event);
        if (msg->message != message || msg->hwnd != window) return false;
        editor.showAndActivate();
        if (result) *result = 0;
        return true;
    }

private:
    Editor &editor;
    UINT message;
    HWND window;
};
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
    Velopack::VelopackApp::Build().SetAutoApplyOnStartup(false).Run();
    if (argc == 2) {
        const QByteArrayView argument(argv[1]);
        if (argument == "--check-update") return checkAppUpdate(false);
        if (argument == "--prepare-update") return checkAppUpdate(true);
    }
#endif
    bool signIn = false;
    for (int i = 1; i < argc; ++i)
        signIn |= QByteArrayView(argv[i]) == "--sign-in";

    const UINT reopenMessage = RegisterWindowMessageW(L"Klippit.ReopenEditor");
    // Creating the named mutex is atomic, including when two launches race.
    const HANDLE instance = CreateMutexW(nullptr, FALSE, L"Local\\Klippit.Editor");
    const DWORD instanceStatus = GetLastError();
    const auto release = qScopeGuard([instance] { if (instance) CloseHandle(instance); });
    if (!reopenMessage || !instance) {
        if (smoke) return 1;
        MessageBoxW(nullptr, L"Could not initialize Klippit's window.", L"Klippit", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (instanceStatus == ERROR_ALREADY_EXISTS) {
        if (smoke) return 1;
        if (signIn) return 0;
        return PostMessageW(HWND_BROADCAST, reopenMessage, 0, 0) ? 0 : 1;
    }
#endif

    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    // A deployed copy can keep its FFmpeg tools beside the executables.
    const auto appFolder = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const QString path = appFolder + ';' + qEnvironmentVariable("PATH");
    SetEnvironmentVariableW(L"PATH", reinterpret_cast<LPCWSTR>(path.utf16()));
#endif
    if (smoke) std::fputs("Qt application initialized\n", stderr);
    Editor editor;
#ifdef Q_OS_WIN
    ReopenEditor reopen(editor, reopenMessage);
    app.installNativeEventFilter(&reopen);
    const auto removeFilter = qScopeGuard([&] { app.removeNativeEventFilter(&reopen); });
    ReplayTray replayTray(editor);
    if (!signIn || !replayTray.isAvailable()) editor.show();
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
