#include "float_bar_model.h"

#include <algorithm>

namespace codex_partner {

FloatBarPoint ConstrainFloatBarPosition(int stored_x, int stored_y,
    int work_left, int work_top, int work_right, int work_bottom,
    int width, int height, int margin) noexcept {
    const int safe_width = std::max(1, width);
    const int safe_height = std::max(1, height);
    const int safe_margin = std::max(0, margin);
    const int maximum_x = std::max(work_left, work_right - safe_width);
    const int maximum_y = std::max(work_top, work_bottom - safe_height);
    if (stored_x == kUnsetWindowPosition || stored_y == kUnsetWindowPosition) {
        return {
            std::clamp(work_right - safe_width - safe_margin, work_left, maximum_x),
            std::clamp(work_top + safe_margin, work_top, maximum_y),
        };
    }
    return {
        std::clamp(stored_x, work_left, maximum_x),
        std::clamp(stored_y, work_top, maximum_y),
    };
}

}  // namespace codex_partner
