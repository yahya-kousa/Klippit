#include "ReplayTray.h"
#include "Editor.h"
#include "ReplaySettings.h"
#include "SaveFolders.h"
#ifdef KLIPPIT_PACKAGED
#include "AppUpdate.h"
#endif

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QUuid>
#include <iterator>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wtsapi32.h>
#endif

ReplayTray::ReplayTray(Editor &editor) : editor(editor)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    QApplication::setQuitOnLastWindowClosed(false);

    status = menu.addAction(QString());
    status->setEnabled(false);
    startAction = menu.addAction("Start Replay", this, &ReplayTray::start);
    stopAction = menu.addAction("Stop Replay", this, &ReplayTray::stop);
    stopAction->setEnabled(false);
    menu.addSeparator();
    menu.addAction("Open Klippit", this, &ReplayTray::showEditor);
    menu.addAction("Clips to Trim", this, [this] {
        showEditor();
        if (!quitting && !QApplication::activeModalWidget()) this->editor.showClipQueue();
    });
    details = menu.addAction("Recorder Error…", this, [this] {
        showEditor();
        if (!quitting && !QApplication::activeModalWidget())
            QMessageBox::warning(&this->editor, "Recorder Error", diagnostics);
    });
    details->setVisible(false);
    menu.addSeparator();
    settingsAction = menu.addAction("Replay Settings…", this, [this] {
        showEditor();
        if (quitting || QApplication::activeModalWidget()) return;
        if (const auto shortcut = editReplaySettings(this->editor)) updateHotkey(*shortcut);
    });
    menu.addAction("Save folders…", this, [this] {
        if (quitting || QApplication::activeModalWidget()) return;
        showEditor();
        editSaveFolders(editor);
    });
    hotkeyTimeout.setSingleShot(true);
    connect(&hotkeyTimeout, &QTimer::timeout, this, [this] {
        finishHotkeyUpdate("The recorder did not confirm the new shortcut.");
    });
    recoveryTimer.setSingleShot(true);
    connect(&recoveryTimer, &QTimer::timeout, this, [this] {
        if (sessionLocked || !recovering || quitting) return;
        ++recoveryAttempts;
        start();
    });
    configureStartup();
#ifdef KLIPPIT_PACKAGED
    configureUpdates();
#endif
    menu.addAction("Quit Klippit", this, [this] {
        if (quitting || QApplication::activeModalWidget() || !this->editor.close()) return;
        quitting = true;
        stop();
        if (recorder.state() == QProcess::NotRunning) QApplication::quit();
    });

    connect(&tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) showEditor();
    });
    connect(&recorder, &QProcess::readyReadStandardOutput, this, [this] {
        while (recorder.canReadLine()) {
            const auto event = QJsonDocument::fromJson(recorder.readLine()).object();
            const auto name = event["event"].toString();
            if (name == "replay_ready") {
                bufferReady = true;
                recovering = false;
                recoveryAttempts = 0;
            }
            if ((name == "replay_ready" || name == "save_hotkey_changed") && bufferReady && stopAction->isEnabled())
                setStatus("Replay is buffering · Save with " + event["save_hotkey"].toString());
            if (!pendingHotkey.isEmpty() && event["requested"].toString() == pendingHotkey) {
                if (name == "save_hotkey_changed") {
                    const auto shortcut = pendingHotkey;
                    const bool stored = storeReplayHotkey(pendingHotkey);
                    finishHotkeyUpdate(stored ? QString() : "The live shortcut changed, but could not be saved for next time.");
                    if (stored) tray.showMessage("Save hotkey updated", "Press " + shortcut + " to save a replay.");
                } else if (name == "save_hotkey_rejected") {
                    finishHotkeyUpdate(event["detail"].toString());
                }
            }
            if (name == "clip_saved") {
                const auto file = QFileInfo(event["path"].toString()).fileName();
                const auto message = event["queued"].toBool() ? file + " added to Clips to Trim."
                                                              : file + " saved. Open it manually.";
                tray.showMessage("Clip saved", message);
            } else if (name == "clip_failed" || name == "clip_rejected") {
#ifdef KLIPPIT_PACKAGED
                if (restarting) completeRestart(false);
#endif
                tray.showMessage("Could not save replay", event["detail"].toString(), QSystemTrayIcon::Warning);
            }
        }
    });
    const auto readDiagnostics = [this] {
        diagnostics += QString::fromUtf8(recorder.readAllStandardError());
        // ddagrab reports the HRESULT when acquiring a frame, but omits it
        // when releasing one. Either call can fail when the desktop switches.
        displayCaptureInterrupted |= diagnostics.contains("887a0026", Qt::CaseInsensitive) ||
                                     diagnostics.contains("DDA ReleaseFrame failed", Qt::CaseInsensitive);
        diagnostics = diagnostics.right(4000);
    };
    connect(&recorder, &QProcess::readyReadStandardError, this, readDiagnostics);
    connect(&recorder, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        finishHotkeyUpdate("Replay could not start.");
        reportFailure(recorder.errorString());
#ifdef KLIPPIT_PACKAGED
        if (restarting) { completeRestart(false); return; }
#endif
        if (quitting) QApplication::quit();
    });
    connect(&recorder, &QProcess::finished, this, [this, readDiagnostics](int code, QProcess::ExitStatus exit) {
        readDiagnostics();
        finishHotkeyUpdate("Replay ended before confirming the new shortcut.");
        if (code != 0 || exit != QProcess::NormalExit) {
            if (exit == QProcess::NormalExit && !quitting && stopAction->isEnabled() &&
                (displayCaptureInterrupted || sessionLocked || recovering)) {
                scheduleRecovery();
                return;
            }
            reportFailure(QString("Recorder exited with code %1.").arg(code));
        } else {
            recovering = false;
            recoveryAttempts = 0;
            startAction->setEnabled(true);
            stopAction->setEnabled(false);
            setStatus("Replay is off");
        }
#ifdef KLIPPIT_PACKAGED
        if (restarting) { completeRestart(code == 0 && exit == QProcess::NormalExit); return; }
#endif
        if (quitting) QApplication::quit();
    });

    setStatus("Replay is off");
