#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>

class ClipExporter : public QObject {
    Q_OBJECT

public:
    enum class Format { Mp4, Mkv };

    explicit ClipExporter(QObject *parent = nullptr);
    bool isBusy() const;
    void start(const QString &source, const QString &destination, Format format,
               qint64 startUs, qint64 durationUs);

signals:
    void statusChanged(const QString &message);
    void mp4Unsupported();

private:
    QProcess writer;
    std::unique_ptr<QTemporaryDir> staging;
    QString destination;
    Format format = Format::Mp4;
};
