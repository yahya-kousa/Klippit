#pragma once

#include <QString>

class QWidget;

struct SaveFolders {
    QString replays;
    QString trims;
};

SaveFolders saveFolders();
QString defaultReplayFolder();
void editSaveFolders(QWidget &parent);
