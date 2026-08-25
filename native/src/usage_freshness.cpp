#include "usage_freshness.h"

#include <algorithm>
#include <cstdint>

namespace codex_partner {
namespace {

const wchar_t* T(bool chinese, const wchar_t* english, const wchar_t* simplified_chinese) noexcept {
    return chinese ? simplified_chinese : english;
}

}  // namespace

std::wstring FormatUsageFreshness(std::chrono::system_clock::time_point updated_at, bool chinese,
    std::chrono::system_clock::time_point now) {
    if (updated_at.time_since_epoch() <= std::chrono::system_clock::duration::zero()) {
        return T(chinese, L"No successful update yet", L"尚无成功更新");
    }

    const auto future_skew = updated_at - now;
    if (future_skew > std::chrono::minutes{5}) {
        return T(chinese, L"Update time unavailable", L"更新时间不可用");
    }

    const auto age = std::max(std::chrono::system_clock::duration::zero(), now - updated_at);
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(age).count();
    if (minutes < 1) return T(chinese, L"Updated just now", L"刚刚更新");
    if (minutes < 60) {
        return chinese ? std::to_wstring(minutes) + L" 分钟前更新" :
            L"Updated " + std::to_wstring(minutes) + L" min ago";
    }
    const auto hours = minutes / 60;
    if (hours < 24) {
        return chinese ? std::to_wstring(hours) + L" 小时前更新" :
            L"Updated " + std::to_wstring(hours) + (hours == 1 ? L" hr ago" : L" hrs ago");
    }
    const auto days = hours / 24;
    return chinese ? std::to_wstring(days) + L" 天前更新" :
        L"Updated " + std::to_wstring(days) + (days == 1 ? L" day ago" : L" days ago");
}

OpenRefreshDecision EvaluateOpenRefresh(const UsageSnapshot& snapshot, bool refresh_active,
    std::optional<std::chrono::steady_clock::time_point> last_refresh_started,
    std::chrono::steady_clock::time_point steady_now, std::chrono::system_clock::time_point wall_now,
    std::chrono::minutes maximum_live_age, std::chrono::seconds retry_cooldown) noexcept {
    if (refresh_active) return {false, OpenRefreshReason::AlreadyRefreshing};

    const auto safe_cooldown = std::max(std::chrono::seconds{1}, retry_cooldown);
    if (last_refresh_started) {
        if (steady_now < *last_refresh_started || steady_now - *last_refresh_started < safe_cooldown) {
            return {false, OpenRefreshReason::RetryCooldown};
        }
    }

    if (!snapshot.error.empty() || snapshot.stale) {
        return {true, OpenRefreshReason::DegradedSnapshot};
    }
    if (snapshot.updated_at.time_since_epoch() <= std::chrono::system_clock::duration::zero()) {
        return {true, OpenRefreshReason::MissingTimestamp};
    }
    if (snapshot.updated_at - wall_now > std::chrono::minutes{5}) {
        return {true, OpenRefreshReason::InvalidTimestamp};
    }

    const auto safe_maximum_age = std::max(std::chrono::minutes{1}, maximum_live_age);
    if (wall_now - snapshot.updated_at >= safe_maximum_age) {
        return {true, OpenRefreshReason::AgedSnapshot};
    }
    return {false, OpenRefreshReason::Fresh};
}

}  // namespace codex_partner
