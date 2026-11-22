#pragma once

#include <QString>

// The short-lived worker writes the downloaded version to stdout (empty if current).
int prepareAppUpdate();
QString scheduleAppUpdate(const QString &expectedVersion);
