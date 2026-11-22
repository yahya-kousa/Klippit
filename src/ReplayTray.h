#pragma once

#include <QMenu>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QTimer>

class Editor;

class ReplayTray : public QObject {
public:
    explicit ReplayTray(Editor &editor);
    bool isAvailable() const { return tray.isVisible(); }

private:
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
    QString pendingHotkey;
    QString diagnostics;
    bool bufferReady = false;
    bool quitting = false;
};
