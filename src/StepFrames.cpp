#include "StepFrames.h"
#include <QtEndian>
#include <QRegularExpression>
#include <iterator>

void StepFrames::clear()
{
    frames.clear();
    bytes = 0;
    if (!decoder) return;
    decoder->disconnect(this);
    decoder->kill();
    decoder->deleteLater();
    decoder = nullptr;
}

void StepFrames::load(const QString &file, qint64 startUs, qint64 anchorUs)
{
    if (decoder) return;
    decoder = new QProcess(this);
    auto finish = [this, anchorUs](bool success) {
        auto *process = decoder;
        decoder = nullptr;
        process->deleteLater();
        if (success) {
            const auto output = process->readAllStandardOutput();
            const auto log = QString::fromUtf8(process->readAllStandardError());
            // showinfo and the passthrough image output both use decoder presentation order.
            static const QRegularExpression record(R"(\[Parsed_showinfo_\d+ @ [^\]]+\]\s+n:\s*\d+\s+pts:\s*(-?\d+))");
            auto pts = record.globalMatch(log);
            qsizetype offset = 0;
            while (output.size() - offset >= 14 && pts.hasNext()) {
                const auto *bmp = output.constData() + offset;
                const auto size = qFromLittleEndian<quint32>(bmp + 2);
                if (bmp[0] != 'B' || bmp[1] != 'M' || size < 14 || size > output.size() - offset) break;
                const auto timestamp = pts.next().captured(1).toLongLong();
                if (!frames.contains(timestamp)) {
                    auto frame = QImage::fromData(reinterpret_cast<const uchar *>(bmp), int(size), "BMP");
                    if (frame.isNull()) break;
                    while (!frames.empty() && bytes + frame.sizeInBytes() > 64 * 1024 * 1024) {
                        auto victim = anchorUs - frames.firstKey() > frames.lastKey() - anchorUs
                            ? frames.begin() : std::prev(frames.end());
                        bytes -= victim.value().sizeInBytes();
                        frames.erase(victim);
                    }
                    bytes += frame.sizeInBytes();
                    frames.insert(timestamp, std::move(frame));
                }
                offset += size;
            }
        }
        emit loaded();
    };
    connect(decoder, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus exit) {
        finish(code == 0 && exit == QProcess::NormalExit);
    });
    connect(decoder, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(false);
    });
    decoder->start("ffmpeg", {"-nostdin", "-hide_banner", "-nostats", "-copyts", "-start_at_zero",
        "-ss", QString::number(startUs / 1e6, 'f', 6), "-i", file, "-map", "0:v:0",
        "-vf", "scale=w='max(1,round(min(min(768,iw),min(432,ih)*dar)))':"
               "h='max(1,round(min(min(432,ih),min(768,iw)/dar)))',setsar=1,"
               "settb=1/1000000,showinfo=checksum=0",
        "-frames:v", QString::number(batchSize), "-fps_mode", "passthrough", "-enc_time_base:v", "filter", "-pix_fmt", "bgr24",
        "-c:v", "bmp", "-f", "image2pipe", "pipe:1"});
}
