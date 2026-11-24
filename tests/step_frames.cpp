#include "StepFrames.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    const QString file = dir.filePath("variable.mkv");
    // Deliberately put frames at 0, 4 and 62 ms, with a nonzero container origin.
    if (QProcess::execute("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
        "testsrc=size=128x96:rate=1000", "-vf", "negate=enable='eq(n,1)',setpts='if(eq(N,0),0,if(eq(N,1),4,62))'",
        "-frames:v", "3", "-fps_mode", "passthrough", "-c:v", "ffv1",
        "-output_ts_offset", "1", file}) != 0) return 1;
    const auto reference = dir.filePath("expected.bmp");
    if (QProcess::execute("ffmpeg", {"-v", "error", "-i", file, "-vf", "select=eq(n\\,1)",
        "-frames:v", "1", "-pix_fmt", "bgr24", reference}) != 0) return 1;
    StepFrames frames;
    QEventLoop wait;
    QObject::connect(&frames, &StepFrames::loaded, &wait, &QEventLoop::quit);
    frames.load(file, 4000, 4000);
    wait.exec();
    const QImage expected(reference);
    if (expected.isNull() || frames.image(4000) != expected || frames.image(62000).isNull()) {
        qWarning("Decoded preview image or its timestamp did not match");
        return 1;
    }
    frames.clear();
    return frames.image(4000).isNull() ? 0 : 1;
}
