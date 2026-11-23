#pragma once

#include <QAbstractNativeEventFilter>
#include <QMenu>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QTimer>

class Editor;

class ReplayTray : public QObject, private QAbstractNativeEventFilter {
public:
    explicit ReplayTray(Editor &editor);
    ~ReplayTray() override;
    bool isAvailable() const { return tray.isVisible(); }

private:
#ifdef KLIPPIT_PACKAGED
    void configureUpdates();
    void completeRestart(bool recorderStoppedCleanly);
    QProcess updateWorker;
    QAction *updateAction = nullptr;
    QString updateVersion;
    bool restarting = false;
#endif
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override;
    void scheduleRecovery();
    void configureStartup();
    void start();
    void stop();
    void showEditor();
    void setStatus(const QString &message);
    void reportFailure(const QString &message);
    void updateHotkey(const QString &shortcut);
    void finishHotkeyUpdate(const QString &message = {});

    Editor &editor;
    QProcess recorder;
    QMenu menu;
    QSystemTrayIcon tray;
    QAction *status;
    QAction *startAction;
    QAction *stopAction;
    QAction *details;
    QAction *settingsAction;
    QTimer hotkeyTimeout;
    QTimer recoveryTimer;
    quintptr sessionWindow = 0;
    int recoveryAttempts = 0;
    bool sessionLocked = false;
    bool recovering = false;
    bool captureAccessLost = false;
    QString pendingHotkey;
    QString diagnostics;
    bool bufferReady = false;
    bool quitting = false;
};
