#include "settings_store.h"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <optional>

namespace codex_partner {
namespace {

std::optional<std::filesystem::path> SettingsPathIn(REFKNOWNFOLDERID folder) {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(folder, KF_FLAG_CREATE, nullptr, &value))) return std::nullopt;
    std::filesystem::path directory(value);
    CoTaskMemFree(value);
    directory /= L"CodexPartner";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return std::nullopt;
    return directory / L"native-settings.ini";
}

std::filesystem::path SettingsPath() {
    if (auto roaming = SettingsPathIn(FOLDERID_RoamingAppData)) return *roaming;
    if (auto local = SettingsPathIn(FOLDERID_LocalAppData)) return *local;
    std::error_code error;
    auto temporary = std::filesystem::temp_directory_path(error);
    if (!error) {
        temporary /= L"CodexPartner";
        std::filesystem::create_directories(temporary, error);
        if (!error) return temporary / L"native-settings.ini";
    }
    return std::filesystem::current_path() / L"native-settings.ini";
}

std::wstring ReadText(const std::filesystem::path& path, const wchar_t* key, const wchar_t* fallback) {
    std::array<wchar_t, 64> buffer{};
    GetPrivateProfileStringW(L"CodexPartner", key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

bool WriteText(const std::filesystem::path& path, const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(L"CodexPartner", key, value.c_str(), path.c_str()) != FALSE;
}

int ReadWindowPosition(const std::filesystem::path& path, const wchar_t* key) {
    const std::wstring value = ReadText(path, key, L"unset");
    if (value == L"unset") return kUnsetWindowPosition;
    wchar_t* end = nullptr;
    const long long parsed = std::wcstoll(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != L'\0' || parsed < -100'000 || parsed > 100'000) {
        return kUnsetWindowPosition;
    }
    return static_cast<int>(parsed);
}

std::wstring WindowPositionText(int value) {
    return value == kUnsetWindowPosition ? L"unset" : std::to_wstring(value);
}

std::chrono::system_clock::time_point ReadSnoozeUntil(const std::filesystem::path& path) {
    const std::wstring value = ReadText(path, L"NotificationSnoozedUntil", L"0");
    wchar_t* end = nullptr;
    const long long seconds = std::wcstoll(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != L'\0' || seconds <= 0) return {};
    const auto now = std::chrono::system_clock::now();
    const auto now_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const auto maximum_seconds = now_seconds + std::chrono::hours{24 * 7}.count() * 60 * 60;
    if (seconds <= now_seconds || seconds > maximum_seconds) return {};
    return std::chrono::system_clock::time_point{std::chrono::seconds{seconds}};
}

std::wstring SnoozeUntilText(std::chrono::system_clock::time_point until) {
    const auto now = std::chrono::system_clock::now();
    if (until <= now || until > now + std::chrono::hours{24 * 7}) return L"0";
    return std::to_wstring(
        std::chrono::duration_cast<std::chrono::seconds>(until.time_since_epoch()).count());
}

}  // namespace

std::optional<GlobalShortcutBinding> BindingForGlobalShortcut(GlobalShortcut shortcut) noexcept {
    constexpr unsigned int repeat_safe = MOD_NOREPEAT;
    switch (shortcut) {
    case GlobalShortcut::Disabled: return std::nullopt;
    case GlobalShortcut::CtrlShiftU: return GlobalShortcutBinding{MOD_CONTROL | MOD_SHIFT | repeat_safe, 'U'};
    case GlobalShortcut::CtrlAltU: return GlobalShortcutBinding{MOD_CONTROL | MOD_ALT | repeat_safe, 'U'};
    case GlobalShortcut::CtrlShiftSpace: return GlobalShortcutBinding{MOD_CONTROL | MOD_SHIFT | repeat_safe, VK_SPACE};
    }
    return std::nullopt;
}

const wchar_t* GlobalShortcutLabel(GlobalShortcut shortcut) noexcept {
    switch (shortcut) {
    case GlobalShortcut::Disabled: return L"Off";
    case GlobalShortcut::CtrlShiftU: return L"Ctrl+Shift+U";
    case GlobalShortcut::CtrlAltU: return L"Ctrl+Alt+U";
    case GlobalShortcut::CtrlShiftSpace: return L"Ctrl+Shift+Space";
    }
    return L"Off";
}

const wchar_t* GlobalShortcutStorageValue(GlobalShortcut shortcut) noexcept {
    switch (shortcut) {
    case GlobalShortcut::Disabled: return L"disabled";
    case GlobalShortcut::CtrlShiftU: return L"ctrl-shift-u";
    case GlobalShortcut::CtrlAltU: return L"ctrl-alt-u";
    case GlobalShortcut::CtrlShiftSpace: return L"ctrl-shift-space";
    }
    return L"disabled";
}

std::optional<GlobalShortcut> ParseGlobalShortcut(std::wstring_view value) noexcept {
    if (value == L"disabled") return GlobalShortcut::Disabled;
    if (value == L"ctrl-shift-u") return GlobalShortcut::CtrlShiftU;
    if (value == L"ctrl-alt-u") return GlobalShortcut::CtrlAltU;
    if (value == L"ctrl-shift-space") return GlobalShortcut::CtrlShiftSpace;
    return std::nullopt;
}

GlobalShortcutChangeResult ResolveGlobalShortcutChange(GlobalShortcut current, GlobalShortcut attempted,
    bool attempted_registration_succeeded, bool previous_registration_restored) noexcept {
    if (attempted_registration_succeeded) {
        return {attempted,
            attempted == GlobalShortcut::Disabled ? GlobalShortcutStatus::Disabled : GlobalShortcutStatus::Registered,
            attempted != current};
    }
    return {current,
        previous_registration_restored ? GlobalShortcutStatus::CandidateUnavailable : GlobalShortcutStatus::Unavailable,
        false};
}

SettingsCommitResult ResolveSettingsCommit(const AppSettings& persisted, const AppSettings& attempted,
    bool save_succeeded) noexcept {
    if (save_succeeded) return {attempted, attempted, SettingsPersistenceState::Saved};
    return {persisted, persisted, SettingsPersistenceState::Failed};
}

SettingsStore::SettingsStore() : path_(SettingsPath()) {}

AppSettings SettingsStore::Load() const {
    AppSettings result;
    const std::wstring theme = ReadText(path_, L"Theme", L"system");
    result.theme = theme == L"light" ? ThemeMode::Light : theme == L"dark" ? ThemeMode::Dark : ThemeMode::System;
    const std::wstring language = ReadText(path_, L"Language", L"system");
    result.language = language == L"zh-CN" ? LanguageMode::SimplifiedChinese : language == L"en" ? LanguageMode::English : LanguageMode::System;
    result.refresh_minutes = std::clamp(GetPrivateProfileIntW(L"CodexPartner", L"RefreshMinutes", 15, path_.c_str()), 1U, 120U);
    result.usage_notifications = GetPrivateProfileIntW(L"CodexPartner", L"UsageNotifications", 1, path_.c_str()) != 0;
    const int warning = static_cast<int>(GetPrivateProfileIntW(L"CodexPartner", L"UsageWarningPercent", 80, path_.c_str()));
    result.usage_warning_percent = warning == 70 || warning == 90 ? warning : 80;
    result.notification_snoozed_until = ReadSnoozeUntil(path_);
    result.start_at_login = IsStartAtLoginEnabled();
    result.start_minimized = GetPrivateProfileIntW(L"CodexPartner", L"StartMinimized", 1, path_.c_str()) != 0;
    result.hide_identity = GetPrivateProfileIntW(L"CodexPartner", L"HideIdentity", 1, path_.c_str()) != 0;
    result.global_shortcut = ParseGlobalShortcut(ReadText(path_, L"GlobalShortcut", L"ctrl-shift-u"))
        .value_or(GlobalShortcut::CtrlShiftU);
    result.show_float_bar = GetPrivateProfileIntW(L"CodexPartner", L"ShowFloatBar", 0, path_.c_str()) != 0;
    result.float_bar_x = ReadWindowPosition(path_, L"FloatBarX");
    result.float_bar_y = ReadWindowPosition(path_, L"FloatBarY");
    return result;
}

bool SettingsStore::Save(const AppSettings& settings) const {
    std::filesystem::path temporary = path_;
    temporary += L".tmp";
    DeleteFileW(temporary.c_str());
    const wchar_t* theme = settings.theme == ThemeMode::Light ? L"light" : settings.theme == ThemeMode::Dark ? L"dark" : L"system";
    bool ok = WriteText(temporary, L"Theme", theme);
    const wchar_t* language = settings.language == LanguageMode::SimplifiedChinese ? L"zh-CN" : settings.language == LanguageMode::English ? L"en" : L"system";
    ok = WriteText(temporary, L"Language", language) && ok;
    ok = WriteText(temporary, L"RefreshMinutes", std::to_wstring(settings.refresh_minutes)) && ok;
    ok = WriteText(temporary, L"UsageNotifications", settings.usage_notifications ? L"1" : L"0") && ok;
    ok = WriteText(temporary, L"UsageWarningPercent", std::to_wstring(settings.usage_warning_percent)) && ok;
    ok = WriteText(temporary, L"NotificationSnoozedUntil",
        SnoozeUntilText(settings.notification_snoozed_until)) && ok;
    ok = WriteText(temporary, L"StartAtLogin", settings.start_at_login ? L"1" : L"0") && ok;
    ok = WriteText(temporary, L"StartMinimized", settings.start_minimized ? L"1" : L"0") && ok;
    ok = WriteText(temporary, L"HideIdentity", settings.hide_identity ? L"1" : L"0") && ok;
    ok = WriteText(temporary, L"GlobalShortcut", GlobalShortcutStorageValue(settings.global_shortcut)) && ok;
    ok = WriteText(temporary, L"ShowFloatBar", settings.show_float_bar ? L"1" : L"0") && ok;
    ok = WriteText(temporary, L"FloatBarX", WindowPositionText(settings.float_bar_x)) && ok;
    ok = WriteText(temporary, L"FloatBarY", WindowPositionText(settings.float_bar_y)) && ok;
    if (ok) WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());
    if (ok) ok = MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

bool SettingsStore::ApplyStartAtLogin(bool enabled, bool minimized) const {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    LSTATUS status = ERROR_SUCCESS;
    if (enabled) {
        std::array<wchar_t, 32768> executable{};
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size()) {
            RegCloseKey(key);
            return false;
        }
        std::wstring command = L"\"" + std::wstring(executable.data(), length) + L"\"";
        if (minimized) command += L" --minimized";
        status = RegSetValueExW(key, L"CodexPartner", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, L"CodexPartner");
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool SettingsStore::IsStartAtLoginEnabled() const {
    std::array<wchar_t, 32768> command{};
    DWORD size = static_cast<DWORD>(command.size() * sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"CodexPartner", RRF_RT_REG_SZ, nullptr, command.data(), &size) != ERROR_SUCCESS) return false;
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return false;
    const std::wstring expected = L"\"" + std::wstring(executable.data(), length) + L"\"";
    const std::wstring registered(command.data());
    return registered.size() >= expected.size() && _wcsnicmp(registered.c_str(), expected.c_str(), expected.size()) == 0;
}

bool ShouldUseLightTheme(ThemeMode mode) noexcept {
    if (mode == ThemeMode::Light) return true;
    if (mode == ThemeMode::Dark) return false;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) return false;
    return value != 0;
}

bool ShouldUseChinese(LanguageMode mode) noexcept {
    if (mode == LanguageMode::SimplifiedChinese) return true;
    if (mode == LanguageMode::English) return false;
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> locale{};
    const int length = GetUserDefaultLocaleName(locale.data(), static_cast<int>(locale.size()));
    return length > 2 && (locale[0] == L'z' || locale[0] == L'Z') && (locale[1] == L'h' || locale[1] == L'H');
}

}  // namespace codex_partner
