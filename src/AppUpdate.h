#pragma once

#include <QString>

// The worker writes the available version to stdout (empty if current).
int checkAppUpdate(bool download);
QString scheduleAppUpdate(const QString &expectedVersion);
