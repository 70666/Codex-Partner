#pragma once

#include "usage_model.h"

#include <array>
#include <cstdint>
#include <optional>

namespace codex_partner {

constexpr std::uint32_t kTrayIconSize = 32;
constexpr std::size_t kTrayIconPixelBytes =
    static_cast<std::size_t>(kTrayIconSize) * kTrayIconSize * 4;

struct TrayIconModel {
    double primary_used_percent = 0.0;
    std::optional<double> secondary_used_percent;
    bool degraded = true;
};

using TrayIconPixels = std::array<std::uint8_t, kTrayIconPixelBytes>;

[[nodiscard]] TrayIconModel BuildTrayIconModel(const UsageSnapshot& snapshot) noexcept;
[[nodiscard]] TrayIconPixels RenderTrayIconRgba(const TrayIconModel& model) noexcept;

}  // namespace codex_partner
