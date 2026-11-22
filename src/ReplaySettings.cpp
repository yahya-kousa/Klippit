#include "ReplaySettings.h"
#include "ReplayHotkey.h"

#include <QtWidgets>
#include <QSettings>
#include <algorithm>

namespace {
enum Audio { Desktop, VideoOnly, Mixed, Separate };

struct Settings {
    int history = 15;
    int fps = 60;
    int bitrate = 30;
    QString shortcut = defaultReplayHotkey;
    Audio audio = Desktop;
    QString mic;
};

constexpr auto settingsKey = "replay/options";

Settings load()
{
    QSettings settings("Klippit", "Klippit");
    const auto values = settings.value(settingsKey).toMap();
    Settings result;
    result.history = std::clamp(values.value("history", result.history).toInt(), 1, 300);
    result.fps = std::clamp(values.value("fps", result.fps).toInt(), 1, 240);
    result.bitrate = std::clamp(values.value("bitrate", result.bitrate).toInt(), 1, 500);
    const auto shortcut = values.value("shortcut").toString();
    if (parseReplayHotkey(shortcut)) result.shortcut = shortcut;
    result.mic = values.value("mic").toString().trimmed();
    const int audio = values.value("audio", Desktop).toInt();
    if (audio == VideoOnly || ((audio == Mixed || audio == Separate) && !result.mic.isEmpty()))
        result.audio = Audio(audio);
    return result;
}
}

QStringList replaySettingsArguments()
{
    const auto settings = load();
    QStringList args{"--history", QString::number(settings.history), "--save-hotkey", settings.shortcut,
                     "--fps", QString::number(settings.fps), "--bitrate", QString::number(settings.bitrate)};
    if (settings.audio == Desktop) args << "--desktop-audio";
    if (settings.audio == Mixed || settings.audio == Separate) args << "--mic" << settings.mic;
    if (settings.audio == Separate) args << "--separate-mic";
    return args;
}

bool storeReplayHotkey(const QString &shortcut)
{
    QSettings settings("Klippit", "Klippit");
    auto values = settings.value(settingsKey).toMap();
    values.insert("shortcut", shortcut);
    settings.setValue(settingsKey, values);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

std::optional<QString> editReplaySettings(QWidget &parent)
{
    const auto settings = load();
    QDialog dialog(&parent);
    dialog.setWindowTitle("Replay Settings");
    auto *layout = new QVBoxLayout(&dialog);
    auto *basic = new QFormLayout;
    layout->addLayout(basic);
    auto spinBox = [](int minimum, int maximum, int value, const QString &suffix) {
        auto *spin = new QSpinBox;
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        spin->setSuffix(suffix);
        return spin;
    };
    auto *history = spinBox(1, 300, settings.history, " s");
    auto *shortcut = new QKeySequenceEdit(QKeySequence::fromString(settings.shortcut, QKeySequence::PortableText));
    shortcut->setMaximumSequenceLength(1);
    auto *audio = new QComboBox;
    audio->addItems({"Desktop audio", "Video only", "Desktop and microphone mixed", "Desktop and microphone separate"});
    audio->setCurrentIndex(settings.audio);
    auto *mic = new QLineEdit(settings.mic);
    mic->setPlaceholderText("Exact DirectShow device name");
    basic->addRow("Replay history", history);
    basic->addRow("Save hotkey", shortcut);
    basic->addRow("Audio", audio);
    basic->addRow("Microphone", mic);
    auto updateMic = [&] { mic->setEnabled(audio->currentIndex() >= Mixed); };
    QObject::connect(audio, &QComboBox::currentIndexChanged, &dialog, updateMic);
    updateMic();

    auto *showAdvanced = new QCheckBox("Advanced");
    layout->addWidget(showAdvanced);
    auto *advanced = new QWidget;
    auto *advancedForm = new QFormLayout(advanced);
    auto *fps = spinBox(1, 240, settings.fps, " FPS");
    auto *bitrate = spinBox(1, 500, settings.bitrate, " Mbps");
    advancedForm->addRow("Target frame rate", fps);
    advancedForm->addRow("Target video bitrate", bitrate);
    layout->addWidget(advanced);
    advanced->hide();
    QObject::connect(showAdvanced, &QCheckBox::toggled, advanced, &QWidget::setVisible);

    auto *message = new QLabel("The save hotkey updates immediately. Other changes apply the next time replay starts.");
    message->setWordWrap(true);
    layout->addWidget(message);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto key = shortcut->keySequence().toString(QKeySequence::PortableText);
        if (!parseReplayHotkey(key)) {
            message->setText("Use Ctrl, Alt, or Shift with a letter, digit, or function key. Windows reserves F12.");
            return;
        }
        const auto device = mic->text().trimmed();
        if (audio->currentIndex() >= Mixed && device.isEmpty()) {
            message->setText("Enter the DirectShow name of the microphone.");
            return;
        }
        QSettings stored("Klippit", "Klippit");
        // Leave the previous hotkey persisted until the controller applies the change.
        stored.setValue(settingsKey, QVariantMap{{"history", history->value()}, {"shortcut", settings.shortcut},
                        {"audio", audio->currentIndex()}, {"mic", device},
                        {"fps", fps->value()}, {"bitrate", bitrate->value()}});
        stored.sync();
        if (stored.status() == QSettings::NoError) dialog.accept();
        else message->setText("Could not save replay settings.");
    });
    if (dialog.exec() != QDialog::Accepted) return {};
    const auto key = shortcut->keySequence().toString(QKeySequence::PortableText);
    if (key == settings.shortcut) return {};
    return key;
}
