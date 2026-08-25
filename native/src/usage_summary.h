#pragma once

#include "usage_model.h"

#include <chrono>
#include <string>

namespace codex_partner {

// Returns a localized comparison between the latest rolling 24 hours and the
// independent daily average from the preceding six days. Empty means the
// nested windows do not contain enough trustworthy evidence for a comparison.
[[nodiscard]] std::wstring FormatSpendPaceInsight(
    const SpendSummary& spend,
    bool chinese,
    bool compact = false);

// Builds a compact, credential-free snapshot suitable for pasting into chat or
// an issue. Raw errors, account identifiers, paths, and model log data are
// intentionally excluded.
[[nodiscard]] std::wstring BuildUsageShareSummary(
    const UsageSnapshot& snapshot,
    bool chinese,
    bool hide_identity,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

}  // namespace codex_partner
