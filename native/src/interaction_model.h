#pragma once

#include "native_ui.h"

namespace codex_partner::ui {

// Returns the next keyboard-focusable action. Direction is positive for
// forward/down and negative for backward/up; focus wraps at both ends.
[[nodiscard]] PopupAction StepPopupAction(PopupAction current, int direction) noexcept;
[[nodiscard]] FloatBarAction StepFloatBarAction(FloatBarAction current, int direction) noexcept;
[[nodiscard]] SettingsAction StepSettingsAction(SettingsTab tab, SettingsAction current, int direction) noexcept;

// Popup blur normally dismisses the tray surface. During a second-instance
// activation handoff the popup stays visible until it receives real user input;
// Windows can otherwise grant and immediately revoke foreground ownership.
[[nodiscard]] bool ShouldDismissPopupOnDeactivate(
    bool settings_visible,
    bool proof_mode,
    bool activation_handoff_pending) noexcept;

}  // namespace codex_partner::ui
