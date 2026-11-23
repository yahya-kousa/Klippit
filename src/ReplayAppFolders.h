#pragma once

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

inline constexpr auto desktopAppsKey = "replayFolders/desktopApps";
inline constexpr auto clippedAppsKey = "replayFolders/clippedApps";

inline QString replayAppFolder(const QString &executable, const QStringList &desktopApps)
{
    QString name = QFileInfo(executable).completeBaseName();
    if (name.compare("explorer", Qt::CaseInsensitive) == 0) return "Desktop";
    static const QRegularExpression unsafe(R"([^\p{L}\p{N}_ -])",
                                          QRegularExpression::UseUnicodePropertiesOption);
    name.replace(unsafe, "_");
    name = name.trimmed();
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[0-9\u00b9\u00b2\u00b3]|LPT[0-9\u00b9\u00b2\u00b3])$"),
                                              QRegularExpression::CaseInsensitiveOption);
    if (name.isEmpty()) return "Desktop";
    if (reserved.match(name).hasMatch()) name.prepend('_');
    return desktopApps.contains(name, Qt::CaseInsensitive) ? QStringLiteral("Desktop") : name;
}
