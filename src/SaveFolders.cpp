#include "SaveFolders.h"
#include "ReplayAppFolders.h"
#include <QTableWidget>
#include <QHeaderView>

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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

std::function<void(QSettings &)> createSaveFoldersPage(QWidget &page)
{
    const auto previous = saveFolders();
    auto *form = new QFormLayout(&page);
    const auto addFolder = [&](const QString &label, const QString &path, const QString &fallback) {
        auto *field = new QLineEdit(path, &page);
        field->setReadOnly(true);
        field->setPlaceholderText(fallback);
        auto *row = new QHBoxLayout;
        auto *browse = new QPushButton("Browse…", &page);
        auto *reset = new QPushButton("Use default", &page);
        row->addWidget(field, 1);
        row->addWidget(browse);
        row->addWidget(reset);
        form->addRow(label, row);
        QObject::connect(browse, &QPushButton::clicked, &page, [&page, field, label, fallback] {
            auto initial = field->text().isEmpty() ? fallback : field->text();
            if (!QFileInfo(initial).isDir()) initial = QDir::homePath();
            const auto selected = QFileDialog::getExistingDirectory(&page, "Choose " + label, initial);
            if (!selected.isEmpty()) field->setText(QDir::toNativeSeparators(selected));
        });
        QObject::connect(reset, &QPushButton::clicked, field, &QLineEdit::clear);
        return field;
    };

    QString replays = previous.replays;
#ifdef Q_OS_WIN
    auto *replayField = addFolder("Replay clips", QDir::toNativeSeparators(replays), defaultReplayFolder());
    form->addRow(new QLabel("Replay folder changes apply the next time you start replay.", &page));
    const QSettings stored("Klippit", "Klippit");
    const auto grouped = stored.value(desktopAppsKey).toStringList();
    const auto customNames = stored.value(appFolderNamesKey).toMap();
    auto known = stored.value(clippedAppsKey).toStringList();
    known.append(grouped);
    // Import pre-history folder names once. Custom destination folders are
    // never scanned as if they were executables.
    if (!stored.contains(appFolderNamesKey)) {
        const QDir root(replays.isEmpty() ? defaultReplayFolder() : replays);
        for (const auto &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QDir folder(root.filePath(name));
            if (!folder.entryList({"Replay-*.mkv"}, QDir::Files).isEmpty()) known.append(name);
        }
    }
    known.sort(Qt::CaseInsensitive);
    auto *apps = new QTableWidget(0, 3, &page);
    apps->setHorizontalHeaderLabels({"App", "Custom folder name", "Use Desktop"});
    apps->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    apps->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    apps->verticalHeader()->hide();
    QString last;
    for (const auto &name : known) {
        if (name.compare(last, Qt::CaseInsensitive) == 0 ||
            name.compare("Desktop", Qt::CaseInsensitive) == 0) continue;
        last = name;
        const int row = apps->rowCount();
        apps->insertRow(row);
        auto *app = new QTableWidgetItem(name);
        app->setFlags(app->flags() & ~Qt::ItemIsEditable);
        apps->setItem(row, 0, app);
        auto *custom = new QLineEdit(customNames.value(name.toCaseFolded()).toString());
        custom->setMaxLength(120);
        custom->setPlaceholderText(name);
        custom->setAccessibleName("Custom folder name for " + name);
        apps->setCellWidget(row, 1, custom);
        auto *desktop = new QTableWidgetItem;
        desktop->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        desktop->setCheckState(grouped.contains(name, Qt::CaseInsensitive) ? Qt::Checked : Qt::Unchecked);
        apps->setItem(row, 2, desktop);
    }
    form->addRow("App folders", apps);
    auto *hint = new QLabel(apps->rowCount()
        ? "Leave the name blank to use the app name. Use Desktop takes priority."
        : "Apps appear here after you save a replay.", &page);
    hint->setWordWrap(true);
    form->addRow(hint);
#endif
    auto *trimField = addFolder("Trimmed exports", QDir::toNativeSeparators(previous.trims),
                                "Same folder as the source clip");
    return [=](QSettings &settings) {
#ifdef Q_OS_WIN
        const auto replayPath = QDir::fromNativeSeparators(replayField->text());
        QStringList desktopApps;
        QVariantMap customNames;
        auto known = settings.value(clippedAppsKey).toStringList();
        for (int row = 0; row < apps->rowCount(); ++row) {
            const auto app = apps->item(row, 0)->text();
            const auto *custom = qobject_cast<QLineEdit *>(apps->cellWidget(row, 1));
            const auto folder = safeReplayFolderName(custom->text());
            if (!folder.isEmpty()) customNames.insert(app.toCaseFolded(), folder);
            if (apps->item(row, 2)->checkState() == Qt::Checked) desktopApps.append(app);
            if (!known.contains(app, Qt::CaseInsensitive)) known.append(app);
        }
        settings.setValue(desktopAppsKey, desktopApps);
        settings.setValue(clippedAppsKey, known);
        settings.setValue(appFolderNamesKey, customNames);
#else
        const auto replayPath = replays;
#endif
        settings.setValue(settingsKey, QVariantMap{{"replays", replayPath},
            {"trims", QDir::fromNativeSeparators(trimField->text())}});
    };
}
