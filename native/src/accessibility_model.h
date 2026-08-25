#pragma once

#include "native_ui.h"
#include "settings_store.h"
#include "usage_model.h"

#include <string>
#include <vector>

namespace codex_partner::accessibility {

enum class Role { StaticText, PushButton, PageTab, CheckButton, ComboBox, Link };

enum State : unsigned {
    StateNone = 0,
    StateFocusable = 1U << 0,
    StateFocused = 1U << 1,
    StatePressed = 1U << 2,
    StateChecked = 1U << 3,
    StateSelected = 1U << 4,
    StateUnavailable = 1U << 5,
    StateReadOnly = 1U << 6,
};

struct Bounds {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct Element {
    long child_id = 0;
    Role role = Role::StaticText;
    Bounds bounds;
    std::wstring name;
    std::wstring value;
    std::wstring description;
    std::wstring default_action;
    std::wstring keyboard_shortcut;
    int command = 0;
    unsigned state = StateNone;
};

[[nodiscard]] std::vector<Element> BuildPopupElements(
    const UsageSnapshot& snapshot,
    bool chinese,
    bool identity_hidden,
    RefreshPhase refresh_phase,
    ui::CopySummaryState copy_state,
    ExternalActionFeedback external_feedback,
    ui::PopupAction focused,
    ui::PopupAction pressed,
    bool refresh_queued = false);

[[nodiscard]] inline std::vector<Element> BuildPopupElements(
    const UsageSnapshot& snapshot,
    bool chinese,
    bool identity_hidden,
    RefreshPhase refresh_phase,
    ui::CopySummaryState copy_state,
    ui::PopupAction focused,
    ui::PopupAction pressed) {
    return BuildPopupElements(snapshot, chinese, identity_hidden, refresh_phase, copy_state, {}, focused, pressed, false);
}

[[nodiscard]] inline std::vector<Element> BuildPopupElements(
    const UsageSnapshot& snapshot,
    bool chinese,
    bool identity_hidden,
    bool refreshing,
    ui::CopySummaryState copy_state,
    ExternalActionFeedback external_feedback,
    ui::PopupAction focused,
    ui::PopupAction pressed,
    bool refresh_queued = false) {
    return BuildPopupElements(snapshot, chinese, identity_hidden,
        refreshing ? RefreshPhase::FetchingUsage : RefreshPhase::Idle,
        copy_state, external_feedback, focused, pressed, refresh_queued);
}

[[nodiscard]] inline std::vector<Element> BuildPopupElements(
    const UsageSnapshot& snapshot,
    bool chinese,
    bool identity_hidden,
    bool refreshing,
    ui::CopySummaryState copy_state,
    ui::PopupAction focused,
    ui::PopupAction pressed) {
    return BuildPopupElements(snapshot, chinese, identity_hidden, refreshing, copy_state, {}, focused, pressed, false);
}

[[nodiscard]] std::vector<Element> BuildFloatBarElements(
    const UsageSnapshot& snapshot,
    bool chinese,
    bool identity_hidden,
    RefreshPhase refresh_phase,
    ui::FloatBarAction focused,
    ui::FloatBarAction pressed);

[[nodiscard]] inline std::vector<Element> BuildFloatBarElements(
    const UsageSnapshot& snapshot,
    bool chinese,
    bool identity_hidden,
    bool refreshing,
    ui::FloatBarAction focused,
    ui::FloatBarAction pressed) {
    return BuildFloatBarElements(snapshot, chinese, identity_hidden,
        refreshing ? RefreshPhase::FetchingUsage : RefreshPhase::Idle, focused, pressed);
}

[[nodiscard]] std::vector<Element> BuildSettingsElements(
    const AppSettings& settings,
    const UsageSnapshot& snapshot,
    const UpdateCheckState& update,
    SettingsPersistenceState persistence,
    ExternalActionFeedback external_feedback,
    RefreshPhase refresh_phase,
    ui::SettingsTab tab,
    bool chinese,
    ui::SettingsAction focused,
    ui::SettingsAction pressed,
    GlobalShortcutStatus global_shortcut_status = GlobalShortcutStatus::Registered);

[[nodiscard]] inline std::vector<Element> BuildSettingsElements(
    const AppSettings& settings,
    const UsageSnapshot& snapshot,
    const UpdateCheckState& update,
    SettingsPersistenceState persistence,
    ui::SettingsTab tab,
    bool chinese,
    ui::SettingsAction focused,
    ui::SettingsAction pressed) {
    return BuildSettingsElements(settings, snapshot, update, persistence, {}, RefreshPhase::Idle, tab, chinese, focused, pressed);
}

[[nodiscard]] inline std::vector<Element> BuildSettingsElements(
    const AppSettings& settings,
    const UsageSnapshot& snapshot,
    const UpdateCheckState& update,
    SettingsPersistenceState persistence,
    ExternalActionFeedback external_feedback,
    ui::SettingsTab tab,
    bool chinese,
    ui::SettingsAction focused,
    ui::SettingsAction pressed) {
    return BuildSettingsElements(settings, snapshot, update, persistence, external_feedback,
        RefreshPhase::Idle, tab, chinese, focused, pressed);
}

[[nodiscard]] long PopupChildId(ui::PopupAction action) noexcept;
[[nodiscard]] long FloatBarChildId(ui::FloatBarAction action) noexcept;
[[nodiscard]] long SettingsChildId(ui::SettingsTab tab, ui::SettingsAction action) noexcept;

}  // namespace codex_partner::accessibility
