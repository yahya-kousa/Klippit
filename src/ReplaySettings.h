#pragma once

#include <QStringList>
#include <optional>

class QWidget;

QStringList replaySettingsArguments();
std::optional<QString> editReplaySettings(QWidget &parent);
bool storeReplayHotkey(const QString &shortcut);
