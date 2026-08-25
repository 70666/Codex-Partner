#pragma once

#include "usage_model.h"

#include <chrono>
#include <string>

namespace codex_partner {

enum class NotificationSnoozePreset { Active, OneHour, FourHours, TwentyFourHours };

[[nodiscard]] std::chrono::system_clock::time_point ResolveNotificationSnooze(
    NotificationSnoozePreset preset,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) noexcept;
[[nodiscard]] bool IsNotificationSnoozed(
    std::chrono::system_clock::time_point until,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) noexcept;
[[nodiscard]] bool ShouldDeliverUsageAlert(
    bool enabled,
    std::chrono::system_clock::time_point snoozed_until,
    UsageAlertLevel level,
    UsageAlertLevel previous_level,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) noexcept;
[[nodiscard]] std::wstring FormatNotificationSnooze(
    std::chrono::system_clock::time_point until,
    bool chinese,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
[[nodiscard]] std::wstring FormatNotificationSnoozeCompact(
    std::chrono::system_clock::time_point until,
    bool chinese,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

}  // namespace codex_partner
