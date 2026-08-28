#pragma once

#include "settings_store.h"
#include "external_action.h"
#include "refresh_coordinator.h"
#include "update_check.h"
#include "usage_model.h"

#include <Windows.h>

namespace codex_partner::ui {

constexpr int kPopupWidth = 400;
constexpr int kPopupHeight = 520;
constexpr int kSettingsWidth = 700;
constexpr int kSettingsHeight = 720;
constexpr int kFloatBarWidth = 420;
constexpr int kFloatBarHeight = 76;
constexpr float kContentScale = 1.25F;
constexpr int kPopupWindowWidth = 500;
constexpr int kPopupWindowHeight = 650;
constexpr int kSettingsWindowWidth = 875;
constexpr int kSettingsWindowHeight = 900;
constexpr int kFloatBarWindowWidth = 525;
constexpr int kFloatBarWindowHeight = 95;

enum class PopupAction { None, CopySummary, Refresh, Settings, Close, Primary };
enum class CopySummaryState { Idle, Copied, Failed };
enum class FloatBarAction { None, OpenPopup, Hide };
enum class SettingsTab { General, Providers, Notifications, FloatBar, UsageSpend, About };
enum class BackdropStyle { Solid, AcrylicGlass };
enum class SettingsAction {
    None,
    SelectGeneral,
    SelectProviders,
    SelectNotifications,
    SelectFloatBar,
    SelectUsageSpend,
    SelectAbout,
    CycleLanguage,
    CycleTheme,
    ChooseGlobalShortcut,
    CycleRefresh,
    ToggleStartAtLogin,
    ToggleStartMinimized,
    TogglePrivacy,
    ToggleFloatBar,
    ResetFloatBarPosition,
    ToggleUsageNotifications,
    ChooseUsageWarning,
    ChooseNotificationSnooze,
    TestNotification,
    LaunchCodexLogin,
    OpenCodexFolder,
    CheckForUpdates,
    ReportIssue,
    OpenProjectSite,
    CopyDiagnostics,
    Close,
};

struct PopupLayout {
    float first_card_y = 134.0F;
    float second_card_y = -1.0F;
    float spend_y = 256.0F;
    float primary_y = 338.0F;
    int content_height = 400;
};

void PaintPopup(HWND window, HDC dc, const UsageSnapshot& snapshot, bool light, bool chinese, bool identity_hidden, RefreshPhase refresh_phase, CopySummaryState copy_state, ExternalActionFeedback external_feedback, PopupAction hovered, PopupAction pressed, float hover_progress, float refresh_angle, bool refresh_queued = false, GlobalShortcut global_shortcut = GlobalShortcut::CtrlShiftU, double ambient_phase = 12.0, BackdropStyle backdrop = BackdropStyle::Solid);
void PaintFloatBar(HWND window, HDC dc, const UsageSnapshot& snapshot, bool light, bool chinese, bool identity_hidden, RefreshPhase refresh_phase, FloatBarAction hovered, FloatBarAction pressed, float hover_progress, double ambient_phase = 12.0, BackdropStyle backdrop = BackdropStyle::Solid);
void PaintSettings(HWND window, HDC dc, const AppSettings& settings, const UsageSnapshot& snapshot, const UpdateCheckState& update, SettingsTab tab, bool light, bool chinese, SettingsPersistenceState persistence, bool diagnostics_copied, ExternalActionFeedback external_feedback, RefreshPhase refresh_phase, SettingsAction hovered, SettingsAction pressed, float hover_progress, GlobalShortcutStatus global_shortcut_status = GlobalShortcutStatus::Registered, std::optional<std::size_t> usage_chart_hover = std::nullopt, float usage_chart_progress = 1.0F, double ambient_phase = 12.0, BackdropStyle backdrop = BackdropStyle::Solid);
[[nodiscard]] inline PopupLayout ResolvePopupLayout(const UsageSnapshot& snapshot) noexcept {
    PopupLayout layout;
    const bool setup = NeedsProviderSetup(snapshot);
    const int card_count = setup ? 2 : static_cast<int>(snapshot.session.has_value()) +
        static_cast<int>(snapshot.weekly.has_value());
    if (card_count >= 2) {
        layout.second_card_y = 254.0F;
        layout.spend_y = 376.0F;
        layout.primary_y = 458.0F;
        layout.content_height = 520;
    } else if (card_count == 0) {
        layout.spend_y = 134.0F;
        layout.primary_y = 216.0F;
        layout.content_height = 278;
    }
    return layout;
}
[[nodiscard]] PopupAction HitTestPopup(POINT logical, float primary_y = 458.0F) noexcept;
[[nodiscard]] FloatBarAction HitTestFloatBar(POINT logical) noexcept;
[[nodiscard]] SettingsAction HitTestSettings(POINT logical, SettingsTab tab) noexcept;
[[nodiscard]] std::optional<std::size_t> HitTestUsageChart(POINT logical) noexcept;
[[nodiscard]] const wchar_t* PopupActionHint(PopupAction action, bool chinese,
    UsagePrimaryTarget primary_target = UsagePrimaryTarget::UsageAnalytics, bool refreshing = false,
    bool refresh_queued = false) noexcept;
[[nodiscard]] const wchar_t* FloatBarActionHint(FloatBarAction action, bool chinese) noexcept;
[[nodiscard]] const wchar_t* SettingsActionHint(SettingsAction action, bool chinese) noexcept;

}  // namespace codex_partner::ui
