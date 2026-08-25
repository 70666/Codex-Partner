#pragma once

#include "usage_model.h"

#include <chrono>
#include <optional>

namespace codex_partner {

struct ResumeRefreshDecision {
    bool accepted = false;
    bool mark_usage_stale = false;
};

[[nodiscard]] ResumeRefreshDecision EvaluateResumeRefresh(
    const UsageSnapshot& snapshot,
    std::optional<std::chrono::steady_clock::time_point> last_resume,
    std::chrono::steady_clock::time_point steady_now = std::chrono::steady_clock::now(),
    std::chrono::system_clock::time_point wall_now = std::chrono::system_clock::now(),
    std::chrono::seconds duplicate_window = std::chrono::seconds{30},
    std::chrono::minutes maximum_live_age = std::chrono::minutes{2}) noexcept;

}  // namespace codex_partner
