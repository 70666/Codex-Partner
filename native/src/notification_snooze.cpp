#include "notification_snooze.h"

#include <algorithm>

namespace codex_partner {
namespace {

const wchar_t* T(bool chinese, const wchar_t* english, const wchar_t* simplified_chinese) noexcept {
    return chinese ? simplified_chinese : english;
}

}  // namespace

std::chrono::system_clock::time_point ResolveNotificationSnooze(
    NotificationSnoozePreset preset, std::chrono::system_clock::time_point now) noexcept {
    switch (preset) {
    case NotificationSnoozePreset::Active: return {};
    case NotificationSnoozePreset::OneHour: return now + std::chrono::hours{1};
    case NotificationSnoozePreset::FourHours: return now + std::chrono::hours{4};
    case NotificationSnoozePreset::TwentyFourHours: return now + std::chrono::hours{24};
    }
    return {};
}

bool IsNotificationSnoozed(std::chrono::system_clock::time_point until,
    std::chrono::system_clock::time_point now) noexcept {
    return until > now && until <= now + std::chrono::hours{24 * 7};
}

bool ShouldDeliverUsageAlert(bool enabled, std::chrono::system_clock::time_point snoozed_until,
    UsageAlertLevel level, UsageAlertLevel previous_level,
    std::chrono::system_clock::time_point now) noexcept {
    return enabled && !IsNotificationSnoozed(snoozed_until, now) &&
        static_cast<int>(level) > static_cast<int>(previous_level);
}

std::wstring FormatNotificationSnooze(std::chrono::system_clock::time_point until, bool chinese,
    std::chrono::system_clock::time_point now) {
    if (!IsNotificationSnoozed(until, now)) return T(chinese, L"Active", L"正常提醒");
    const auto seconds = std::max(std::chrono::seconds{1},
        std::chrono::duration_cast<std::chrono::seconds>(until - now));
    const auto minutes = (seconds.count() + 59) / 60;
    if (minutes < 60) {
        return T(chinese, L"Paused · ", L"已暂停 · ") + std::to_wstring(minutes) +
            T(chinese, L" min left", L" 分钟后恢复");
    }
    const auto hours = minutes / 60;
    const auto remainder = minutes % 60;
    std::wstring result = T(chinese, L"Paused · ", L"已暂停 · ") + std::to_wstring(hours) +
        T(chinese, L"h", L" 小时");
    if (remainder != 0) {
        result += L" " + std::to_wstring(remainder) + T(chinese, L"m", L" 分钟");
    }
    return result + T(chinese, L" left", L"后恢复");
}

std::wstring FormatNotificationSnoozeCompact(std::chrono::system_clock::time_point until, bool chinese,
    std::chrono::system_clock::time_point now) {
    if (!IsNotificationSnoozed(until, now)) return T(chinese, L"Active", L"开启");
    const auto seconds = std::max(std::chrono::seconds{1},
        std::chrono::duration_cast<std::chrono::seconds>(until - now));
    const auto minutes = (seconds.count() + 59) / 60;
    if (minutes < 60) {
        return std::to_wstring(minutes) + T(chinese, L" min", L" 分钟");
    }
    const auto hours = (minutes + 59) / 60;
    return std::to_wstring(hours) + T(chinese, L"h", L" 小时");
}

}  // namespace codex_partner
