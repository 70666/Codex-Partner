#include "interaction_model.h"

#include <array>
#include <span>

namespace codex_partner::ui {
namespace {

template <typename Action>
Action Step(std::span<const Action> actions, Action current, int direction) noexcept {
    if (actions.empty()) return Action::None;
    std::size_t index = actions.size();
    for (std::size_t candidate = 0; candidate < actions.size(); ++candidate) {
        if (actions[candidate] == current) {
            index = candidate;
            break;
        }
    }
    if (index == actions.size()) return direction < 0 ? actions.back() : actions.front();
    if (direction < 0) return actions[(index + actions.size() - 1) % actions.size()];
    return actions[(index + 1) % actions.size()];
}

constexpr std::array kPopupActions{PopupAction::CopySummary, PopupAction::Refresh, PopupAction::Settings, PopupAction::Primary};
constexpr std::array kFloatBarActions{FloatBarAction::OpenPopup, FloatBarAction::Hide};
constexpr std::array kGeneralActions{
    SettingsAction::SelectGeneral,
    SettingsAction::SelectProviders,
    SettingsAction::SelectNotifications,
    SettingsAction::SelectFloatBar,
    SettingsAction::SelectUsageSpend,
    SettingsAction::SelectAbout,
    SettingsAction::CycleLanguage,
    SettingsAction::CycleTheme,
    SettingsAction::ChooseGlobalShortcut,
    SettingsAction::CycleRefresh,
    SettingsAction::ToggleStartAtLogin,
    SettingsAction::ToggleStartMinimized,
    SettingsAction::TogglePrivacy,
};
constexpr std::array kPageWithFolderActions{
    SettingsAction::SelectGeneral,
    SettingsAction::SelectProviders,
    SettingsAction::SelectNotifications,
    SettingsAction::SelectFloatBar,
    SettingsAction::SelectUsageSpend,
    SettingsAction::SelectAbout,
    SettingsAction::OpenCodexFolder,
};
constexpr std::array kPageOnlyActions{
    SettingsAction::SelectGeneral,
    SettingsAction::SelectProviders,
    SettingsAction::SelectNotifications,
    SettingsAction::SelectFloatBar,
    SettingsAction::SelectUsageSpend,
    SettingsAction::SelectAbout,
};
constexpr std::array kProviderActions{
    SettingsAction::SelectGeneral,
    SettingsAction::SelectProviders,
    SettingsAction::SelectNotifications,
    SettingsAction::SelectFloatBar,
    SettingsAction::SelectUsageSpend,
    SettingsAction::SelectAbout,
    SettingsAction::LaunchCodexLogin,
    SettingsAction::OpenCodexFolder,
};
constexpr std::array kAboutActions{
    SettingsAction::SelectGeneral,
    SettingsAction::SelectProviders,
    SettingsAction::SelectNotifications,
    SettingsAction::SelectFloatBar,
    SettingsAction::SelectUsageSpend,
    SettingsAction::SelectAbout,
    SettingsAction::CheckForUpdates,
    SettingsAction::ReportIssue,
    SettingsAction::OpenProjectSite,
    SettingsAction::CopyDiagnostics,
};
constexpr std::array kNotificationActions{
    SettingsAction::SelectGeneral,
    SettingsAction::SelectProviders,
    SettingsAction::SelectNotifications,
    SettingsAction::SelectFloatBar,
    SettingsAction::SelectUsageSpend,
    SettingsAction::SelectAbout,
    SettingsAction::ToggleUsageNotifications,
    SettingsAction::ChooseUsageWarning,
    SettingsAction::ChooseNotificationSnooze,
    SettingsAction::TestNotification,
};
constexpr std::array kFloatBarSettingsActions{
    SettingsAction::SelectGeneral,
    SettingsAction::SelectProviders,
    SettingsAction::SelectNotifications,
    SettingsAction::SelectFloatBar,
    SettingsAction::SelectUsageSpend,
    SettingsAction::SelectAbout,
    SettingsAction::ToggleFloatBar,
    SettingsAction::ResetFloatBarPosition,
};

}  // namespace

PopupAction StepPopupAction(PopupAction current, int direction) noexcept {
    return Step<PopupAction>(kPopupActions, current, direction);
}

FloatBarAction StepFloatBarAction(FloatBarAction current, int direction) noexcept {
    return Step<FloatBarAction>(kFloatBarActions, current, direction);
}

SettingsAction StepSettingsAction(SettingsTab tab, SettingsAction current, int direction) noexcept {
    if (tab == SettingsTab::General) return Step<SettingsAction>(kGeneralActions, current, direction);
    if (tab == SettingsTab::Providers) return Step<SettingsAction>(kProviderActions, current, direction);
    if (tab == SettingsTab::Notifications) return Step<SettingsAction>(kNotificationActions, current, direction);
    if (tab == SettingsTab::FloatBar) return Step<SettingsAction>(kFloatBarSettingsActions, current, direction);
    if (tab == SettingsTab::UsageSpend) return Step<SettingsAction>(kPageOnlyActions, current, direction);
    if (tab == SettingsTab::About) return Step<SettingsAction>(kAboutActions, current, direction);
    return Step<SettingsAction>(kPageWithFolderActions, current, direction);
}

bool ShouldDismissPopupOnDeactivate(
    bool settings_visible,
    bool proof_mode,
    bool activation_handoff_pending) noexcept {
    return !settings_visible && !proof_mode && !activation_handoff_pending;
}

}  // namespace codex_partner::ui
