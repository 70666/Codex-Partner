#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

namespace codex_partner::rendering {

// DWM's alpha-aware redirection surface requires premultiplied BGRA. GDI+
// already writes that format into a 32-bpp DIB; this final pass makes the
// channel invariant explicit and removes invalid color from transparent pixels.
inline void FinalizeBgra(std::span<std::uint32_t> pixels, bool preserve_alpha) noexcept {
    for (std::uint32_t& pixel : pixels) {
        if (!preserve_alpha) {
            pixel |= 0xFF000000U;
            continue;
        }
        const std::uint32_t alpha = pixel >> 24U;
        if (alpha == 0U) {
            pixel = 0U;
            continue;
        }
        const std::uint32_t red = std::min((pixel >> 16U) & 0xFFU, alpha);
        const std::uint32_t green = std::min((pixel >> 8U) & 0xFFU, alpha);
        const std::uint32_t blue = std::min(pixel & 0xFFU, alpha);
        pixel = (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
    }
}

[[nodiscard]] inline bool IsPremultipliedBgra(std::span<const std::uint32_t> pixels) noexcept {
    return std::ranges::all_of(pixels, [](std::uint32_t pixel) {
        const std::uint32_t alpha = pixel >> 24U;
        return ((pixel >> 16U) & 0xFFU) <= alpha &&
            ((pixel >> 8U) & 0xFFU) <= alpha && (pixel & 0xFFU) <= alpha;
    });
}

}  // namespace codex_partner::rendering
