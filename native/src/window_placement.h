#pragma once

namespace codex_partner::windowing {

struct PixelRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct PopupPlacement {
    int x = 0;
    int y = 0;
};

// Computes a physical-pixel popup origin that keeps the complete window,
// including a safety allowance for the DWM shadow, inside the monitor work
// area. The caller must pass a width and height scaled for the target monitor.
[[nodiscard]] PopupPlacement CalculatePopupPlacement(
    PixelRect anchor,
    PixelRect work_area,
    int window_width,
    int window_height,
    int safe_inset = 16,
    int anchor_gap = 8,
    int trailing_bias = 24) noexcept;

}  // namespace codex_partner::windowing
