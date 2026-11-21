#pragma once
#include <QtGlobal>
#include <algorithm>
#include <iterator>
#include <optional>
#include <vector>

struct TrimRange {
    qint64 startUs;
    qint64 durationUs;
};

inline std::optional<TrimRange> losslessRange(const std::vector<qint64> &sortedKeysUs,
                                             qint64 requestedStartMs, qint64 endMs)
{
    auto after = std::upper_bound(sortedKeysUs.begin(), sortedKeysUs.end(), requestedStartMs * 1000);
    if (after == sortedKeysUs.begin()) return std::nullopt;
    const auto startUs = *std::prev(after);
    return TrimRange{startUs, endMs * 1000 - startUs};
}
