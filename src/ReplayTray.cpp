#include "ReplayTray.h"
#include "Editor.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStyle>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
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
    menu.addAction("Quit Klippit", this, [this] {
        if (QApplication::activeModalWidget() || !this->editor.close()) return;
        quitting = true;
        if (recorder.state() == QProcess::NotRunning) QApplication::quit();
        else stop();
    });

    connect(&tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) showEditor();
    });
    connect(&recorder, &QProcess::readyReadStandardOutput, this, [this] {
        while (recorder.canReadLine()) {
            const auto event = QJsonDocument::fromJson(recorder.readLine()).object();
            const auto name = event["event"].toString();
            if (name == "replay_ready" && stopAction->isEnabled()) setStatus("Replay is buffering");
            if (name == "clip_saved") {
                const auto file = QFileInfo(event["path"].toString()).fileName();
                const auto message = event["queued"].toBool() ? file + " added to Clips to Trim."
                                                              : file + " saved. Open it manually.";
                tray.showMessage("Clip saved", message);
            } else if (name == "clip_failed" || name == "clip_rejected") {
                tray.showMessage("Could not save replay", event["detail"].toString(), QSystemTrayIcon::Warning);
            }
        }
    });
    connect(&recorder, &QProcess::readyReadStandardError, this, [this] {
        diagnostics = (diagnostics + QString::fromUtf8(recorder.readAllStandardError())).right(4000);
    });
    connect(&recorder, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        reportFailure(recorder.errorString());
        if (quitting) QApplication::quit();
    });
    connect(&recorder, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        if (code != 0 || exit != QProcess::NormalExit)
            reportFailure(QString("Recorder exited with code %1.").arg(code));
        else {
            startAction->setEnabled(true);
            stopAction->setEnabled(false);
            setStatus("Replay is off");
        }
        if (quitting) QApplication::quit();
    });

    setStatus("Replay is off");
#ifdef Q_OS_WIN
    recorder.setCreateProcessArgumentsModifier([](auto *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    tray.setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
    tray.setContextMenu(&menu);
    tray.show();
}

void ReplayTray::start()
{
    if (recorder.state() != QProcess::NotRunning || quitting) return;
    diagnostics.clear();
    details->setVisible(false);
    const auto executable = QDir(QCoreApplication::applicationDirPath()).filePath("klippit-record.exe");
    if (!QFileInfo(executable).isFile()) {
        reportFailure("klippit-record.exe must be next to klippit.exe.");
        return;
    }
    const auto movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const auto folder = QDir(movies.isEmpty() ? QDir::homePath() : movies).filePath("Klippit");
    if (!QDir().mkpath(folder)) {
        reportFailure("Could not create the clip folder " + folder);
        return;
    }

    const auto stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss");
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const auto output = QDir(folder).filePath("Replay-" + stamp + "-" + id + ".mkv");
    startAction->setEnabled(false);
    stopAction->setEnabled(true);
    setStatus("Starting replay…");
    recorder.start(executable, {"--replay", "--managed", "--desktop-audio", output});
}

void ReplayTray::stop()
{
    stopAction->setEnabled(false);
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
    diagnostics = message + "\n\n" + diagnostics.trimmed();
    details->setVisible(true);
    startAction->setEnabled(true);
    stopAction->setEnabled(false);
    setStatus("Replay failed");
    tray.showMessage("Replay failed", message, QSystemTrayIcon::Warning);
}
