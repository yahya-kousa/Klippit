#pragma once

#include <QStringList>
#include <functional>
#include <QKeySequence>

class QWidget;
class QSettings;

QStringList replaySettingsArguments();
using SaveReplaySettings = std::function<QString(QSettings &, const QList<QKeySequence> &, QString &)>;
SaveReplaySettings createReplaySettingsPage(QWidget &page);
bool storeReplayHotkey(const QString &shortcut);
