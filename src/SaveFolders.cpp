#include "SaveFolders.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>

namespace {
constexpr auto settingsKey = "saveFolders";
}

SaveFolders saveFolders()
{
    const QSettings settings("Klippit", "Klippit");
    const auto values = settings.value(settingsKey).toMap();
    return {values.value("replays").toString(), values.value("trims").toString()};
}

QString defaultReplayFolder()
{
    const auto movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    return QDir(movies.isEmpty() ? QDir::homePath() : movies).filePath("Klippit");
}

void editSaveFolders(QWidget &parent)
{
    const auto previous = saveFolders();
    QDialog dialog(&parent);
    dialog.setWindowTitle("Save folders");
    auto *form = new QFormLayout(&dialog);
    const auto addFolder = [&](const QString &label, const QString &path, const QString &fallback) {
        auto *field = new QLineEdit(path, &dialog);
        field->setReadOnly(true);
        field->setPlaceholderText(fallback);
        auto *row = new QHBoxLayout;
        auto *browse = new QPushButton("Browse…", &dialog);
        auto *reset = new QPushButton("Use default", &dialog);
        row->addWidget(field, 1);
        row->addWidget(browse);
        row->addWidget(reset);
        form->addRow(label, row);
        QObject::connect(browse, &QPushButton::clicked, &dialog, [&dialog, field, label, fallback] {
            auto initial = field->text().isEmpty() ? fallback : field->text();
            if (!QFileInfo(initial).isDir()) initial = QDir::homePath();
            const auto selected = QFileDialog::getExistingDirectory(&dialog, "Choose " + label, initial);
            if (!selected.isEmpty()) field->setText(QDir::toNativeSeparators(selected));
        });
        QObject::connect(reset, &QPushButton::clicked, field, &QLineEdit::clear);
        return field;
    };

    QString replays = previous.replays;
#ifdef Q_OS_WIN
    auto *replayField = addFolder("Replay clips", QDir::toNativeSeparators(replays), defaultReplayFolder());
    form->addRow(new QLabel("Replay folder changes apply the next time you start replay.", &dialog));
#endif
    auto *trimField = addFolder("Trimmed exports", QDir::toNativeSeparators(previous.trims),
                                "Same folder as the source clip");
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(680, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted) return;

#ifdef Q_OS_WIN
    replays = QDir::fromNativeSeparators(replayField->text());
#endif
    QSettings settings("Klippit", "Klippit");
    settings.setValue(settingsKey, QVariantMap{{"replays", replays},
        {"trims", QDir::fromNativeSeparators(trimField->text())}});
    settings.sync();
    if (settings.status() != QSettings::NoError)
        QMessageBox::warning(&parent, "Save folders", "Could not save your folder preferences.");
}
