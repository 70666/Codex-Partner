#pragma once

#include "settings_store.h"

namespace codex_partner {

struct FloatBarPoint {
    int x = 0;
    int y = 0;
};

// Keeps a persisted float-bar origin inside one monitor work area. An unset
// origin uses the unobtrusive top-right default. The function is independent
// from HWND state so removed-monitor and DPI edge cases remain testable.
[[nodiscard]] FloatBarPoint ConstrainFloatBarPosition(
    int stored_x,
    int stored_y,
    int work_left,
    int work_top,
    int work_right,
    int work_bottom,
    int width,
    int height,
    int margin = 16) noexcept;

}  // namespace codex_partner
