#include "ReplaySettings.h"
#include "ReplayHotkey.h"

#include <QtWidgets>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
enum Audio { Desktop, VideoOnly, Mixed, Separate };

struct Settings {
    int history = 15;
    int fps = 60;
    int bitrate = 30;
    QString shortcut = defaultReplayHotkey;
    Audio audio = Desktop;
    QString mic;
    QString micLabel;
};

struct AudioDevice {
    QString name;
    QString id;
};

QList<AudioDevice> directShowAudioDevices(const QByteArray &log)
{
    static const QRegularExpression prefix(R"(^\[[^\]]+ @ [^\]]+\]\s*)");
    static const QRegularExpression device(R"re(^"(.*)"(?: \(([^)]*)\))?$)re");
    static const QRegularExpression alternative(R"re(^Alternative name "(.*)"$)re");
    QList<AudioDevice> devices;
    bool audioSection = false;
    bool lastWasAudio = false;
    for (auto line : QString::fromUtf8(log).split('\n')) {
        line.remove(prefix);
        line = line.trimmed();
        if (const auto alias = alternative.match(line); alias.hasMatch() && lastWasAudio) {
            devices.last().id = alias.captured(1);
        } else if (const auto entry = device.match(line); entry.hasMatch()) {
            const auto type = entry.captured(2);
            lastWasAudio = type == "audio" || (type.isEmpty() && audioSection);
            if (lastWasAudio) devices.append({entry.captured(1), entry.captured(1)});
        } else {
            // Older builds list the device types in separate sections.
            if (line.startsWith("DirectShow audio devices")) audioSection = true;
            if (line.startsWith("DirectShow video devices")) audioSection = false;
            lastWasAudio = false;
        }
    }
    return devices;
}

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
    result.micLabel = values.value("mic_label", result.mic).toString();
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

