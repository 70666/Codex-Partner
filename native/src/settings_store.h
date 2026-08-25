#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace codex_partner {

enum class ThemeMode { System, Light, Dark };
enum class LanguageMode { System, SimplifiedChinese, English };
enum class SettingsPersistenceState { Idle, Saved, Failed };
enum class GlobalShortcut { Disabled, CtrlShiftU, CtrlAltU, CtrlShiftSpace };
enum class GlobalShortcutStatus { Disabled, Registered, Unavailable, CandidateUnavailable };
inline constexpr int kUnsetWindowPosition = (-2147483647 - 1);

struct GlobalShortcutBinding {
    unsigned int modifiers = 0;
    unsigned int virtual_key = 0;
    bool operator==(const GlobalShortcutBinding&) const = default;
};

struct GlobalShortcutChangeResult {
    GlobalShortcut effective = GlobalShortcut::Disabled;
    GlobalShortcutStatus status = GlobalShortcutStatus::Disabled;
    bool should_persist = false;
};

[[nodiscard]] std::optional<GlobalShortcutBinding> BindingForGlobalShortcut(GlobalShortcut shortcut) noexcept;
[[nodiscard]] const wchar_t* GlobalShortcutLabel(GlobalShortcut shortcut) noexcept;
[[nodiscard]] const wchar_t* GlobalShortcutStorageValue(GlobalShortcut shortcut) noexcept;
[[nodiscard]] std::optional<GlobalShortcut> ParseGlobalShortcut(std::wstring_view value) noexcept;
[[nodiscard]] GlobalShortcutChangeResult ResolveGlobalShortcutChange(
    GlobalShortcut current,
    GlobalShortcut attempted,
    bool attempted_registration_succeeded,
    bool previous_registration_restored) noexcept;

struct AppSettings {
    ThemeMode theme = ThemeMode::System;
    LanguageMode language = LanguageMode::System;
    int refresh_minutes = 15;
    bool usage_notifications = true;
    int usage_warning_percent = 80;
    std::chrono::system_clock::time_point notification_snoozed_until{};
    bool start_at_login = false;
    bool start_minimized = true;
    bool hide_identity = true;
    GlobalShortcut global_shortcut = GlobalShortcut::CtrlShiftU;
    bool show_float_bar = false;
    int float_bar_x = kUnsetWindowPosition;
    int float_bar_y = kUnsetWindowPosition;
    bool operator==(const AppSettings&) const = default;
};

struct SettingsCommitResult {
    AppSettings effective;
    AppSettings persisted;
    SettingsPersistenceState feedback = SettingsPersistenceState::Idle;
};

[[nodiscard]] SettingsCommitResult ResolveSettingsCommit(
    const AppSettings& persisted,
    const AppSettings& attempted,
    bool save_succeeded) noexcept;

class SettingsStore {
public:
    SettingsStore();
    explicit SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

    [[nodiscard]] AppSettings Load() const;
    [[nodiscard]] bool Save(const AppSettings& settings) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] bool ApplyStartAtLogin(bool enabled, bool minimized) const;
    [[nodiscard]] bool IsStartAtLoginEnabled() const;

private:
    std::filesystem::path path_;
};

[[nodiscard]] bool ShouldUseLightTheme(ThemeMode mode) noexcept;
[[nodiscard]] bool ShouldUseChinese(LanguageMode mode) noexcept;

}  // namespace codex_partner
