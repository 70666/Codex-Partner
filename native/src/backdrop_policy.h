#pragma once

namespace codex_partner::ui {

enum class BackdropStyle {
    Solid,
    AcrylicGlass,
    TransparentGlass,
};

constexpr bool BackdropPreservesAlpha(BackdropStyle style) noexcept {
    return style != BackdropStyle::Solid;
}

// Windows background acrylic becomes an opaque fallback whenever its window
// is inactive. Keep it only while the user is interacting with a panel; an
// inactive panel and the persistent floating bar use alpha composition.
constexpr BackdropStyle ResolveBackdropStyle(
    bool glass_available, bool window_active, bool always_transparent) noexcept {
    if (!glass_available) return BackdropStyle::Solid;
    return window_active && !always_transparent ?
        BackdropStyle::AcrylicGlass : BackdropStyle::TransparentGlass;
}

}  // namespace codex_partner::ui