SaveReplaySettings createReplaySettingsPage(QWidget &page)
{
    const auto settings = load();
    auto *layout = new QVBoxLayout(&page);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    auto *basic = new QFormLayout;
    basic->setVerticalSpacing(8);
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
    auto *mic = new QComboBox;
    mic->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mic->setMinimumContentsLength(24);
    mic->setPlaceholderText("Select a microphone");
    if (!settings.mic.isEmpty()) {
        mic->addItem(settings.micLabel + " (saved)", settings.mic);
        mic->setItemData(0, settings.micLabel, Qt::UserRole + 1);
    }
    auto *refresh = new QPushButton("Refresh");
    auto *micRow = new QWidget;
    auto *micLayout = new QHBoxLayout(micRow);
    micLayout->setContentsMargins(0, 0, 0, 0);
    micLayout->addWidget(mic, 1);
    micLayout->addWidget(refresh);
    auto *deviceStatus = new QLabel;
    deviceStatus->setWordWrap(true);
    basic->addRow("Replay history", history);
    basic->addRow("Save hotkey", shortcut);
    basic->addRow("Audio", audio);
    basic->addRow("Microphone", micRow);
    basic->addRow(deviceStatus);

    struct MicrophoneProbe : QProcess {
        using QProcess::QProcess;
        ~MicrophoneProbe() override {
            if (state() == QProcess::NotRunning) return;
            blockSignals(true);
            kill();
            waitForFinished();
        }
        QTimer timeout;
        bool searched = false;
        bool timedOut = false;
    };
    auto *deviceProbe = new MicrophoneProbe(&page);
#ifdef Q_OS_WIN
    deviceProbe->setCreateProcessArgumentsModifier([](auto *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    deviceProbe->timeout.setSingleShot(true);
    auto updateMic = [=] {
        const bool micMode = audio->currentIndex() >= Mixed;
        const bool scanning = deviceProbe->state() != QProcess::NotRunning;
        basic->setRowVisible(micRow, micMode);
        basic->setRowVisible(deviceStatus, micMode && !deviceStatus->text().isEmpty());
        mic->setEnabled(!scanning && mic->count() > 0);
        refresh->setEnabled(!scanning);
    };
    auto refreshDevices = [=] {
        if (deviceProbe->state() != QProcess::NotRunning) return;
        deviceProbe->searched = true;
        deviceProbe->timedOut = false;
        deviceStatus->setText("Looking for microphones…");
        deviceProbe->start("ffmpeg", {"-hide_banner", "-nostdin", "-f", "dshow",
                                     "-list_devices", "true", "-i", "dummy"});
        deviceProbe->timeout.start(5000);
        updateMic();
    };
    QObject::connect(&deviceProbe->timeout, &QTimer::timeout, &page, [=] {
        deviceProbe->timedOut = true;
        deviceProbe->kill();
    });
    QObject::connect(deviceProbe, &QProcess::errorOccurred, &page, [=](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        deviceProbe->timeout.stop();
        deviceStatus->setText("Could not start FFmpeg to list microphones.");
        updateMic();
    });
    QObject::connect(deviceProbe, &QProcess::finished, &page, [=](int, QProcess::ExitStatus exit) {
        deviceProbe->timeout.stop();
        // The dummy input can return nonzero after successfully printing the list.
        const auto devices = directShowAudioDevices(deviceProbe->readAllStandardError());
        const auto selected = mic->currentData().toString();
        const auto previous = selected.isEmpty() ? settings.mic : selected;
        const auto previousName = selected.isEmpty() ? settings.micLabel : mic->currentData(Qt::UserRole + 1).toString();
        mic->clear();
        int selection = -1;
        QHash<QString, int> nameCount;
        for (const auto &device : devices) ++nameCount[device.name];
        QHash<QString, int> nextNumber;
        for (const auto &device : devices) {
            QString label = device.name;
            if (nameCount.value(device.name) > 1) label += QString(" (%1)").arg(++nextNumber[device.name]);
            const int row = mic->count();
            mic->addItem(label, device.id);
            mic->setItemData(row, device.name, Qt::UserRole + 1);
            if (previous == device.id ||
                (selection < 0 && nameCount.value(device.name) == 1 && previous == device.name)) selection = row;
        }
        if (selection < 0 && !previous.isEmpty()) {
            selection = mic->count();
            mic->addItem(previousName + " (unavailable)", previous);
            mic->setItemData(selection, previousName, Qt::UserRole + 1);
        }
        mic->setCurrentIndex(selection);
        if (deviceProbe->timedOut || exit != QProcess::NormalExit)
            deviceStatus->setText("Microphone scan did not finish. Try Refresh.");
        else if (devices.isEmpty())
            deviceStatus->setText("No microphones found. Connect one and click Refresh.");
        else
            deviceStatus->clear();
        updateMic();
    });
    QObject::connect(refresh, &QPushButton::clicked, &page, refreshDevices);
    auto audioChanged = [=] {
        updateMic();
        if (audio->currentIndex() >= Mixed && !deviceProbe->searched) refreshDevices();
    };
    QObject::connect(audio, &QComboBox::currentIndexChanged, &page, audioChanged);
    QTimer::singleShot(0, &page, audioChanged);

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
    layout->addStretch();
    return [=](QSettings &stored, const QList<QKeySequence> &editorKeys, QString &newHotkey) -> QString {
        const auto key = shortcut->keySequence().toString(QKeySequence::PortableText);
        if (!parseReplayHotkey(key))
            return "Use Ctrl, Alt, or Shift with a letter, digit, or function key. Windows reserves F12.";
        const auto previousKey = QKeySequence::fromString(settings.shortcut, QKeySequence::PortableText);
        if (editorKeys.contains(shortcut->keySequence()) || editorKeys.contains(previousKey))
            return "An editor shortcut conflicts with the replay save hotkey. Choose a different key combination.";
        const auto device = mic->currentData().toString();
        if (audio->currentIndex() >= Mixed && device.isEmpty())
            return "Select a microphone. If it is missing, connect it and click Refresh.";

        // Keep the previous hotkey until the recorder confirms the new one.
        stored.setValue(settingsKey, QVariantMap{{"history", history->value()}, {"shortcut", settings.shortcut},
                        {"audio", audio->currentIndex()}, {"mic", device.isEmpty() ? settings.mic : device},
                        {"mic_label", device.isEmpty() ? settings.micLabel : mic->currentData(Qt::UserRole + 1).toString()},
                        {"fps", fps->value()}, {"bitrate", bitrate->value()}});
        if (key != settings.shortcut) newHotkey = key;
        return {};
    };
}