#ifdef Q_OS_WIN
    const auto window = reinterpret_cast<HWND>(editor.winId());
    if (WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION)) {
        sessionWindow = reinterpret_cast<quintptr>(window);
        QApplication::instance()->installNativeEventFilter(this);
    }
    recorder.setCreateProcessArgumentsModifier([](auto *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    tray.setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
    tray.setContextMenu(&menu);
    tray.show();
}

ReplayTray::~ReplayTray()
{
#ifdef Q_OS_WIN
    if (sessionWindow) {
        QApplication::instance()->removeNativeEventFilter(this);
        WTSUnRegisterSessionNotification(reinterpret_cast<HWND>(sessionWindow));
    }
#endif
}

bool ReplayTray::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
#ifdef Q_OS_WIN
    if (eventType != "windows_generic_MSG") return false;
    const auto *event = static_cast<const MSG *>(message);
    if (event->message != WM_WTSSESSION_CHANGE ||
        reinterpret_cast<quintptr>(event->hwnd) != sessionWindow) return false;

    if (event->wParam == WTS_SESSION_LOCK) {
        sessionLocked = true;
        if (recovering) scheduleRecovery();
    } else if (event->wParam == WTS_SESSION_UNLOCK) {
        sessionLocked = false;
        if (recovering && recorder.state() == QProcess::NotRunning) {
            recoveryAttempts = 0;
            scheduleRecovery();
        }
    }
#endif
    return false;
}

void ReplayTray::scheduleRecovery()
{
    constexpr int retryDelaysMs[] = {1000, 2000, 4000, 8000, 16000};
    recovering = true;
    recoveryTimer.stop();
    if (sessionLocked) {
        setStatus("Replay interrupted · Waiting for Windows unlock");
        return;
    }
    if (recoveryAttempts >= int(std::size(retryDelaysMs))) {
        reportFailure("Could not reconnect replay capture. Try starting replay again.");
        return;
    }
    setStatus("Replay interrupted · Reconnecting…");
    recoveryTimer.start(retryDelaysMs[recoveryAttempts]);
}

void ReplayTray::updateHotkey(const QString &shortcut)
{
    if (recorder.state() == QProcess::NotRunning || !stopAction->isEnabled()) {
        if (!storeReplayHotkey(shortcut))
            tray.showMessage("Save hotkey", "Could not save the shortcut.", QSystemTrayIcon::Warning);
        return;
    }
    pendingHotkey = shortcut;
    settingsAction->setEnabled(false);
    if (recorder.write("hotkey " + shortcut.toUtf8() + '\n') < 0) {
        finishHotkeyUpdate("Could not send the shortcut to the recorder.");
        return;
    }
    hotkeyTimeout.start(5000);
}

