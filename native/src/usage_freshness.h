#pragma once

#include "usage_model.h"

#include <chrono>
#include <optional>
#include <string>

namespace codex_partner {

// Formats the last successful provider refresh without trusting missing or
// implausibly future timestamps. The result is safe for UI, accessibility,
// tooltips, and copied summaries.
[[nodiscard]] std::wstring FormatUsageFreshness(
    std::chrono::system_clock::time_point updated_at,
    bool chinese,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

enum class OpenRefreshReason {
    Fresh,
    AlreadyRefreshing,
    RetryCooldown,
    MissingTimestamp,
    DegradedSnapshot,
    AgedSnapshot,
    InvalidTimestamp,
};

struct OpenRefreshDecision {
    bool should_refresh = false;
    OpenRefreshReason reason = OpenRefreshReason::Fresh;
};

// Decides whether revealing the daily-glance surface should start background
// work. Recent live data opens without I/O; degraded or old data refreshes once,
// while active work and rapid reopen attempts never amplify into a request
// storm. This policy is shared by tray, global-shortcut, floating-bar, and
// second-instance activation because they all converge on App::ShowPopup.
[[nodiscard]] OpenRefreshDecision EvaluateOpenRefresh(
    const UsageSnapshot& snapshot,
    bool refresh_active,
    std::optional<std::chrono::steady_clock::time_point> last_refresh_started,
    std::chrono::steady_clock::time_point steady_now = std::chrono::steady_clock::now(),
    std::chrono::system_clock::time_point wall_now = std::chrono::system_clock::now(),
    std::chrono::minutes maximum_live_age = std::chrono::minutes{5},
    std::chrono::seconds retry_cooldown = std::chrono::seconds{30}) noexcept;

}  // namespace codex_partner
