#pragma once

#include <QString>
#include <QStringList>
#include <optional>

inline constexpr auto defaultReplayHotkey = "Ctrl+Shift+F9";

struct ReplayHotkey {
    enum Modifier : unsigned { Alt = 1, Ctrl = 2, Shift = 4 };
    unsigned modifiers = 0;
    // Windows virtual-key code. Alphanumeric keys match ASCII.
    unsigned key = 0;
    bool operator==(const ReplayHotkey &) const = default;
};

inline std::optional<ReplayHotkey> parseReplayHotkey(const QString &text)
{
    auto parts = text.split('+');
    const QString key = parts.takeLast().toUpper();
    ReplayHotkey result;
    for (const QString &part : parts) {
        unsigned modifier = 0;
        if (part == "Alt") modifier = ReplayHotkey::Alt;
        else if (part == "Ctrl") modifier = ReplayHotkey::Ctrl;
        else if (part == "Shift") modifier = ReplayHotkey::Shift;
        if (!modifier || (result.modifiers & modifier)) return {};
        result.modifiers |= modifier;
    }
    if (!result.modifiers) return {};
    if (key.size() == 1 && ((key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9'))) {
        result.key = key[0].unicode();
    } else if (key.startsWith('F')) {
        const int number = key.sliced(1).toInt();
        if (number < 1 || number > 24 || number == 12 || key != QString("F%1").arg(number)) return {};
        result.key = 0x70 + number - 1;
    } else {
        return {};
    }
    return result;
}
