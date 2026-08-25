#include "window_placement.h"

#include <algorithm>
#include <cstdint>

namespace codex_partner::windowing {
namespace {

int Midpoint(int first, int second) noexcept {
    return first + static_cast<int>((static_cast<std::int64_t>(second) - first) / 2);
}

int ConstrainOrigin(int preferred, int lower, int upper, int extent) noexcept {
    const std::int64_t maximum = static_cast<std::int64_t>(upper) - std::max(0, extent);
    if (maximum <= lower) return lower;
    return std::clamp(preferred, lower, static_cast<int>(maximum));
}

}  // namespace

PopupPlacement CalculatePopupPlacement(
    PixelRect anchor,
    PixelRect work_area,
    int window_width,
    int window_height,
    int safe_inset,
    int anchor_gap,
    int trailing_bias) noexcept {
    window_width = std::max(1, window_width);
    window_height = std::max(1, window_height);
    safe_inset = std::max(0, safe_inset);
    anchor_gap = std::max(0, anchor_gap);

    int safe_left = work_area.left + safe_inset;
    int safe_top = work_area.top + safe_inset;
    int safe_right = work_area.right - safe_inset;
    int safe_bottom = work_area.bottom - safe_inset;
    if (safe_right <= safe_left) {
        safe_left = work_area.left;
        safe_right = std::max(work_area.left + 1, work_area.right);
    }
    if (safe_bottom <= safe_top) {
        safe_top = work_area.top;
        safe_bottom = std::max(work_area.top + 1, work_area.bottom);
    }

    const int anchor_x = Midpoint(anchor.left, anchor.right);
    const int preferred_x = anchor_x - window_width + trailing_bias;
    const int x = ConstrainOrigin(preferred_x, safe_left, safe_right, window_width);

    const int above = anchor.top - anchor_gap - window_height;
    const int below = anchor.bottom + anchor_gap;
    int preferred_y = above;
    if (above < safe_top && static_cast<std::int64_t>(below) + window_height <= safe_bottom) {
        preferred_y = below;
    } else if (above < safe_top) {
        const int anchor_y = Midpoint(anchor.top, anchor.bottom);
        const int work_midpoint = Midpoint(safe_top, safe_bottom);
        preferred_y = anchor_y <= work_midpoint ? below : above;
    }
    const int y = ConstrainOrigin(preferred_y, safe_top, safe_bottom, window_height);
    return {x, y};
}

}  // namespace codex_partner::windowing
