#pragma once

#include "settings_store.h"
#include "float_bar_model.h"
#include "usage_cache.h"
#include "usage_model.h"
#include "usage_provider.h"
#include "native_ui.h"
#include "notification_snooze.h"
#include "refresh_coordinator.h"
#include "resume_refresh_model.h"
#include "update_check.h"

#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

namespace codex_partner {

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    [[nodiscard]] bool Initialize(int show_command);
    [[nodiscard]] int Run();

private:
    static constexpr UINT kTrayMessage = WM_APP + 10;
    static constexpr UINT kUsageUpdated = WM_APP + 11;
    static constexpr UINT kActivateExisting = WM_APP + 12;
    static constexpr UINT kSpendUpdated = WM_APP + 13;
    static constexpr UINT kUpdateChecked = WM_APP + 14;
    static constexpr UINT_PTR kRefreshTimer = 1;
    static constexpr UINT_PTR kPressFeedbackTimer = 2;
    static constexpr UINT_PTR kRefreshAnimationTimer = 3;
    static constexpr UINT_PTR kHoverAnimationTimer = 4;
    static constexpr UINT_PTR kSavedFeedbackTimer = 5;
    static constexpr UINT_PTR kCopyFeedbackTimer = 6;
    static constexpr UINT_PTR kExternalFeedbackTimer = 7;
    static constexpr UINT_PTR kSnoozeStatusTimer = 8;
    static constexpr UINT_PTR kUsageChartAnimationTimer = 9;
    static constexpr UINT_PTR kTrayClickTimer = 10;
    static constexpr UINT_PTR kAmbientAnimationTimer = 11;
    static constexpr int kGlobalShortcutId = 1;

    static LRESULT CALLBACK PopupProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK SettingsProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK FloatBarProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT OnPopupMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT OnSettingsMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT OnFloatBarMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    [[nodiscard]] bool RegisterWindows();
    [[nodiscard]] bool CreateWindows();
    [[nodiscard]] bool AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayTooltip();
    void ShowTrayMenu(POINT point);
    void TogglePopup();
    void ShowPopup();
    void HidePopup();
    void ShowSettings();
    void HideSettings();
    void ShowFloatBar(bool activate = false);
    void HideFloatBar(bool persist = true);
    void ResetFloatBarPosition();
    void PersistFloatBarPosition();
    void ActivatePopupAction(ui::PopupAction action);
    void ActivateSettingsAction(ui::SettingsAction action);
    void ActivateFloatBarAction(ui::FloatBarAction action);
    void ShowLanguageMenu(HWND owner);
    void ShowThemeMenu(HWND owner);
    void ShowGlobalShortcutMenu(HWND owner);
    void ShowRefreshMenu(HWND owner);
    void ShowUsageWarningMenu(HWND owner);
    void ShowNotificationSnoozeMenu(HWND owner);
    void ApplyNotificationSnooze(NotificationSnoozePreset preset);
    void MaybeNotifyUsage(const UsageSnapshot& snapshot);
    void ShowUsageNotification(UsageAlertLevel level, bool test = false);
    void MaybeRefreshOnOpen();
    void RefreshAsync(bool retain_if_active = true);
    void StartRefreshCycle();
    [[nodiscard]] RefreshPhase CurrentRefreshPhase() const noexcept;
    void ApplyTheme(HWND window, std::optional<bool> active_override = std::nullopt);
    [[nodiscard]] ui::BackdropStyle BackdropStyleFor(HWND window) const noexcept;
    void SyncAmbientAnimationTimer();
    void TickAmbientAnimation();
    bool SaveSettings();
    void SyncSettingsPresentation();
    void ShowSettingsSaveFailure();
    void RestartRefreshTimer();
    void SyncNotificationSnoozeTimer();
    [[nodiscard]] bool BindGlobalShortcut(GlobalShortcut shortcut);
    void ChangeGlobalShortcut(GlobalShortcut shortcut);
    void HandleSystemResume();
    void LaunchCodexLogin();
    void OpenCodexFolder();
    void OpenProjectSite();
    void ReportIssue();
    void CheckForUpdates();
    void OpenAvailableRelease();
    [[nodiscard]] bool LaunchExternal(HWND owner, const wchar_t* target, const wchar_t* parameters = nullptr) const;
    void SetSettingsExternalFeedback(ExternalAction action, bool opened);
    [[nodiscard]] bool CopyUsageSummary();
    [[nodiscard]] bool CopyDiagnostics();
    [[nodiscard]] bool LoadProofSeed();
    void Quit();
    void SetPopupHover(ui::PopupAction action);
    void SetSettingsHover(ui::SettingsAction action);
    void SetPopupAccessibleFocus(ui::PopupAction action);
    void SetSettingsAccessibleFocus(ui::SettingsAction action);
    void SetFloatBarHover(ui::FloatBarAction action);
    void SetFloatBarAccessibleFocus(ui::FloatBarAction action);
    void TickPopupHoverAnimation();
    void TickSettingsHoverAnimation();
    void TickFloatBarHoverAnimation();
    [[nodiscard]] POINT LogicalPoint(HWND window, LPARAM lparam) const noexcept;
    [[nodiscard]] UsageSnapshot SnapshotCopy() const;