void ReplayTray::finishHotkeyUpdate(const QString &message)
{
    if (pendingHotkey.isEmpty()) return;
    pendingHotkey.clear();
    hotkeyTimeout.stop();
    settingsAction->setEnabled(true);
    if (!message.isEmpty()) {
        diagnostics = (diagnostics + '\n' + message).right(4000);
        details->setVisible(true);
        tray.showMessage("Save hotkey", message, QSystemTrayIcon::Warning);
    }
}

#ifdef KLIPPIT_PACKAGED
void ReplayTray::configureUpdates()
{
    updateAction = menu.addAction("Check for updates…", this, [this] {
        if (quitting || QApplication::activeModalWidget()) return;
        if (updateVersion.isEmpty()) {
            checkForUpdates(true);
            return;
        }
        const auto answer = QMessageBox::question(&editor, "Restart and update",
            QString("Restart Klippit to install version %1? Unsaved replay history and trim marks "
                    "will be lost. Requested saves will finish first.").arg(updateVersion),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes || !editor.close()) return;
        quitting = restarting = true;
        updateAction->setEnabled(false);
        stop();
        if (recorder.state() == QProcess::NotRunning) completeRestart(true);
    });
    auto checkFailed = [this](const QString &reason) {
        if (quitting) return;
        refreshUpdateAction();
        if (!backgroundUpdateCheck)
            tray.showMessage("Update check failed", reason, QSystemTrayIcon::Warning);
    };
    connect(&updateWorker, &QProcess::errorOccurred, this, [this, checkFailed](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) checkFailed(updateWorker.errorString());
    });
    connect(&updateWorker, &QProcess::finished, this, [this, checkFailed](int code, QProcess::ExitStatus status) {
        const QString version = QString::fromUtf8(updateWorker.readAllStandardOutput()).trimmed();
        const QString diagnostic = QString::fromUtf8(updateWorker.readAllStandardError()).trimmed();
        if (quitting) return;
        if (status != QProcess::NormalExit || code != 0) {
            checkFailed(diagnostic.isEmpty() ? "Update worker exited unexpectedly." : diagnostic);
            return;
        }
        const bool newVersion = version != availableUpdateVersion;
        availableUpdateVersion = version;
        if (!backgroundUpdateCheck) updateVersion = version;
        refreshUpdateAction();
        if (backgroundUpdateCheck) {
            if (newVersion && !version.isEmpty()) {
                tray.showMessage("Klippit update available",
                    QString("Version %1 is available. Open the tray menu to download it.").arg(version));
            }
            return;
        }
        tray.showMessage("Klippit updates", version.isEmpty() ? QStringLiteral("You're up to date.")
            : QString("Version %1 is ready. Restart from the tray when convenient.").arg(version));
    });
    QTimer::singleShot(10000, this, [this] { checkForUpdates(false); });
    connect(&updateCheckTimer, &QTimer::timeout, this, [this] { checkForUpdates(false); });
    updateCheckTimer.start(6 * 60 * 60 * 1000);
}

void ReplayTray::checkForUpdates(bool download)
{
    if (quitting || !updateVersion.isEmpty() || updateWorker.state() != QProcess::NotRunning) return;
    backgroundUpdateCheck = !download;
    updateAction->setEnabled(false);
    updateAction->setText("Checking for updates…");
    updateWorker.start(QCoreApplication::applicationFilePath(),
                       {download ? "--prepare-update" : "--check-update"});
}

void ReplayTray::refreshUpdateAction()
{
    if (!updateVersion.isEmpty())
        updateAction->setText(QString("Restart and update to %1…").arg(updateVersion));
    else if (!availableUpdateVersion.isEmpty())
        updateAction->setText(QString("Download update %1…").arg(availableUpdateVersion));
    else
        updateAction->setText("Check for updates…");
    updateAction->setEnabled(true);
}

