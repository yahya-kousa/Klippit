#pragma once

#include <QStringList>

// Capture arguments end at the encoder options; replay mode supplies the muxer.
int recordReplay(QStringList captureArgs, const QString &firstPath, bool desktop, int historySeconds, qint64 bufferBytes, bool managed, const QString &saveHotkey);
