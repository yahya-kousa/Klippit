#pragma once

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

inline constexpr auto desktopAppsKey = "replayFolders/desktopApps";
inline constexpr auto clippedAppsKey = "replayFolders/clippedApps";

inline constexpr auto appFolderNamesKey = "replayFolders/customNames";

inline QString safeReplayFolderName(QString name)
{
    static const QRegularExpression unsafe(R"([^\p{L}\p{N}_ -])",
                                          QRegularExpression::UseUnicodePropertiesOption);
    name.replace(unsafe, "_");
    name = name.trimmed();
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[0-9\u00b9\u00b2\u00b3]|LPT[0-9\u00b9\u00b2\u00b3])$"),
                                              QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(name).hasMatch()) name.prepend('_');
    return name;
}

inline QString replayAppName(const QString &executable)
{
    auto name = QFileInfo(executable).completeBaseName();
    if (name.compare("explorer", Qt::CaseInsensitive) == 0) return "Desktop";
    name = safeReplayFolderName(name);
    return name.isEmpty() ? QStringLiteral("Desktop") : name;
}

inline QString replayAppFolder(const QString &app, const QStringList &desktopApps,
                               const QVariantMap &customNames = {})
{
    if (app.compare("Desktop", Qt::CaseInsensitive) == 0 ||
        desktopApps.contains(app, Qt::CaseInsensitive)) return "Desktop";
    const auto custom = safeReplayFolderName(customNames.value(app.toCaseFolded()).toString());
    return custom.isEmpty() ? app : custom;
}