void ReplayTray::completeRestart(bool recorderStoppedCleanly)
{
    const QString error = recorderStoppedCleanly ? scheduleAppUpdate(updateVersion)
        : QStringLiteral("Replay did not finish cleanly. Check the saved clips before retrying.");
    if (error.isEmpty()) {
        QApplication::quit();
        return;
    }
    restarting = quitting = false;
    updateVersion.clear();
    refreshUpdateAction();
    showEditor();
    tray.showMessage("Update postponed", error, QSystemTrayIcon::Warning);
}
#endif

void ReplayTray::configureStartup()
{
    static constexpr auto runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static constexpr auto autoReplayKey = "replay/automatic";
    auto *launch = menu.addAction("Launch at Windows sign-in");
    launch->setCheckable(true);
    {
        const QSettings startup(runKey, QSettings::NativeFormat);
        launch->setChecked(startup.contains("Klippit"));
    }
    connect(launch, &QAction::triggered, this, [this, launch](bool enabled) {
        QSettings startup(runKey, QSettings::NativeFormat);
        if (enabled) {
            const auto executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
            startup.setValue("Klippit", QString("\"%1\" --sign-in").arg(executable));
        } else {
            startup.remove("Klippit");
        }
        startup.sync();
        if (startup.status() != QSettings::NoError) {
            launch->setChecked(!enabled);
            tray.showMessage("Klippit", "Could not update Windows sign-in settings.", QSystemTrayIcon::Warning);
        }
    });

    auto *automatic = menu.addAction("Start replay automatically");
    automatic->setCheckable(true);
    const QSettings settings("Klippit", "Klippit");
    automatic->setChecked(settings.value(autoReplayKey, false).toBool());
    connect(automatic, &QAction::triggered, this, [this, automatic](bool enabled) {
        QSettings settings("Klippit", "Klippit");
        settings.setValue(autoReplayKey, enabled);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            automatic->setChecked(!enabled);
            tray.showMessage("Klippit", "Could not save the automatic replay setting.", QSystemTrayIcon::Warning);
        }
    });
    if (automatic->isChecked()) QTimer::singleShot(0, this, &ReplayTray::start);
}

void ReplayTray::start()
{
    if (recorder.state() != QProcess::NotRunning || quitting) return;
    bufferReady = false;
    displayCaptureInterrupted = false;
    diagnostics.clear();
    details->setVisible(false);
    const auto executable = QDir(QCoreApplication::applicationDirPath()).filePath("klippit-record.exe");
    if (!QFileInfo(executable).isFile()) {
        reportFailure("klippit-record.exe must be next to klippit.exe.");
        return;
    }
    const auto preferred = saveFolders().replays;
    const auto folder = preferred.isEmpty() ? defaultReplayFolder() : preferred;
    if (preferred.isEmpty() ? !QDir().mkpath(folder) : !QFileInfo(folder).isDir()) {
        reportFailure(QString("The replay folder is unavailable. Choose another in Save folders.\n%1")
                          .arg(QDir::toNativeSeparators(folder)));
        return;
    }

    const auto stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss");
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const auto output = QDir(folder).filePath("Replay-" + stamp + "-" + id + ".mkv");
    startAction->setEnabled(false);
    stopAction->setEnabled(true);
    setStatus(recovering ? "Replay interrupted · Reconnecting…" : "Starting replay…");
    recorder.start(executable, QStringList{"--replay", "--managed"} + replaySettingsArguments() + QStringList{output});
}

void ReplayTray::stop()
{
    recoveryTimer.stop();
    recovering = false;
    recoveryAttempts = 0;
    stopAction->setEnabled(false);
    if (recorder.state() == QProcess::NotRunning) {
        startAction->setEnabled(true);
        setStatus("Replay is off");
        return;
    }
    setStatus("Stopping replay…");
    // Closing stdin asks the recorder to drain accepted saves before exiting.
    recorder.closeWriteChannel();
}

void ReplayTray::showEditor()
{
    if (!quitting) editor.showAndActivate();
}

void ReplayTray::setStatus(const QString &message)
{
    status->setText(message);
    tray.setToolTip("Klippit · " + message);
}

void ReplayTray::reportFailure(const QString &message)
{
    recoveryTimer.stop();
    recovering = false;
    recoveryAttempts = 0;
    diagnostics = message + "\n\n" + diagnostics.trimmed();
    details->setVisible(true);
    startAction->setEnabled(true);
    stopAction->setEnabled(false);
    setStatus("Replay failed");
    tray.showMessage("Replay failed", message, QSystemTrayIcon::Warning);
}
