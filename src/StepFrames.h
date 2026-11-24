#pragma once
#include <QImage>
#include <QMap>
#include <QObject>
#include <QProcess>

class StepFrames : public QObject {
    Q_OBJECT
public:
    static constexpr int batchSize = 24;
    QImage image(qint64 timestamp) const { return QImage::fromData(frames.value(timestamp), "JPEG"); }
    bool contains(qint64 timestamp) const { return frames.contains(timestamp); }
    bool setPreviewSize(QSize size);
    bool busy() const { return decoder != nullptr; }
    void load(const QString &file, qint64 startUs, qint64 anchorUs);
    void clear();
    ~StepFrames() override { clear(); }
signals:
    void loaded();
private:
    QMap<qint64, QByteArray> frames;
    QSize previewSize;
    qsizetype bytes = 0;
    QProcess *decoder = nullptr;
};
