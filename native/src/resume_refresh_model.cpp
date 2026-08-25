#include "resume_refresh_model.h"

namespace codex_partner {

ResumeRefreshDecision EvaluateResumeRefresh(const UsageSnapshot& snapshot,
    std::optional<std::chrono::steady_clock::time_point> last_resume,
    std::chrono::steady_clock::time_point steady_now,
    std::chrono::system_clock::time_point wall_now,
    std::chrono::seconds duplicate_window,
    std::chrono::minutes maximum_live_age) noexcept {
    if (duplicate_window < std::chrono::seconds::zero()) duplicate_window = std::chrono::seconds::zero();
    if (maximum_live_age < std::chrono::minutes::zero()) maximum_live_age = std::chrono::minutes::zero();
    if (last_resume && steady_now >= *last_resume && steady_now - *last_resume < duplicate_window) {
        return {};
    }

    ResumeRefreshDecision decision{true, false};
    if ((!snapshot.session && !snapshot.weekly) || snapshot.stale || !snapshot.error.empty()) {
        return decision;
    }
    if (snapshot.updated_at.time_since_epoch() <= std::chrono::system_clock::duration::zero()) {
        decision.mark_usage_stale = true;
        return decision;
    }
    constexpr auto maximum_future_skew = std::chrono::minutes{5};
    if (snapshot.updated_at > wall_now + maximum_future_skew) {
        decision.mark_usage_stale = true;
        return decision;
    }
    if (snapshot.updated_at <= wall_now && wall_now - snapshot.updated_at >= maximum_live_age) {
        decision.mark_usage_stale = true;
    }
    return decision;
}

}  // namespace codex_partner
