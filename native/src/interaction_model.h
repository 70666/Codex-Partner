#pragma once

#include "native_ui.h"

namespace codex_partner::ui {

// Returns the next keyboard-focusable action. Direction is positive for
// forward/down and negative for backward/up; focus wraps at both ends.
[[nodiscard]] PopupAction StepPopupAction(PopupAction current, int direction) noexcept;
[[nodiscard]] FloatBarAction StepFloatBarAction(FloatBarAction current, int direction) noexcept;
[[nodiscard]] SettingsAction StepSettingsAction(SettingsTab tab, SettingsAction current, int direction) noexcept;

// Codex Partner is an inspectable surface, not a transient context menu. Losing
// activation never dismisses it; explicit Close, Escape, or tray toggle does.
[[nodiscard]] bool ShouldDismissPopupOnDeactivate(
    bool settings_visible,
    bool proof_mode,
    bool activation_handoff_pending) noexcept;

}  // namespace codex_partner::ui
