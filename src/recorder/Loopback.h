#pragma once

#include <QProcess>

// With desktop audio enabled, connect the pipe first and start the producer
// only after FFmpeg has opened the desktop video input.
bool startCapture(QProcess &ffmpeg, const QStringList &args, QProcess *loopback);
int streamLoopback();
