#pragma once

#include <QString>
#include <functional>

class QWidget;
class QSettings;

struct SaveFolders {
    QString replays;
    QString trims;
};

SaveFolders saveFolders();
QString defaultReplayFolder();
std::function<void(QSettings &)> createSaveFoldersPage(QWidget &page);
