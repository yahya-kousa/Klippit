#pragma once

#include <QProcess>

// Start a helper on the same executable so WASAPI capture can feed FFmpeg over
// a bounded OS pipe independently of the recorder's UI/control event loop.
bool startLoopback(QProcess &helper, QProcess &ffmpeg);
int streamLoopback();
