#include "StepFrames.h"
#include <QRegularExpression>
#include <iterator>

bool StepFrames::setPreviewSize(QSize size)
{
    if (size.isEmpty() || size == previewSize) return false;
    clear();
    previewSize = size;
    return true;
}

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
            // FFmpeg's multipart JPEG muxer writes a byte count before each image.
            while (pts.hasNext()) {
                const auto endHeader = output.indexOf("\r\n\r\n", offset);
                if (endHeader < 0) break;
                const QByteArrayView header(output.constData() + offset, endHeader - offset);
                constexpr QByteArrayView field("Content-length: ");
                const auto lengthPos = header.indexOf(field);
                if (lengthPos < 0) break;
                const auto size = header.sliced(lengthPos + field.size()).toLongLong();
                offset = endHeader + 4;
                if (size <= 0 || size > output.size() - offset) break;
                const auto timestamp = pts.next().captured(1).toLongLong();
                if (!frames.contains(timestamp)) {
                    while (!frames.empty() && bytes + size > 64 * 1024 * 1024) {
                        auto victim = anchorUs - frames.firstKey() > frames.lastKey() - anchorUs
                            ? frames.begin() : std::prev(frames.end());
                        bytes -= victim.value().size();
                        frames.erase(victim);
                    }
                    bytes += size;
                    frames.insert(timestamp, output.mid(offset, size));
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
    const auto filter = QStringLiteral(
        "scale=w='max(1,round(min(min(%1,iw),min(%2,ih)*dar)))':"
        "h='max(1,round(min(min(%2,ih),min(%1,iw)/dar)))',setsar=1,"
        "settb=1/1000000,showinfo=checksum=0").arg(previewSize.width()).arg(previewSize.height());
    decoder->start("ffmpeg", {"-nostdin", "-hide_banner", "-nostats", "-copyts", "-start_at_zero",
        "-ss", QString::number(startUs / 1e6, 'f', 6), "-i", file, "-map", "0:v:0",
        "-vf", filter,
        "-frames:v", QString::number(batchSize), "-fps_mode", "passthrough", "-enc_time_base:v", "filter", "-pix_fmt", "yuvj444p",
        "-c:v", "mjpeg", "-q:v", "2", "-f", "mpjpeg", "pipe:1"});
}
