#include "tray_icon_model.h"

#include <algorithm>
#include <cmath>

namespace codex_partner {
namespace {

struct Rgb {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

double SafePercent(double value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0, 100.0) : 0.0;
}

Rgb UsageColor(double percent) noexcept {
    const double value = SafePercent(percent);
    if (value < 50.0) return {76, 175, 80};
    if (value < 80.0) return {255, 193, 7};
    if (value < 95.0) return {255, 152, 0};
    return {244, 67, 54};
}

void PutPixel(TrayIconPixels& pixels, std::uint32_t x, std::uint32_t y,
    Rgb color, std::uint8_t alpha = 255) noexcept {
    const std::size_t offset = (static_cast<std::size_t>(y) * kTrayIconSize + x) * 4;
    pixels[offset] = color.red;
    pixels[offset + 1] = color.green;
    pixels[offset + 2] = color.blue;
    pixels[offset + 3] = alpha;
}

}  // namespace

TrayIconModel BuildTrayIconModel(const UsageSnapshot& snapshot) noexcept {
    TrayIconModel model;
    if (snapshot.session) {
        model.primary_used_percent = SafePercent(snapshot.session->used_percent);
        if (snapshot.weekly) model.secondary_used_percent = SafePercent(snapshot.weekly->used_percent);
    } else if (snapshot.weekly) {
        model.primary_used_percent = SafePercent(snapshot.weekly->used_percent);
    }
    model.degraded = snapshot.loading || snapshot.stale || !snapshot.error.empty() ||
        NeedsProviderSetup(snapshot) || (!snapshot.session && !snapshot.weekly);
    return model;
}

TrayIconPixels RenderTrayIconRgba(const TrayIconModel& model) noexcept {
    TrayIconPixels pixels{};
    const std::uint8_t background_alpha = model.degraded ? 180 : 255;
    for (std::uint32_t y = 2; y < kTrayIconSize - 2; ++y) {
        for (std::uint32_t x = 2; x < kTrayIconSize - 2; ++x) {
            PutPixel(pixels, x, y, {60, 60, 70}, background_alpha);
        }
    }

    constexpr std::uint32_t bar_left = 4;
    constexpr std::uint32_t bar_right = kTrayIconSize - 4;
    constexpr std::uint32_t bar_width = bar_right - bar_left;
    const auto draw_bar = [&](std::uint32_t y_start, std::uint32_t y_end, double percent) {
        Rgb color = UsageColor(percent);
        if (model.degraded) {
            const auto gray = static_cast<std::uint8_t>((static_cast<unsigned>(color.red) +
                color.green + color.blue) / 3U);
            color = {gray, gray, gray};
        }
        const std::uint32_t fill = static_cast<std::uint32_t>(
            SafePercent(percent) / 100.0 * static_cast<double>(bar_width));
        const std::uint32_t fill_end = std::min(bar_left + fill, bar_right);
        for (std::uint32_t y = y_start; y < y_end; ++y) {
            for (std::uint32_t x = bar_left; x < bar_right; ++x) {
                PutPixel(pixels, x, y, {80, 80, 90});
            }
            for (std::uint32_t x = bar_left; x < fill_end; ++x) PutPixel(pixels, x, y, color);
        }
    };

    if (model.secondary_used_percent) {
        draw_bar(8, 15, model.primary_used_percent);
        draw_bar(18, 23, *model.secondary_used_percent);
    } else {
        draw_bar(10, 22, model.primary_used_percent);
    }
    return pixels;
}

}  // namespace codex_partner