    HINSTANCE instance_ = nullptr;
    HWND popup_ = nullptr;
    HWND settings_window_ = nullptr;
    HWND float_bar_window_ = nullptr;
    HANDLE singleton_ = nullptr;
    HANDLE activation_event_ = nullptr;
    NOTIFYICONDATAW tray_{};
    ULONG_PTR gdiplus_token_ = 0;
    UINT taskbar_created_message_ = 0;
    SettingsStore settings_store_;
    AppSettings settings_;
    AppSettings persisted_settings_;
    UsageCache usage_cache_;
    ProviderRegistry providers_;
    mutable std::mutex snapshot_mutex_;
    UsageSnapshot snapshot_;
    RefreshCoordinator refresh_coordinator_;
    std::atomic_bool usage_refreshing_ = false;
    std::atomic_bool spend_refreshing_ = false;
    std::optional<std::chrono::steady_clock::time_point> last_refresh_started_;
    std::optional<std::chrono::steady_clock::time_point> last_resume_refresh_;
    std::jthread refresh_thread_;
    std::jthread activation_thread_;
    std::jthread update_thread_;
    std::mutex update_mutex_;
    std::optional<UpdateCheckState> pending_update_check_;
    UpdateCheckState update_check_;
    bool tray_added_ = false;
    bool tray_click_pending_ = false;
    bool tray_double_click_suppressed_ = false;
    SettingsPersistenceState settings_persistence_ = SettingsPersistenceState::Idle;
    ui::CopySummaryState usage_summary_copy_state_ = ui::CopySummaryState::Idle;
    ExternalActionFeedback popup_external_feedback_;
    ExternalActionFeedback settings_external_feedback_;
    bool diagnostics_copied_ = false;
    bool proof_seeded_ = false;
    bool proof_mode_ = false;
    GlobalShortcut registered_global_shortcut_ = GlobalShortcut::Disabled;
    GlobalShortcutStatus global_shortcut_status_ = GlobalShortcutStatus::Disabled;
    bool popup_tracking_mouse_ = false;
    bool settings_tracking_mouse_ = false;
    bool float_bar_tracking_mouse_ = false;
    bool positioning_float_bar_ = false;
    bool float_bar_dragging_ = false;
    POINT float_bar_press_screen_{};
    ui::PopupAction popup_hover_ = ui::PopupAction::None;
    ui::PopupAction popup_pressed_ = ui::PopupAction::None;
    ui::PopupAction popup_accessible_focus_ = ui::PopupAction::None;
    ui::SettingsAction settings_hover_ = ui::SettingsAction::None;
    ui::SettingsAction settings_pressed_ = ui::SettingsAction::None;
    ui::SettingsAction settings_accessible_focus_ = ui::SettingsAction::None;
    ui::FloatBarAction float_bar_hover_ = ui::FloatBarAction::None;
    ui::FloatBarAction float_bar_pressed_ = ui::FloatBarAction::None;
    ui::FloatBarAction float_bar_accessible_focus_ = ui::FloatBarAction::None;
    ui::SettingsTab settings_tab_ = ui::SettingsTab::General;
    float popup_hover_progress_ = 0.0F;
    float settings_hover_progress_ = 0.0F;
    float float_bar_hover_progress_ = 0.0F;
    float refresh_angle_ = 0.0F;
    double ambient_animation_phase_ = 12.0;
    std::chrono::steady_clock::time_point ambient_animation_tick_ = std::chrono::steady_clock::now();
    ui::BackdropStyle popup_backdrop_ = ui::BackdropStyle::Solid;
    ui::BackdropStyle settings_backdrop_ = ui::BackdropStyle::Solid;
    ui::BackdropStyle float_bar_backdrop_ = ui::BackdropStyle::Solid;
    std::optional<std::size_t> usage_chart_hover_;
    float usage_chart_progress_ = 1.0F;
    bool popup_activation_handoff_pending_ = false;
    UsageAlertLevel last_alert_level_ = UsageAlertLevel::None;
};

}  // namespace codex_partner
