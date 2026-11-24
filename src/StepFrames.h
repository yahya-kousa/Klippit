#pragma once
#include <QImage>
#include <QMap>
#include <QObject>
#include <QProcess>

class StepFrames : public QObject {
    Q_OBJECT
public:
    static constexpr int batchSize = 24;
    QImage image(qint64 timestamp) const { return frames.value(timestamp); }
    bool busy() const { return decoder != nullptr; }
    void load(const QString &file, qint64 startUs, qint64 anchorUs);
    void clear();
    ~StepFrames() override { clear(); }
signals:
    void loaded();
private:
    QMap<qint64, QImage> frames;
    qsizetype bytes = 0;
    QProcess *decoder = nullptr;
};
