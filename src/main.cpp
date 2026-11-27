#include "Editor.h"
#include "AppStyle.h"
#include <QApplication>
#include <QIcon>
#include <QTimer>
#include <QWindow>
#include <cstdio>
#include <iterator>
#ifdef Q_OS_WIN
#include "ReplayTray.h"
#include <QAbstractNativeEventFilter>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QScopeGuard>
#include <windows.h>
#ifdef KLIPPIT_PACKAGED
#include <QSettings>
#include <shlobj.h>
#endif

namespace {
constexpr wchar_t editorProperty[] = L"Klippit.Editor.OpenFile";
constexpr ULONG_PTR openFileMessage = 1;
constexpr qsizetype maxPathLength = 32767;

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

bool forwardFile(const QString &path)
{
    if (path.size() > maxPathLength) return false;
    HWND target = nullptr;
    for (int attempt = 0; attempt < 40 && !target; ++attempt) {
        EnumWindows([](HWND window, LPARAM result) -> BOOL {
            if (!GetPropW(window, editorProperty)) return TRUE;
            *reinterpret_cast<HWND *>(result) = window;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&target));
        if (!target) QThread::msleep(50);
    }
    if (!target) return false;
    DWORD editorProcess = 0;
    GetWindowThreadProcessId(target, &editorProcess);
    if (editorProcess) AllowSetForegroundWindow(editorProcess);
    COPYDATASTRUCT data{openFileMessage, DWORD(path.size() * sizeof(char16_t)),
                        const_cast<void *>(static_cast<const void *>(path.utf16()))};
    DWORD_PTR accepted = 0;
    return SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &accepted) && accepted;
}

class ReopenEditor : public QAbstractNativeEventFilter {
public:
    ReopenEditor(Editor &editor, UINT message)
        : editor(editor), message(message), window(reinterpret_cast<HWND>(editor.winId()))
    {
        SetPropW(window, editorProperty, reinterpret_cast<HANDLE>(1));
    }

    ~ReopenEditor() override { RemovePropW(window, editorProperty); }

    bool nativeEventFilter(const QByteArray &, void *event, qintptr *result) override
    {
        const auto *msg = static_cast<const MSG *>(event);
        if (msg->hwnd != window) return false;
        if (msg->message == WM_COPYDATA && msg->lParam) {
            const auto *data = reinterpret_cast<const COPYDATASTRUCT *>(msg->lParam);
            if (data->dwData != openFileMessage || !data->lpData || !data->cbData ||
                data->cbData % sizeof(char16_t) || data->cbData > maxPathLength * sizeof(char16_t))
                return false;
            const auto path = QString::fromUtf16(static_cast<const char16_t *>(data->lpData),
                                                 data->cbData / sizeof(char16_t));
            if (path.contains(QChar(u'\0'))) return false;
            QTimer::singleShot(0, &editor, [this, path] { editor.openExternalFile(path); });
            if (result) *result = TRUE;
            return true;
        }
        if (msg->message != message) return false;
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
        if (!file.isEmpty()) {
            if (forwardFile(file)) return 0;
            MessageBoxW(nullptr, L"Could not send the video to the running Klippit window.",
                        L"Klippit", MB_OK | MB_ICONWARNING);
            return 1;
        }
        return PostMessageW(HWND_BROADCAST, reopenMessage, 0, 0) ? 0 : 1;
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
    ReopenEditor reopen(editor, reopenMessage);
    app.installNativeEventFilter(&reopen);
    const auto removeFilter = qScopeGuard([&] { app.removeNativeEventFilter(&reopen); });
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
