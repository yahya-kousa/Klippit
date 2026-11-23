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
                                             qint64 requestedStartUs, qint64 endUs)
{
    auto after = std::upper_bound(sortedKeysUs.begin(), sortedKeysUs.end(), requestedStartUs);
    if (after == sortedKeysUs.begin()) return std::nullopt;
    const auto startUs = *std::prev(after);
    return TrimRange{startUs, endUs - startUs};
}
