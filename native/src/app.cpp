#include "app.h"

#include "accessibility.h"
#include "accessibility_model.h"
#include "diagnostics.h"
#include "codex_provider.h"
#include "interaction_model.h"
#include "json.h"
#include "native_ui.h"
#include "resource.h"
#include "tray_icon_win32.h"
#include "usage_freshness.h"
#include "usage_summary.h"
#include "version.h"
#include "window_placement.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <string_view>
#include <vector>

namespace codex_partner {
namespace {

constexpr wchar_t kPopupClass[] = L"CodexPartner.Popup";
constexpr wchar_t kSettingsClass[] = L"CodexPartner.Settings";
constexpr wchar_t kFloatBarClass[] = L"CodexPartner.FloatBar";
constexpr wchar_t kActivationEventName[] = L"Local\\CodexPartner.Activate";
constexpr UINT kCommandOpen = 1001;
constexpr UINT kCommandRefresh = 1002;
constexpr UINT kCommandSettings = 1003;
constexpr UINT kCommandExit = 1004;
constexpr UINT kCommandFloatBar = 1005;
constexpr UINT kCommandCopySummary = 1006;
constexpr UINT kLanguageSystem = 2101;
constexpr UINT kLanguageChinese = 2102;
constexpr UINT kLanguageEnglish = 2103;
constexpr UINT kThemeSystem = 2201;
constexpr UINT kThemeLight = 2202;
constexpr UINT kThemeDark = 2203;
constexpr UINT kRefreshFive = 2301;
constexpr UINT kRefreshFifteen = 2302;
constexpr UINT kRefreshThirty = 2303;
constexpr UINT kWarningSeventy = 2401;
constexpr UINT kWarningEighty = 2402;
constexpr UINT kWarningNinety = 2403;
constexpr UINT kSnoozeActive = 2501;
constexpr UINT kSnoozeOneHour = 2502;
constexpr UINT kSnoozeFourHours = 2503;
constexpr UINT kSnoozeTwentyFourHours = 2504;
constexpr UINT kShortcutDisabled = 2601;
constexpr UINT kShortcutCtrlShiftU = 2602;
constexpr UINT kShortcutCtrlAltU = 2603;
constexpr UINT kShortcutCtrlShiftSpace = 2604;

UINT EffectiveDpiForMonitor(HMONITOR monitor) noexcept {
    UINT dpi_x = 0;
    UINT dpi_y = 0;
    if (monitor && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)) && dpi_x > 0) {
        return dpi_x;
    }
    return std::max(96U, GetDpiForSystem());
}

windowing::PixelRect Pixels(RECT rect) noexcept {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

void SetRoundedCorners(HWND window) {
    constexpr DWORD corner_preference = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(window, 33, &corner_preference, sizeof(corner_preference));
}

std::wstring EnvironmentValue(const wchar_t* name) {
    std::wstring result(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, result.data(), static_cast<DWORD>(result.size()));
    if (length == 0 || length >= result.size()) return {};
    result.resize(length);
    return result;
}

bool CopyTextToClipboard(HWND owner, std::wstring_view text) {
    if (text.empty()) return false;
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return false;
    auto* destination = static_cast<wchar_t*>(GlobalLock(memory));
    if (!destination) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(destination, text.data(), text.size() * sizeof(wchar_t));
    destination[text.size()] = L'\0';
    GlobalUnlock(memory);
    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return false;
    }
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

POINT SelectorMenuAnchor(HWND owner, int logical_y) {
    const UINT dpi = GetDpiForWindow(owner);
    POINT point{MulDiv(static_cast<int>(648.0F * ui::kContentScale), static_cast<int>(dpi), 96),
        MulDiv(static_cast<int>(static_cast<float>(logical_y) * ui::kContentScale), static_cast<int>(dpi), 96)};
    ClientToScreen(owner, &point);
    return point;
}

UINT ShowRadioMenu(HWND owner, HMENU menu, UINT first, UINT last, UINT selected, int logical_y) {
    CheckMenuRadioItem(menu, first, last, selected, MF_BYCOMMAND);
    const POINT anchor = SelectorMenuAnchor(owner, logical_y);
    SetForegroundWindow(owner);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, anchor.x, anchor.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return command;
}

void AppendNotificationSnoozeItems(HMENU menu, bool chinese, bool snoozed) {
    AppendMenuW(menu, MF_STRING, kSnoozeActive,
        snoozed ? (chinese ? L"立即恢复提醒" : L"Resume alerts now") :
            (chinese ? L"提醒正常开启" : L"Alerts active"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kSnoozeOneHour, chinese ? L"暂停 1 小时" : L"Pause for 1 hour");
    AppendMenuW(menu, MF_STRING, kSnoozeFourHours, chinese ? L"暂停 4 小时" : L"Pause for 4 hours");
    AppendMenuW(menu, MF_STRING, kSnoozeTwentyFourHours, chinese ? L"暂停 24 小时" : L"Pause for 24 hours");
    if (!snoozed) CheckMenuRadioItem(menu, kSnoozeActive, kSnoozeActive, kSnoozeActive, MF_BYCOMMAND);
}

SIZE WindowSizeForClient(int client_width, int client_height, DWORD style, DWORD extended_style, UINT dpi) {
    RECT rect{0, 0, client_width, client_height};
    if (!AdjustWindowRectExForDpi(&rect, style, FALSE, extended_style, dpi)) {
        return {client_width, client_height};
    }
    return {rect.right - rect.left, rect.bottom - rect.top};
}

template <typename Painter>
void PaintDoubleBuffered(HWND window, HDC target, Painter&& painter) {
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    HDC buffer = CreateCompatibleDC(target);
    HBITMAP bitmap = width > 0 && height > 0 ? CreateCompatibleBitmap(target, width, height) : nullptr;
    if (!buffer || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        painter(target);
        return;
    }
    HGDIOBJ previous = SelectObject(buffer, bitmap);
    painter(buffer);
    BitBlt(target, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previous);
    DeleteObject(bitmap);
    DeleteDC(buffer);
}

bool AnimationsEnabled() noexcept {
    BOOL enabled = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) return true;
    return enabled != FALSE;
}

bool FindPngEncoder(CLSID& encoder) {
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || count == 0 || bytes == 0) return false;
    std::vector<BYTE> storage(bytes);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) return false;
    for (UINT index = 0; index < count; ++index) {
        if (codecs[index].MimeType && wcscmp(codecs[index].MimeType, L"image/png") == 0) {
            encoder = codecs[index].Clsid;
            return true;
        }
    }
    return false;
}

bool SaveClientProofPng(HWND window, const std::filesystem::path& path, std::wstring& failure) {
    failure.clear();
    if (!IsWindow(window) || path.empty()) {
        failure = L"invalid window or output path";
        return false;
    }
    RECT client{};
    if (!GetClientRect(window, &client)) {
        failure = L"GetClientRect failed";
        return false;
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        failure = L"client area is empty";
        return false;
    }
    HDC source = GetDC(window);
    HDC buffer = source ? CreateCompatibleDC(source) : nullptr;
    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = source ? CreateDIBSection(source, &bitmap_info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    if (!source || !buffer || !bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        if (source) ReleaseDC(window, source);
        failure = L"could not allocate the 32-bit proof surface";
        return false;
    }
    const HGDIOBJ previous = SelectObject(buffer, bitmap);
    SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(buffer), PRF_CLIENT | PRF_ERASEBKGND);
    SelectObject(buffer, previous);
    CLSID encoder{};
    bool saved = false;
    if (FindPngEncoder(encoder)) {
        Gdiplus::Bitmap image(bitmap, nullptr);
        const Gdiplus::Status status = image.Save(path.c_str(), &encoder, nullptr);
        saved = status == Gdiplus::Ok;
        if (!saved) failure = L"GDI+ PNG save status " + std::to_wstring(static_cast<int>(status));
    } else {
        failure = L"PNG encoder is unavailable";
    }
    DeleteObject(bitmap);
    DeleteDC(buffer);
    ReleaseDC(window, source);
    return saved;
}

void SaveRequestedProof(HWND window) {
    const std::wstring output = EnvironmentValue(L"CODEX_PARTNER_PROOF_OUTPUT");
    if (output.empty()) return;
    UpdateWindow(window);
    std::wstring failure;
    if (!SaveClientProofPng(window, std::filesystem::path(output), failure)) {
        OutputDebugStringW(L"[CodexPartner] Could not save proof screenshot\n");
        std::wofstream report(std::filesystem::path(output + L".error.txt"));
        if (report) report << failure << L'\n';
    }
}

void SaveRequestedTrayIconProof(const UsageSnapshot& snapshot) {
    const std::wstring output = EnvironmentValue(L"CODEX_PARTNER_PROOF_TRAY_OUTPUT");
    if (output.empty()) return;
    constexpr UINT scale = 8;
    constexpr UINT inset = 16;
    constexpr UINT canvas_size = kTrayIconSize * scale + inset * 2;
    constexpr std::uint8_t proof_background = 238;
    const TrayIconPixels pixels = RenderTrayIconRgba(BuildTrayIconModel(snapshot));
    Gdiplus::Bitmap image(canvas_size, canvas_size, PixelFormat32bppARGB);
    for (UINT y = 0; y < canvas_size; ++y) {
        for (UINT x = 0; x < canvas_size; ++x) {
            image.SetPixel(x, y, Gdiplus::Color(255, proof_background, proof_background, proof_background));
        }
    }
    for (std::uint32_t y = 0; y < kTrayIconSize; ++y) {
        for (std::uint32_t x = 0; x < kTrayIconSize; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * kTrayIconSize + x) * 4;
            const unsigned alpha = pixels[offset + 3];
            const auto composite = [&](std::uint8_t channel) {
                return static_cast<std::uint8_t>((static_cast<unsigned>(channel) * alpha +
                    static_cast<unsigned>(proof_background) * (255U - alpha)) / 255U);
            };
            const Gdiplus::Color color(255, composite(pixels[offset]), composite(pixels[offset + 1]),
                composite(pixels[offset + 2]));
            for (UINT pixel_y = 0; pixel_y < scale; ++pixel_y) {
                for (UINT pixel_x = 0; pixel_x < scale; ++pixel_x) {
                    image.SetPixel(inset + x * scale + pixel_x, inset + y * scale + pixel_y, color);
                }
            }
        }
    }
    CLSID encoder{};
    const bool saved = FindPngEncoder(encoder) &&
        image.Save(std::filesystem::path(output).c_str(), &encoder, nullptr) == Gdiplus::Ok;
    if (!saved) {
        OutputDebugStringW(L"[CodexPartner] Could not save tray icon proof\n");
        std::wofstream report(std::filesystem::path(output + L".error.txt"));
        if (report) report << L"could not encode tray icon proof as PNG\n";
    }
}

}  // namespace

App::App(HINSTANCE instance)
    : instance_(instance), settings_(settings_store_.Load()), persisted_settings_(settings_) {
    if (auto cached = usage_cache_.Load()) snapshot_ = std::move(*cached);
    providers_.Add(std::make_unique<CodexProvider>());
}

App::~App() {
    if (activation_thread_.joinable()) {
        activation_thread_.request_stop();
        if (activation_event_) SetEvent(activation_event_);
        activation_thread_.join();
    }
    if (refresh_thread_.joinable()) {
        refresh_thread_.request_stop();
        refresh_thread_.join();
    }
    if (update_thread_.joinable()) {
        update_thread_.request_stop();
        update_thread_.join();
    }
    if (popup_ && registered_global_shortcut_ != GlobalShortcut::Disabled) {
        UnregisterHotKey(popup_, kGlobalShortcutId);
        registered_global_shortcut_ = GlobalShortcut::Disabled;
    }
    RemoveTrayIcon();
    if (singleton_) CloseHandle(singleton_);
    if (activation_event_) CloseHandle(activation_event_);
    if (gdiplus_token_) Gdiplus::GdiplusShutdown(gdiplus_token_);
}

bool App::Initialize(int show_command) {
    OutputDebugStringW(L"[CodexPartner] Initialize begin\n");
    // Create/open this before the mutex. If two processes start together, an
    // activation signal remains set until the primary installs its waiter.
    activation_event_ = CreateEventW(nullptr, FALSE, FALSE, kActivationEventName);
    if (!activation_event_) return false;
    singleton_ = CreateMutexW(nullptr, TRUE, L"Local\\CodexPartner.Singleton");
    if (!singleton_) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        SetEvent(activation_event_);
        CloseHandle(singleton_);
        singleton_ = nullptr;
        return false;
    }

    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token_, &input, nullptr) != Gdiplus::Ok) return false;
    OutputDebugStringW(L"[CodexPartner] GDI+ ready\n");
    taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (!RegisterWindows() || !CreateWindows()) return false;
    activation_thread_ = std::jthread([this](std::stop_token stop) {
        while (!stop.stop_requested()) {
            const DWORD result = WaitForSingleObject(activation_event_, INFINITE);
            if (stop.stop_requested()) return;
            if (result != WAIT_OBJECT_0) return;
            if (popup_) PostMessageW(popup_, kActivateExisting, 0, 0);
        }
    });
    OutputDebugStringW(L"[CodexPartner] windows created\n");
    const std::wstring proof = EnvironmentValue(L"CODEX_PARTNER_PROOF_MODE");
    proof_mode_ = !proof.empty();
    if (!proof.empty()) {
        const std::wstring proof_language = EnvironmentValue(L"CODEX_PARTNER_PROOF_LANGUAGE");
        if (proof_language == L"zh-CN") settings_.language = LanguageMode::SimplifiedChinese;
        else if (proof_language == L"en") settings_.language = LanguageMode::English;
        const std::wstring proof_theme = EnvironmentValue(L"CODEX_PARTNER_PROOF_THEME");
        if (proof_theme == L"light") settings_.theme = ThemeMode::Light;
        else if (proof_theme == L"dark") settings_.theme = ThemeMode::Dark;
        if (proof_theme == L"light" || proof_theme == L"dark") {
            ApplyTheme(popup_);
            ApplyTheme(settings_window_);
            ApplyTheme(float_bar_window_);
        }
        const std::wstring proof_privacy = EnvironmentValue(L"CODEX_PARTNER_PROOF_PRIVACY");
        if (proof_privacy == L"hidden") settings_.hide_identity = true;
        else if (proof_privacy == L"visible") settings_.hide_identity = false;
        const std::wstring proof_snooze = EnvironmentValue(L"CODEX_PARTNER_PROOF_SNOOZE");
        if (proof_snooze == L"1h") {
            settings_.notification_snoozed_until = ResolveNotificationSnooze(NotificationSnoozePreset::OneHour);
        } else if (proof_snooze == L"4h") {
            settings_.notification_snoozed_until = ResolveNotificationSnooze(NotificationSnoozePreset::FourHours);
        } else if (proof_snooze == L"24h") {
            settings_.notification_snoozed_until = ResolveNotificationSnooze(NotificationSnoozePreset::TwentyFourHours);
        } else if (proof_snooze == L"active") {
            settings_.notification_snoozed_until = {};
        }
        if (proof == L"settings:about") {
            const std::wstring proof_update = EnvironmentValue(L"CODEX_PARTNER_PROOF_UPDATE");
            if (proof_update == L"available") {
                update_check_ = {UpdateCheckStatus::Available, L"v99.0.0",
                    L"https://github.com/70666/Codex-Partner/releases/tag/v99.0.0",
                    L"https://github.com/70666/Codex-Partner/releases/download/v99.0.0/Codex-Partner-99.0.0-native-windows-x64.exe"};
            } else if (proof_update == L"available-fallback") {
                update_check_ = {UpdateCheckStatus::Available, L"v99.0.0",
                    L"https://github.com/70666/Codex-Partner/releases/tag/v99.0.0", {}};
            } else if (proof_update == L"current") {
                update_check_ = {UpdateCheckStatus::UpToDate, L"v" CODEX_PARTNER_VERSION_WIDE,
                    L"https://github.com/70666/Codex-Partner/releases/tag/v" CODEX_PARTNER_VERSION_WIDE, {}};
            } else if (proof_update == L"failed") {
                update_check_ = {UpdateCheckStatus::Failed, {}, {}, {}};
            }
        }
        if (proof.starts_with(L"settings")) {
            const std::wstring proof_settings = EnvironmentValue(L"CODEX_PARTNER_PROOF_SETTINGS");
            if (proof_settings == L"saved") settings_persistence_ = SettingsPersistenceState::Saved;
            else if (proof_settings == L"failed") settings_persistence_ = SettingsPersistenceState::Failed;
        }
        if (proof.starts_with(L"popup")) {
            const std::wstring proof_copy = EnvironmentValue(L"CODEX_PARTNER_PROOF_COPY");
            if (proof_copy == L"copied") usage_summary_copy_state_ = ui::CopySummaryState::Copied;
            else if (proof_copy == L"failed") usage_summary_copy_state_ = ui::CopySummaryState::Failed;
        }
        const std::wstring proof_external = EnvironmentValue(L"CODEX_PARTNER_PROOF_EXTERNAL");
        {
            const bool opened = proof_external.ends_with(L"-opened");
            const ExternalActionOutcome outcome = opened ? ExternalActionOutcome::Opened : ExternalActionOutcome::Failed;
            if (proof_external == L"login-failed" || proof_external == L"login-opened") {
                settings_external_feedback_ = {ExternalAction::CodexLogin, outcome};
            } else if (proof_external == L"folder-failed" || proof_external == L"folder-opened") {
                settings_external_feedback_ = {ExternalAction::CodexFolder, outcome};
            } else if (proof_external == L"project-failed" || proof_external == L"project-opened") {
                settings_external_feedback_ = {ExternalAction::ProjectSite, outcome};
            } else if (proof_external == L"issue-opened") {
                diagnostics_copied_ = true;
                settings_external_feedback_ = {ExternalAction::IssuePage, ExternalActionOutcome::Opened, true, true};
            } else if (proof_external == L"issue-copy-failed") {
                settings_external_feedback_ = {ExternalAction::IssuePage, ExternalActionOutcome::Opened, true, false};
            } else if (proof_external == L"issue-open-failed") {
                diagnostics_copied_ = true;
                settings_external_feedback_ = {ExternalAction::IssuePage, ExternalActionOutcome::Failed, true, true};
            } else if (proof_external == L"issue-failed") {
                settings_external_feedback_ = {ExternalAction::IssuePage, ExternalActionOutcome::Failed, true, false};
            } else if (proof_external == L"release-failed" || proof_external == L"release-opened") {
                settings_external_feedback_ = {ExternalAction::ReleasePage, outcome};
            } else if (proof_external == L"update-download-failed" || proof_external == L"update-download-opened") {
                settings_external_feedback_ = {ExternalAction::NativeUpdateDownload, outcome};
            }
        }
    }
    const std::wstring proof_hotkey = EnvironmentValue(L"CODEX_PARTNER_PROOF_HOTKEY");
    if (proof_mode_) {
        if (proof_hotkey == L"disabled") {
            settings_.global_shortcut = GlobalShortcut::Disabled;
            registered_global_shortcut_ = GlobalShortcut::Disabled;
            global_shortcut_status_ = GlobalShortcutStatus::Disabled;
        } else if (proof_hotkey == L"unavailable") {
            registered_global_shortcut_ = GlobalShortcut::Disabled;
            global_shortcut_status_ = GlobalShortcutStatus::Unavailable;
        } else {
            registered_global_shortcut_ = settings_.global_shortcut;
            global_shortcut_status_ = proof_hotkey == L"candidate-unavailable" ?
                GlobalShortcutStatus::CandidateUnavailable :
                (settings_.global_shortcut == GlobalShortcut::Disabled ?
                    GlobalShortcutStatus::Disabled : GlobalShortcutStatus::Registered);
        }
    } else {
        (void)BindGlobalShortcut(settings_.global_shortcut);
    }
    if (!AddTrayIcon()) {
        if (proof.empty()) {
            MessageBoxW(nullptr,
                ShouldUseChinese(settings_.language) ? L"无法在 Windows 通知区域创建 Codex Partner 图标。请重启 Windows 资源管理器后再试。" :
                    L"Codex Partner could not create its notification-area icon. Restart Windows Explorer and try again.",
                L"Codex Partner", MB_OK | MB_ICONERROR);
            return false;
        }
        OutputDebugStringW(L"[CodexPartner] Proof mode continuing without a notification-area icon\n");
    }
    RestartRefreshTimer();
    proof_seeded_ = LoadProofSeed();
    if (proof_seeded_ && EnvironmentValue(L"CODEX_PARTNER_PROOF_SPEND") == L"partial") {
        std::scoped_lock lock(snapshot_mutex_);
        if (!snapshot_.spend) snapshot_.spend = SpendSummary{};
        SpendSummary& spend = *snapshot_.spend;
        spend.one_day_usd = 2'748.9520;
        spend.seven_day_usd = 3'796.5744;
        spend.thirty_day_usd = 5'196.2713;
        spend.files_scanned = 86;
        spend.priced_events = 55'741;
        spend.unpriced_events = 4'275;
        spend.priced_input_tokens = 8'027'876'807ULL;
        spend.priced_cached_input_tokens = 7'667'416'576ULL;
        spend.priced_cache_write_input_tokens = 121'600'000ULL;
        spend.priced_output_tokens = 25'292'185ULL;
        spend.unpriced_input_tokens = 583'557'218ULL;
        spend.unpriced_cached_input_tokens = 541'368'576ULL;
        spend.unpriced_cache_write_input_tokens = 8'400'000ULL;
        spend.unpriced_output_tokens = 347'435ULL;
        spend.unpriced_models = {"codex-auto-review"};
        spend.one_day_partial = true;
        spend.seven_day_partial = true;
        spend.thirty_day_partial = true;
        spend.partial = true;
    }
    if (proof_seeded_) {
        const std::wstring proof_refresh = EnvironmentValue(L"CODEX_PARTNER_PROOF_REFRESH");
        if (proof_refresh == L"queued") {
            (void)refresh_coordinator_.Request();
            (void)refresh_coordinator_.Request();
            usage_refreshing_ = true;
            spend_refreshing_ = true;
        } else if (proof_refresh == L"spend") {
            usage_refreshing_ = false;
            spend_refreshing_ = true;
        }
    }
    if (proof_seeded_ && EnvironmentValue(L"CODEX_PARTNER_PROOF_RESUME") == L"old") {
        {
            std::scoped_lock lock(snapshot_mutex_);
            snapshot_.updated_at = std::chrono::system_clock::now() - std::chrono::hours{6};
            snapshot_.stale = false;
            snapshot_.error.clear();
        }
        SendMessageW(popup_, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
    }
    UpdateTrayTooltip();
    if (!proof.empty()) SaveRequestedTrayIconProof(SnapshotCopy());
    if (!proof_seeded_) RefreshAsync();
    OutputDebugStringW(L"[CodexPartner] data initialized\n");

    const bool minimized_argument = wcsstr(GetCommandLineW(), L"--minimized") != nullptr;
    if (proof.starts_with(L"settings")) {
        if (proof == L"settings:providers") settings_tab_ = ui::SettingsTab::Providers;
        else if (proof == L"settings:notifications") settings_tab_ = ui::SettingsTab::Notifications;
        else if (proof == L"settings:floatbar") settings_tab_ = ui::SettingsTab::FloatBar;
        else if (proof == L"settings:usageSpend") settings_tab_ = ui::SettingsTab::UsageSpend;
        else if (proof == L"settings:about") settings_tab_ = ui::SettingsTab::About;
        else settings_tab_ = ui::SettingsTab::General;
        ShowSettings();
    }
    else if (proof.starts_with(L"floatbar")) ShowFloatBar(true);
    else if (proof.starts_with(L"popup") || (!minimized_argument && show_command != SW_HIDE)) ShowPopup();
    if (settings_.show_float_bar && proof.empty()) ShowFloatBar(false);
    if (!proof.empty() && proof != L"handoff") {
        HWND proof_window = proof.starts_with(L"settings") ? settings_window_ :
            proof.starts_with(L"floatbar") ? float_bar_window_ : popup_;
        SaveRequestedProof(proof_window);
    }
    OutputDebugStringW(L"[CodexPartner] Initialize complete\n");
    return true;
}

int App::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool App::RegisterWindows() {
    WNDCLASSEXW popup_class{sizeof(WNDCLASSEXW)};
    popup_class.style = CS_HREDRAW | CS_VREDRAW;
    popup_class.lpfnWndProc = PopupProc;
    popup_class.hInstance = instance_;
    popup_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    popup_class.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_CODEX_PARTNER));
    popup_class.hbrBackground = nullptr;
    popup_class.lpszClassName = kPopupClass;
    if (!RegisterClassExW(&popup_class)) return false;

    WNDCLASSEXW settings_class = popup_class;
    settings_class.lpfnWndProc = SettingsProc;
    settings_class.lpszClassName = kSettingsClass;
    if (!RegisterClassExW(&settings_class)) return false;

    WNDCLASSEXW float_bar_class = popup_class;
    float_bar_class.lpfnWndProc = FloatBarProc;
    float_bar_class.lpszClassName = kFloatBarClass;
    return RegisterClassExW(&float_bar_class) != 0;
}

bool App::CreateWindows() {
    const UINT dpi = GetDpiForSystem();
    const std::wstring proof_surface = EnvironmentValue(L"CODEX_PARTNER_PROOF_MODE");
    const bool proof_mode = !proof_surface.empty();
    const DWORD popup_extended_style = WS_EX_TOPMOST | (proof_mode ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW);
    // CUA ignores tool-style popup windows. In proof mode expose the same
    // client in a conventional top-level window. Size the outer frame around
    // the requested client so proof never clips edge controls or feedback.
    const DWORD popup_style = proof_surface.starts_with(L"popup") ?
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE : WS_POPUP;
    const SIZE popup_size = WindowSizeForClient(
        MulDiv(ui::kPopupWindowWidth, static_cast<int>(dpi), 96),
        MulDiv(ui::kPopupWindowHeight, static_cast<int>(dpi), 96),
        popup_style, popup_extended_style, dpi);
    popup_ = CreateWindowExW(popup_extended_style, kPopupClass, L"Codex Partner", popup_style, CW_USEDEFAULT, CW_USEDEFAULT,
        popup_size.cx, popup_size.cy, nullptr, nullptr, instance_, this);
    if (!popup_) return false;
    SetRoundedCorners(popup_);

    RECT settings_rect{0, 0, MulDiv(ui::kSettingsWindowWidth, static_cast<int>(dpi), 96), MulDiv(ui::kSettingsWindowHeight, static_cast<int>(dpi), 96)};
    AdjustWindowRectExForDpi(&settings_rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0, dpi);
    const DWORD settings_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | (proof_surface.starts_with(L"settings") ? WS_VISIBLE : 0U);
    settings_window_ = CreateWindowExW(0, kSettingsClass, ShouldUseChinese(settings_.language) ? L"Codex Partner 设置" : L"Codex Partner Settings", settings_style, CW_USEDEFAULT, CW_USEDEFAULT, settings_rect.right - settings_rect.left, settings_rect.bottom - settings_rect.top, nullptr, nullptr, instance_, this);
    if (!settings_window_) return false;

    const int float_bar_width = MulDiv(ui::kFloatBarWindowWidth, static_cast<int>(dpi), 96);
    const int float_bar_height = MulDiv(ui::kFloatBarWindowHeight, static_cast<int>(dpi), 96);
    const DWORD float_bar_extended_style = WS_EX_TOPMOST | (proof_surface.starts_with(L"floatbar") ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW);
    const DWORD float_bar_style = WS_POPUP | (proof_surface.starts_with(L"floatbar") ? WS_VISIBLE : 0U);
    float_bar_window_ = CreateWindowExW(float_bar_extended_style, kFloatBarClass,
        ShouldUseChinese(settings_.language) ? L"Codex Partner 浮动用量条" : L"Codex Partner Floating Usage Bar",
        float_bar_style, CW_USEDEFAULT, CW_USEDEFAULT, float_bar_width, float_bar_height,
        nullptr, nullptr, instance_, this);
    if (!float_bar_window_) return false;
    SetRoundedCorners(settings_window_);
    SetRoundedCorners(float_bar_window_);
    ApplyTheme(popup_);
    ApplyTheme(settings_window_);
    ApplyTheme(float_bar_window_);
    return true;
}

bool App::AddTrayIcon() {
    const HICON previous_dynamic = dynamic_tray_icon_;
    HICON next_dynamic = CreateUsageTrayIconHandle(SnapshotCopy());
    tray_ = {};
    tray_.cbSize = sizeof(tray_);
    tray_.hWnd = popup_;
    tray_.uID = 1;
    tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    tray_.uCallbackMessage = kTrayMessage;
    tray_.hIcon = next_dynamic ? next_dynamic : LoadIconW(instance_, MAKEINTRESOURCEW(IDI_CODEX_PARTNER));
    wcscpy_s(tray_.szTip, ShouldUseChinese(settings_.language) ? L"Codex Partner - 正在检查使用情况" : L"Codex Partner - checking usage");
    tray_added_ = Shell_NotifyIconW(NIM_ADD, &tray_) != FALSE;
    if (!tray_added_) {
        if (next_dynamic) DestroyIcon(next_dynamic);
        tray_.hIcon = previous_dynamic;
        return false;
    }
    dynamic_tray_icon_ = next_dynamic;
    if (previous_dynamic && previous_dynamic != dynamic_tray_icon_) DestroyIcon(previous_dynamic);
    tray_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &tray_);
    return true;
}

void App::RemoveTrayIcon() {
    if (tray_added_) Shell_NotifyIconW(NIM_DELETE, &tray_);
    tray_added_ = false;
    if (dynamic_tray_icon_) DestroyIcon(dynamic_tray_icon_);
    dynamic_tray_icon_ = nullptr;
    tray_.hIcon = nullptr;
}

void App::UpdateTrayTooltip() {
    const UsageSnapshot snapshot = SnapshotCopy();
    std::wstring tip = L"Codex Partner";
    const bool chinese = ShouldUseChinese(settings_.language);
    const RefreshPhase refresh_phase = CurrentRefreshPhase();
    if (refresh_phase == RefreshPhase::FetchingUsage) tip += chinese ? L" - 正在刷新额度" : L" - refreshing limits";
    else if (refresh_phase == RefreshPhase::ScanningSpend) tip += chinese ? L" - 额度已更新 · 正在扫描本地费用" : L" - limits ready · scanning local spend";
    else if (NeedsProviderSetup(snapshot)) tip += chinese ? L" - 需要登录 Codex" : L" - Codex sign-in required";
    else if (snapshot.stale && (snapshot.session || snapshot.weekly)) tip += L" - " + std::to_wstring(static_cast<int>(MostConstrainedPercent(snapshot))) + (chinese ? L"% 已使用（上次数据）" : L"% used (last known)");
    else if (!snapshot.error.empty()) tip += chinese ? L" - 使用情况不可用" : L" - usage unavailable";
    else {
        tip += L" - " + std::to_wstring(static_cast<int>(MostConstrainedPercent(snapshot))) + (chinese ? L"% 已使用" : L"% used");
        if (MostUrgentPaceForecast(snapshot)) tip += chinese ? L" · 节奏偏快" : L" · pace risk";
    }
    tip += L" · " + FormatUsageFreshness(snapshot.updated_at, chinese);
    wcsncpy_s(tray_.szTip, tip.c_str(), _TRUNCATE);
    HICON next_dynamic = CreateUsageTrayIconHandle(snapshot);
    const HICON previous_dynamic = dynamic_tray_icon_;
    tray_.uFlags = NIF_TIP | (next_dynamic ? NIF_ICON : 0U);
    if (next_dynamic) tray_.hIcon = next_dynamic;
    const bool modified = tray_added_ && Shell_NotifyIconW(NIM_MODIFY, &tray_) != FALSE;
    if (next_dynamic && modified) {
        dynamic_tray_icon_ = next_dynamic;
        if (previous_dynamic && previous_dynamic != dynamic_tray_icon_) DestroyIcon(previous_dynamic);
    } else if (next_dynamic) {
        DestroyIcon(next_dynamic);
        tray_.hIcon = previous_dynamic ? previous_dynamic : LoadIconW(instance_, MAKEINTRESOURCEW(IDI_CODEX_PARTNER));
    }
}

void App::ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const bool chinese = ShouldUseChinese(settings_.language);
    std::wstring open_label = chinese ? L"打开 Codex Partner" : L"Open Codex Partner";
    if (registered_global_shortcut_ != GlobalShortcut::Disabled) {
        open_label += L"\t";
        open_label += GlobalShortcutLabel(registered_global_shortcut_);
    }
    AppendMenuW(menu, MF_STRING | MF_DEFAULT, kCommandOpen, open_label.c_str());
    AppendMenuW(menu, MF_STRING, kCommandRefresh, chinese ? L"刷新使用情况" : L"Refresh usage");
    AppendMenuW(menu, MF_STRING, kCommandCopySummary,
        chinese ? L"复制使用摘要\tCtrl+C" : L"Copy usage summary\tCtrl+C");
    const bool snoozed = IsNotificationSnoozed(settings_.notification_snoozed_until);
    if (settings_.usage_notifications) {
        HMENU snooze_menu = CreatePopupMenu();
        if (snooze_menu) {
            AppendNotificationSnoozeItems(snooze_menu, chinese, snoozed);
            const std::wstring label = snoozed ?
                FormatNotificationSnooze(settings_.notification_snoozed_until, chinese) :
                (chinese ? L"暂停提醒" : L"Pause alerts");
            if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(snooze_menu), label.c_str())) {
                DestroyMenu(snooze_menu);
            }
        }
    } else {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0,
            chinese ? L"提醒已在设置中关闭" : L"Alerts disabled in Settings");
    }
    AppendMenuW(menu, MF_STRING, kCommandSettings, chinese ? L"设置" : L"Settings");
    AppendMenuW(menu, MF_STRING | (settings_.show_float_bar ? MF_CHECKED : MF_UNCHECKED), kCommandFloatBar,
        chinese ? L"浮动用量条" : L"Floating usage bar");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandExit, chinese ? L"退出" : L"Quit");
    SetForegroundWindow(popup_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, popup_, nullptr);
    DestroyMenu(menu);
    switch (command) {
    case kCommandOpen: ShowPopup(); break;
    case kCommandRefresh:
        RestartRefreshTimer();
        RefreshAsync();
        break;
    case kCommandCopySummary: ActivatePopupAction(ui::PopupAction::CopySummary); break;
    case kSnoozeActive: ApplyNotificationSnooze(NotificationSnoozePreset::Active); break;
    case kSnoozeOneHour: ApplyNotificationSnooze(NotificationSnoozePreset::OneHour); break;
    case kSnoozeFourHours: ApplyNotificationSnooze(NotificationSnoozePreset::FourHours); break;
    case kSnoozeTwentyFourHours: ApplyNotificationSnooze(NotificationSnoozePreset::TwentyFourHours); break;
    case kCommandSettings: ShowSettings(); break;
    case kCommandFloatBar:
        settings_.show_float_bar = !settings_.show_float_bar;
        if (settings_.show_float_bar) ShowFloatBar(false);
        else HideFloatBar(false);
        SaveSettings();
        break;
    case kCommandExit: Quit(); break;
    default: break;
    }
}

void App::TogglePopup() {
    if (IsWindowVisible(popup_)) HidePopup();
    else ShowPopup();
}

void App::ShowPopup() {
    ApplyTheme(popup_);
    RECT anchor_rect{};
    NOTIFYICONIDENTIFIER identifier{sizeof(identifier), tray_.hWnd, tray_.uID, {}};
    RECT icon_rect{};
    if (SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &icon_rect))) {
        anchor_rect = icon_rect;
    } else {
        POINT anchor{};
        GetCursorPos(&anchor);
        anchor_rect = {anchor.x, anchor.y, anchor.x + 1, anchor.y + 1};
    }
    const POINT anchor{anchor_rect.left + (anchor_rect.right - anchor_rect.left) / 2,
        anchor_rect.top + (anchor_rect.bottom - anchor_rect.top) / 2};
    HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return;
    const UINT target_dpi = EffectiveDpiForMonitor(monitor);
    const UsageSnapshot snapshot = SnapshotCopy();
    const ui::PopupLayout popup_layout = ui::ResolvePopupLayout(snapshot);
    const int client_width = MulDiv(ui::kPopupWindowWidth, static_cast<int>(target_dpi), 96);
    const int logical_window_height = static_cast<int>(
        std::lround(static_cast<float>(popup_layout.content_height) * ui::kContentScale));
    const int client_height = MulDiv(logical_window_height, static_cast<int>(target_dpi), 96);
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(popup_, GWL_STYLE));
    const DWORD extended_style = static_cast<DWORD>(GetWindowLongPtrW(popup_, GWL_EXSTYLE));
    const SIZE outer_size = WindowSizeForClient(
        client_width, client_height, style, extended_style, target_dpi);
    auto placement = windowing::CalculatePopupPlacement(
        Pixels(anchor_rect), Pixels(info.rcWork), outer_size.cx, outer_size.cy);
    SetWindowPos(popup_, HWND_TOPMOST, placement.x, placement.y, outer_size.cx, outer_size.cy, SWP_SHOWWINDOW);

    // Moving a Per-Monitor V2 window can synchronously apply WM_DPICHANGED.
    // Constrain the resulting real outer rectangle once more so a target-DPI
    // resize or DWM shadow can never push controls beyond the work area.
    RECT actual{};
    if (GetWindowRect(popup_, &actual)) {
        const int actual_width = actual.right - actual.left;
        const int actual_height = actual.bottom - actual.top;
        const auto corrected = windowing::CalculatePopupPlacement(
            Pixels(anchor_rect), Pixels(info.rcWork), actual_width, actual_height);
        if (corrected.x != actual.left || corrected.y != actual.top) {
            SetWindowPos(popup_, HWND_TOPMOST, corrected.x, corrected.y, 0, 0,
                SWP_NOSIZE | SWP_SHOWWINDOW);
        }
    }
    SetForegroundWindow(popup_);
    SetFocus(popup_);
    if (RefreshIsActive(CurrentRefreshPhase()) && AnimationsEnabled()) SetTimer(popup_, kRefreshAnimationTimer, 16, nullptr);
    InvalidateRect(popup_, nullptr, FALSE);
    MaybeRefreshOnOpen();
}

void App::HidePopup() {
    KillTimer(popup_, kRefreshAnimationTimer);
    KillTimer(popup_, kHoverAnimationTimer);
    popup_hover_ = ui::PopupAction::None;
    popup_hover_progress_ = 0.0F;
    popup_activation_handoff_pending_ = false;
    ShowWindow(popup_, SW_HIDE);
}

void App::ShowSettings() {
    HidePopup();
    ApplyTheme(settings_window_);
    SetWindowTextW(settings_window_, ShouldUseChinese(settings_.language) ? L"Codex Partner 设置" : L"Codex Partner Settings");
    if (!IsWindowVisible(settings_window_)) {
        RECT rect{};
        GetWindowRect(settings_window_, &rect);
        HMONITOR monitor = MonitorFromWindow(settings_window_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(monitor, &info);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        SetWindowPos(settings_window_, nullptr, info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2, info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
    }
    ShowWindow(settings_window_, SW_SHOWNORMAL);
    SetForegroundWindow(settings_window_);
    SyncNotificationSnoozeTimer();
    InvalidateRect(settings_window_, nullptr, FALSE);
}

void App::HideSettings() {
    KillTimer(settings_window_, kHoverAnimationTimer);
    KillTimer(settings_window_, kSnoozeStatusTimer);
    settings_hover_ = ui::SettingsAction::None;
    settings_hover_progress_ = 0.0F;
    ShowWindow(settings_window_, SW_HIDE);
}

void App::ShowFloatBar(bool activate) {
    ApplyTheme(float_bar_window_);
    SetWindowTextW(float_bar_window_, ShouldUseChinese(settings_.language) ?
        L"Codex Partner 浮动用量条" : L"Codex Partner Floating Usage Bar");
    RECT current{};
    GetWindowRect(float_bar_window_, &current);
    int width = current.right - current.left;
    int height = current.bottom - current.top;
    if (width <= 0 || height <= 0) {
        const UINT dpi = GetDpiForSystem();
        width = MulDiv(ui::kFloatBarWindowWidth, static_cast<int>(dpi), 96);
        height = MulDiv(ui::kFloatBarWindowHeight, static_cast<int>(dpi), 96);
    }

    HMONITOR monitor = nullptr;
    if (settings_.float_bar_x != kUnsetWindowPosition && settings_.float_bar_y != kUnsetWindowPosition) {
        monitor = MonitorFromPoint(POINT{settings_.float_bar_x, settings_.float_bar_y}, MONITOR_DEFAULTTOPRIMARY);
    } else if (IsWindowVisible(settings_window_)) {
        monitor = MonitorFromWindow(settings_window_, MONITOR_DEFAULTTOPRIMARY);
    } else {
        monitor = MonitorFromWindow(popup_, MONITOR_DEFAULTTOPRIMARY);
    }
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return;
    const FloatBarPoint position = ConstrainFloatBarPosition(settings_.float_bar_x, settings_.float_bar_y,
        info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom, width, height);
    positioning_float_bar_ = true;
    SetWindowPos(float_bar_window_, HWND_TOPMOST, position.x, position.y, width, height,
        SWP_SHOWWINDOW | (activate ? 0U : SWP_NOACTIVATE));
    positioning_float_bar_ = false;
    if (activate) {
        SetForegroundWindow(float_bar_window_);
        SetFocus(float_bar_window_);
    }
    InvalidateRect(float_bar_window_, nullptr, FALSE);
}

void App::HideFloatBar(bool persist) {
    KillTimer(float_bar_window_, kHoverAnimationTimer);
    KillTimer(float_bar_window_, kPressFeedbackTimer);
    float_bar_hover_ = ui::FloatBarAction::None;
    float_bar_pressed_ = ui::FloatBarAction::None;
    float_bar_hover_progress_ = 0.0F;
    ShowWindow(float_bar_window_, SW_HIDE);
    if (persist && settings_.show_float_bar) {
        settings_.show_float_bar = false;
        SaveSettings();
    }
}

void App::ResetFloatBarPosition() {
    settings_.float_bar_x = kUnsetWindowPosition;
    settings_.float_bar_y = kUnsetWindowPosition;
    SaveSettings();
    if (IsWindowVisible(float_bar_window_)) ShowFloatBar(false);
}

void App::PersistFloatBarPosition() {
    if (positioning_float_bar_ || !IsWindowVisible(float_bar_window_)) return;
    RECT rect{};
    if (!GetWindowRect(float_bar_window_, &rect)) return;
    if (settings_.float_bar_x == rect.left && settings_.float_bar_y == rect.top) return;
    settings_.float_bar_x = rect.left;
    settings_.float_bar_y = rect.top;
    SaveSettings();
}

void App::ShowLanguageMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const bool chinese = ShouldUseChinese(settings_.language);
    AppendMenuW(menu, MF_STRING, kLanguageSystem, chinese ? L"跟随系统" : L"Use system language");
    AppendMenuW(menu, MF_STRING, kLanguageChinese, L"简体中文");
    AppendMenuW(menu, MF_STRING, kLanguageEnglish, L"English");
    const UINT selected = settings_.language == LanguageMode::SimplifiedChinese ? kLanguageChinese :
        settings_.language == LanguageMode::English ? kLanguageEnglish : kLanguageSystem;
    const UINT command = ShowRadioMenu(owner, menu, kLanguageSystem, kLanguageEnglish, selected, 168);
    LanguageMode next = settings_.language;
    if (command == kLanguageSystem) next = LanguageMode::System;
    else if (command == kLanguageChinese) next = LanguageMode::SimplifiedChinese;
    else if (command == kLanguageEnglish) next = LanguageMode::English;
    if (next != settings_.language) {
        settings_.language = next;
        SaveSettings();
    }
}

void App::ShowThemeMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const bool chinese = ShouldUseChinese(settings_.language);
    AppendMenuW(menu, MF_STRING, kThemeSystem, chinese ? L"跟随系统" : L"Use system theme");
    AppendMenuW(menu, MF_STRING, kThemeLight, chinese ? L"浅色" : L"Light");
    AppendMenuW(menu, MF_STRING, kThemeDark, chinese ? L"深色" : L"Dark");
    const UINT selected = settings_.theme == ThemeMode::Light ? kThemeLight : settings_.theme == ThemeMode::Dark ? kThemeDark : kThemeSystem;
    const UINT command = ShowRadioMenu(owner, menu, kThemeSystem, kThemeDark, selected, 240);
    ThemeMode next = settings_.theme;
    if (command == kThemeSystem) next = ThemeMode::System;
    else if (command == kThemeLight) next = ThemeMode::Light;
    else if (command == kThemeDark) next = ThemeMode::Dark;
    if (next != settings_.theme) {
        settings_.theme = next;
        SaveSettings();
    }
}

void App::ShowGlobalShortcutMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const bool chinese = ShouldUseChinese(settings_.language);
    AppendMenuW(menu, MF_STRING, kShortcutDisabled, chinese ? L"关闭" : L"Off");
    AppendMenuW(menu, MF_STRING, kShortcutCtrlShiftU,
        chinese ? L"Ctrl+Shift+U（推荐）" : L"Ctrl+Shift+U (recommended)");
    AppendMenuW(menu, MF_STRING, kShortcutCtrlAltU, L"Ctrl+Alt+U");
    AppendMenuW(menu, MF_STRING, kShortcutCtrlShiftSpace, L"Ctrl+Shift+Space");
    const UINT selected = settings_.global_shortcut == GlobalShortcut::Disabled ? kShortcutDisabled :
        settings_.global_shortcut == GlobalShortcut::CtrlAltU ? kShortcutCtrlAltU :
        settings_.global_shortcut == GlobalShortcut::CtrlShiftSpace ? kShortcutCtrlShiftSpace :
        kShortcutCtrlShiftU;
    const UINT command = ShowRadioMenu(owner, menu, kShortcutDisabled, kShortcutCtrlShiftSpace, selected, 304);
    if (command == kShortcutDisabled) ChangeGlobalShortcut(GlobalShortcut::Disabled);
    else if (command == kShortcutCtrlShiftU) ChangeGlobalShortcut(GlobalShortcut::CtrlShiftU);
    else if (command == kShortcutCtrlAltU) ChangeGlobalShortcut(GlobalShortcut::CtrlAltU);
    else if (command == kShortcutCtrlShiftSpace) ChangeGlobalShortcut(GlobalShortcut::CtrlShiftSpace);
}

bool App::BindGlobalShortcut(GlobalShortcut shortcut) {
    if (proof_mode_) {
        registered_global_shortcut_ = shortcut;
        global_shortcut_status_ = shortcut == GlobalShortcut::Disabled ?
            GlobalShortcutStatus::Disabled : GlobalShortcutStatus::Registered;
        return true;
    }
    if (registered_global_shortcut_ == shortcut &&
        (shortcut == GlobalShortcut::Disabled || global_shortcut_status_ == GlobalShortcutStatus::Registered)) {
        global_shortcut_status_ = shortcut == GlobalShortcut::Disabled ?
            GlobalShortcutStatus::Disabled : GlobalShortcutStatus::Registered;
        return true;
    }
    if (registered_global_shortcut_ != GlobalShortcut::Disabled) {
        UnregisterHotKey(popup_, kGlobalShortcutId);
        registered_global_shortcut_ = GlobalShortcut::Disabled;
    }
    if (shortcut == GlobalShortcut::Disabled) {
        global_shortcut_status_ = GlobalShortcutStatus::Disabled;
        return true;
    }
    const auto binding = BindingForGlobalShortcut(shortcut);
    if (binding && RegisterHotKey(popup_, kGlobalShortcutId, binding->modifiers, binding->virtual_key)) {
        registered_global_shortcut_ = shortcut;
        global_shortcut_status_ = GlobalShortcutStatus::Registered;
        return true;
    }
    global_shortcut_status_ = GlobalShortcutStatus::Unavailable;
    return false;
}

void App::ChangeGlobalShortcut(GlobalShortcut shortcut) {
    const GlobalShortcut previous = settings_.global_shortcut;
    const bool attempted_registered = BindGlobalShortcut(shortcut);
    if (!attempted_registered) {
        const bool previous_restored = BindGlobalShortcut(previous);
        const GlobalShortcutChangeResult result = ResolveGlobalShortcutChange(
            previous, shortcut, false, previous_restored);
        settings_.global_shortcut = result.effective;
        global_shortcut_status_ = result.status;
        KillTimer(settings_window_, kSavedFeedbackTimer);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings_window_, OBJID_CLIENT, 16);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings_window_, OBJID_CLIENT, 60);
        InvalidateRect(settings_window_, nullptr, FALSE);
        return;
    }
    const GlobalShortcutChangeResult result = ResolveGlobalShortcutChange(previous, shortcut, true, false);
    settings_.global_shortcut = result.effective;
    global_shortcut_status_ = result.status;
    if (result.should_persist) {
        if (!SaveSettings()) {
            (void)BindGlobalShortcut(settings_.global_shortcut);
        }
    } else {
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings_window_, OBJID_CLIENT, 16);
        InvalidateRect(popup_, nullptr, FALSE);
        InvalidateRect(settings_window_, nullptr, FALSE);
    }
}

void App::ShowRefreshMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const bool chinese = ShouldUseChinese(settings_.language);
    AppendMenuW(menu, MF_STRING, kRefreshFive, chinese ? L"每 5 分钟" : L"Every 5 minutes");
    AppendMenuW(menu, MF_STRING, kRefreshFifteen, chinese ? L"每 15 分钟" : L"Every 15 minutes");
    AppendMenuW(menu, MF_STRING, kRefreshThirty, chinese ? L"每 30 分钟" : L"Every 30 minutes");
    const UINT selected = settings_.refresh_minutes == 5 ? kRefreshFive : settings_.refresh_minutes == 30 ? kRefreshThirty : kRefreshFifteen;
    const UINT command = ShowRadioMenu(owner, menu, kRefreshFive, kRefreshThirty, selected, 372);
    int next = settings_.refresh_minutes;
    if (command == kRefreshFive) next = 5;
    else if (command == kRefreshFifteen) next = 15;
    else if (command == kRefreshThirty) next = 30;
    if (next != settings_.refresh_minutes) {
        settings_.refresh_minutes = next;
        SaveSettings();
    }
}

void App::ShowUsageWarningMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const bool chinese = ShouldUseChinese(settings_.language);
    AppendMenuW(menu, MF_STRING, kWarningSeventy, chinese ? L"70%（提前提醒）" : L"70% (early warning)");
    AppendMenuW(menu, MF_STRING, kWarningEighty, chinese ? L"80%（推荐）" : L"80% (recommended)");
    AppendMenuW(menu, MF_STRING, kWarningNinety, chinese ? L"90%（较晚提醒）" : L"90% (late warning)");
    const UINT selected = settings_.usage_warning_percent == 70 ? kWarningSeventy :
        settings_.usage_warning_percent == 90 ? kWarningNinety : kWarningEighty;
    const UINT command = ShowRadioMenu(owner, menu, kWarningSeventy, kWarningNinety, selected, 240);
    int next = settings_.usage_warning_percent;
    if (command == kWarningSeventy) next = 70;
    else if (command == kWarningEighty) next = 80;
    else if (command == kWarningNinety) next = 90;
    if (next != settings_.usage_warning_percent) {
        settings_.usage_warning_percent = next;
        last_alert_level_ = UsageAlertLevel::None;
        SaveSettings();
    }
}

void App::ShowNotificationSnoozeMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const bool chinese = ShouldUseChinese(settings_.language);
    if (!settings_.usage_notifications) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0,
            chinese ? L"请先开启额度提醒" : L"Turn on usage alerts first");
        const POINT anchor = SelectorMenuAnchor(owner, 320);
        SetForegroundWindow(owner);
        TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
            anchor.x, anchor.y, 0, owner, nullptr);
        PostMessageW(owner, WM_NULL, 0, 0);
        DestroyMenu(menu);
        return;
    }
    const bool snoozed = IsNotificationSnoozed(settings_.notification_snoozed_until);
    AppendNotificationSnoozeItems(menu, chinese, snoozed);
    const UINT command = ShowRadioMenu(owner, menu, kSnoozeActive, kSnoozeTwentyFourHours,
        snoozed ? 0U : kSnoozeActive, 320);
    if (command == kSnoozeActive) ApplyNotificationSnooze(NotificationSnoozePreset::Active);
    else if (command == kSnoozeOneHour) ApplyNotificationSnooze(NotificationSnoozePreset::OneHour);
    else if (command == kSnoozeFourHours) ApplyNotificationSnooze(NotificationSnoozePreset::FourHours);
    else if (command == kSnoozeTwentyFourHours) ApplyNotificationSnooze(NotificationSnoozePreset::TwentyFourHours);
}

void App::ApplyNotificationSnooze(NotificationSnoozePreset preset) {
    settings_.notification_snoozed_until = ResolveNotificationSnooze(preset);
    last_alert_level_ = DeriveUsageAlertLevel(
        SnapshotCopy(), static_cast<double>(settings_.usage_warning_percent));
    SaveSettings();
    SyncNotificationSnoozeTimer();
}

void App::ShowUsageNotification(UsageAlertLevel level, bool test) {
    if (!tray_added_) return;
    const bool chinese = ShouldUseChinese(settings_.language);
    std::wstring title;
    std::wstring body;
    DWORD flags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
    if (test) {
        title = chinese ? L"Codex Partner 测试提醒" : L"Codex Partner test notification";
        body = chinese ? L"提醒功能工作正常。只有使用率跨过阈值时才会再次通知。" :
            L"Notifications are working. You will only be alerted again when usage crosses a threshold.";
        flags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    } else {
        const int used = static_cast<int>(std::round(MostConstrainedPercent(SnapshotCopy())));
        if (level == UsageAlertLevel::Critical) {
            title = chinese ? L"Codex 额度即将用尽" : L"Codex capacity is almost exhausted";
            body = chinese ? L"当前最高使用率为 " + std::to_wstring(used) + L"%。点击查看重置时间。" :
                L"Your most constrained window is at " + std::to_wstring(used) + L"%. Click to view reset timing.";
            flags = NIIF_ERROR | NIIF_RESPECT_QUIET_TIME;
        } else {
            title = chinese ? L"Codex 额度需要留意" : L"Codex capacity needs attention";
            body = chinese ? L"当前最高使用率为 " + std::to_wstring(used) + L"%，已达到你的预警阈值。" :
                L"Your most constrained window is at " + std::to_wstring(used) + L"%, reaching your warning threshold.";
        }
    }
    wcsncpy_s(tray_.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(tray_.szInfo, body.c_str(), _TRUNCATE);
    tray_.dwInfoFlags = flags;
    tray_.uFlags = NIF_INFO | NIF_REALTIME;
    Shell_NotifyIconW(NIM_MODIFY, &tray_);
}

void App::MaybeNotifyUsage(const UsageSnapshot& snapshot) {
    if (snapshot.loading || !snapshot.error.empty() || (!snapshot.session && !snapshot.weekly)) return;
    const UsageAlertLevel level = DeriveUsageAlertLevel(snapshot, static_cast<double>(settings_.usage_warning_percent));
    if (ShouldDeliverUsageAlert(settings_.usage_notifications, settings_.notification_snoozed_until,
        level, last_alert_level_)) {
        ShowUsageNotification(level);
    }
    last_alert_level_ = level;
}

void App::ActivatePopupAction(ui::PopupAction action) {
    switch (action) {
    case ui::PopupAction::CopySummary:
        usage_summary_copy_state_ = CopyUsageSummary() ? ui::CopySummaryState::Copied : ui::CopySummaryState::Failed;
        KillTimer(popup_, kCopyFeedbackTimer);
        SetTimer(popup_, kCopyFeedbackTimer,
            usage_summary_copy_state_ == ui::CopySummaryState::Copied ? 1600 : 2600, nullptr);
        InvalidateRect(popup_, nullptr, FALSE);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, popup_, OBJID_CLIENT, 4);
        break;
    case ui::PopupAction::Refresh:
        RestartRefreshTimer();
        RefreshAsync();
        break;
    case ui::PopupAction::Settings: ShowSettings(); break;
    case ui::PopupAction::Primary: {
        const UsagePrimaryTarget target = ResolveUsagePrimaryTarget(SnapshotCopy());
        if (target == UsagePrimaryTarget::ProviderSetup) {
            settings_tab_ = ui::SettingsTab::Providers;
            ShowSettings();
        } else if (target == UsagePrimaryTarget::RefreshUsage) {
            RestartRefreshTimer();
            RefreshAsync();
        } else {
            settings_tab_ = ui::SettingsTab::UsageSpend;
            ShowSettings();
        }
        break;
    }
    case ui::PopupAction::None: break;
    }
}

void App::ActivateSettingsAction(ui::SettingsAction action) {
    diagnostics_copied_ = false;
    const ui::SettingsTab previous_tab = settings_tab_;
    switch (action) {
    case ui::SettingsAction::SelectGeneral: settings_tab_ = ui::SettingsTab::General; break;
    case ui::SettingsAction::SelectProviders: settings_tab_ = ui::SettingsTab::Providers; break;
    case ui::SettingsAction::SelectNotifications: settings_tab_ = ui::SettingsTab::Notifications; break;
    case ui::SettingsAction::SelectFloatBar: settings_tab_ = ui::SettingsTab::FloatBar; break;
    case ui::SettingsAction::SelectUsageSpend: settings_tab_ = ui::SettingsTab::UsageSpend; break;
    case ui::SettingsAction::SelectAbout: settings_tab_ = ui::SettingsTab::About; break;
    case ui::SettingsAction::CycleLanguage: ShowLanguageMenu(settings_window_); break;
    case ui::SettingsAction::CycleTheme: ShowThemeMenu(settings_window_); break;
    case ui::SettingsAction::ChooseGlobalShortcut: ShowGlobalShortcutMenu(settings_window_); break;
    case ui::SettingsAction::CycleRefresh: ShowRefreshMenu(settings_window_); break;
    case ui::SettingsAction::ToggleUsageNotifications:
        settings_.usage_notifications = !settings_.usage_notifications;
        settings_.notification_snoozed_until = {};
        last_alert_level_ = DeriveUsageAlertLevel(SnapshotCopy(), static_cast<double>(settings_.usage_warning_percent));
        SaveSettings();
        SyncNotificationSnoozeTimer();
        break;
    case ui::SettingsAction::ChooseUsageWarning: ShowUsageWarningMenu(settings_window_); break;
    case ui::SettingsAction::ChooseNotificationSnooze: ShowNotificationSnoozeMenu(settings_window_); break;
    case ui::SettingsAction::TestNotification: ShowUsageNotification(UsageAlertLevel::Warning, true); break;
    case ui::SettingsAction::LaunchCodexLogin: LaunchCodexLogin(); break;
    case ui::SettingsAction::ToggleStartAtLogin: {
        const AppSettings previous = settings_;
        settings_.start_at_login = !settings_.start_at_login;
        if (!settings_store_.ApplyStartAtLogin(settings_.start_at_login, settings_.start_minimized)) {
            settings_ = previous;
            ShowSettingsSaveFailure();
        } else if (!SaveSettings()) {
            (void)settings_store_.ApplyStartAtLogin(previous.start_at_login, previous.start_minimized);
        }
        break;
    }
    case ui::SettingsAction::ToggleStartMinimized: {
        const AppSettings previous = settings_;
        settings_.start_minimized = !settings_.start_minimized;
        if (settings_.start_at_login && !settings_store_.ApplyStartAtLogin(true, settings_.start_minimized)) {
            settings_ = previous;
            ShowSettingsSaveFailure();
        } else if (!SaveSettings() && previous.start_at_login) {
            (void)settings_store_.ApplyStartAtLogin(true, previous.start_minimized);
        }
        break;
    }
    case ui::SettingsAction::TogglePrivacy:
        settings_.hide_identity = !settings_.hide_identity;
        SaveSettings();
        break;
    case ui::SettingsAction::ToggleFloatBar:
        settings_.show_float_bar = !settings_.show_float_bar;
        if (settings_.show_float_bar) ShowFloatBar(false);
        else HideFloatBar(false);
        SaveSettings();
        break;
    case ui::SettingsAction::ResetFloatBarPosition:
        ResetFloatBarPosition();
        break;
    case ui::SettingsAction::OpenCodexFolder: OpenCodexFolder(); break;
    case ui::SettingsAction::CheckForUpdates:
        if (update_check_.status == UpdateCheckStatus::Available && !update_check_.release_url.empty()) {
            OpenAvailableRelease();
        } else {
            CheckForUpdates();
        }
        break;
    case ui::SettingsAction::ReportIssue: ReportIssue(); break;
    case ui::SettingsAction::OpenProjectSite: OpenProjectSite(); break;
    case ui::SettingsAction::CopyDiagnostics:
        settings_external_feedback_ = {};
        KillTimer(settings_window_, kExternalFeedbackTimer);
        diagnostics_copied_ = CopyDiagnostics();
        if (diagnostics_copied_) {
            KillTimer(settings_window_, kSavedFeedbackTimer);
            SetTimer(settings_window_, kSavedFeedbackTimer, 1600, nullptr);
        }
        break;
    case ui::SettingsAction::None: break;
    }
    if (settings_tab_ != previous_tab) {
        NotifyWinEvent(EVENT_OBJECT_REORDER, settings_window_, OBJID_CLIENT, CHILDID_SELF);
        NotifyWinEvent(EVENT_OBJECT_SELECTION, settings_window_, OBJID_CLIENT,
            accessibility::SettingsChildId(settings_tab_, action));
    } else {
        const long child_id = accessibility::SettingsChildId(settings_tab_, action);
        if (child_id > 0) NotifyWinEvent(EVENT_OBJECT_STATECHANGE, settings_window_, OBJID_CLIENT, child_id);
    }
    InvalidateRect(settings_window_, nullptr, FALSE);
}

void App::ActivateFloatBarAction(ui::FloatBarAction action) {
    switch (action) {
    case ui::FloatBarAction::OpenPopup:
        ShowPopup();
        break;
    case ui::FloatBarAction::Hide:
        HideFloatBar(true);
        break;
    case ui::FloatBarAction::None:
        break;
    }
}

void App::MaybeRefreshOnOpen() {
    if (proof_mode_) return;
    const OpenRefreshDecision decision = EvaluateOpenRefresh(
        SnapshotCopy(), refresh_coordinator_.active(), last_refresh_started_);
    if (!decision.should_refresh) return;
    RestartRefreshTimer();
    RefreshAsync(false);
}

void App::RefreshAsync(bool retain_if_active) {
    if (proof_seeded_) return;
    const RefreshRequestDisposition disposition = refresh_coordinator_.Request(retain_if_active);
    if (disposition == RefreshRequestDisposition::Started) {
        StartRefreshCycle();
        return;
    }
    if (disposition == RefreshRequestDisposition::Coalesced) return;

    // A provider request and the heavier local scan form one serialized cycle.
    // Keep exactly one trailing cycle rather than dropping deliberate user or
    // wake intent—or multiplying repeated clicks into a request storm. Periodic
    // timer ticks use retain_if_active=false and simply try again next interval.
    if (IsWindowVisible(popup_) && AnimationsEnabled()) SetTimer(popup_, kRefreshAnimationTimer, 16, nullptr);
    InvalidateRect(popup_, nullptr, FALSE);
    InvalidateRect(settings_window_, nullptr, FALSE);
    InvalidateRect(float_bar_window_, nullptr, FALSE);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, popup_, OBJID_CLIENT, CHILDID_SELF);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, float_bar_window_, OBJID_CLIENT, 1);
}

void App::StartRefreshCycle() {
    last_refresh_started_ = std::chrono::steady_clock::now();
    usage_refreshing_ = true;
    spend_refreshing_ = true;
    popup_external_feedback_ = {};
    {
        std::scoped_lock lock(snapshot_mutex_);
        if (!snapshot_.session && !snapshot_.weekly) snapshot_.loading = true;
    }
    refresh_angle_ = 0.0F;
    if (IsWindowVisible(popup_) && AnimationsEnabled()) SetTimer(popup_, kRefreshAnimationTimer, 16, nullptr);
    InvalidateRect(popup_, nullptr, FALSE);
    InvalidateRect(settings_window_, nullptr, FALSE);
    InvalidateRect(float_bar_window_, nullptr, FALSE);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, popup_, OBJID_CLIENT, CHILDID_SELF);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, float_bar_window_, OBJID_CLIENT, 1);
    if (refresh_thread_.joinable()) refresh_thread_.join();
    refresh_thread_ = std::jthread([this](std::stop_token stop) {
        const IUsageProvider* provider = providers_.Find(L"codex");
        std::future<std::optional<SpendSummary>> spend_future;
        if (provider) {
            try {
                spend_future = std::async(std::launch::async, [provider, stop] { return provider->FetchSpend(stop); });
            } catch (...) {
                OutputDebugStringW(L"[CodexPartner] Could not start spend refresh\n");
            }
        }
        UsageSnapshot next;
        try {
            if (provider) next = provider->Fetch(stop);
            else {
                next.loading = false;
                next.updated_at = std::chrono::system_clock::now();
                next.error = L"Codex provider is not registered.";
            }
        } catch (...) {
            next.loading = false;
            next.updated_at = std::chrono::system_clock::now();
            next.error = L"Codex refresh failed unexpectedly.";
            OutputDebugStringW(L"[CodexPartner] Provider refresh failed unexpectedly\n");
        }
        UsageSnapshot usage_cache_candidate;
        {
            std::scoped_lock lock(snapshot_mutex_);
            snapshot_ = MergeUsageRefresh(snapshot_, std::move(next));
            usage_cache_candidate = snapshot_;
        }
        if (!usage_cache_candidate.stale && usage_cache_candidate.error.empty()) (void)usage_cache_.Save(usage_cache_candidate);
        usage_refreshing_ = false;
        PostMessageW(popup_, kUsageUpdated, 0, 0);

        std::optional<SpendSummary> next_spend;
        if (spend_future.valid()) {
            try {
                next_spend = spend_future.get();
            } catch (...) {
                OutputDebugStringW(L"[CodexPartner] Spend refresh failed unexpectedly\n");
            }
        }
        UsageSnapshot spend_cache_candidate;
        {
            std::scoped_lock lock(snapshot_mutex_);
            snapshot_.spend = MergeSpendRefresh(snapshot_.spend, std::move(next_spend));
            spend_cache_candidate = snapshot_;
        }
        if (!spend_cache_candidate.stale && spend_cache_candidate.error.empty()) (void)usage_cache_.Save(spend_cache_candidate);
        PostMessageW(popup_, kSpendUpdated, 0, 0);
    });
}

void App::HandleSystemResume() {
    const auto steady_now = std::chrono::steady_clock::now();
    const auto wall_now = std::chrono::system_clock::now();
    bool marked_stale = false;
    {
        std::scoped_lock lock(snapshot_mutex_);
        const ResumeRefreshDecision decision = EvaluateResumeRefresh(
            snapshot_, last_resume_refresh_, steady_now, wall_now);
        if (!decision.accepted) return;
        last_resume_refresh_ = steady_now;
        if (decision.mark_usage_stale) {
            snapshot_.stale = true;
            marked_stale = true;
        }
    }
    RestartRefreshTimer();
    if (marked_stale) {
        UpdateTrayTooltip();
        InvalidateRect(popup_, nullptr, FALSE);
        InvalidateRect(settings_window_, nullptr, FALSE);
        InvalidateRect(float_bar_window_, nullptr, FALSE);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, popup_, OBJID_CLIENT, CHILDID_SELF);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings_window_, OBJID_CLIENT, CHILDID_SELF);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, float_bar_window_, OBJID_CLIENT, 1);
    }
    OutputDebugStringW(L"[CodexPartner] System resume accepted; usage refresh scheduled\n");
    RefreshAsync();
}

void App::SetPopupHover(ui::PopupAction action) {
    if (popup_hover_ == action) return;
    popup_hover_ = action;
    if (!AnimationsEnabled()) {
        KillTimer(popup_, kHoverAnimationTimer);
        popup_hover_progress_ = action == ui::PopupAction::None ? 0.0F : 1.0F;
        InvalidateRect(popup_, nullptr, FALSE);
        return;
    }
    popup_hover_progress_ = action == ui::PopupAction::None ? std::min(popup_hover_progress_, 1.0F) : 0.0F;
    SetTimer(popup_, kHoverAnimationTimer, 16, nullptr);
    InvalidateRect(popup_, nullptr, FALSE);
}

void App::SetSettingsHover(ui::SettingsAction action) {
    if (settings_hover_ == action) return;
    settings_hover_ = action;
    if (!AnimationsEnabled()) {
        KillTimer(settings_window_, kHoverAnimationTimer);
        settings_hover_progress_ = action == ui::SettingsAction::None ? 0.0F : 1.0F;
        InvalidateRect(settings_window_, nullptr, FALSE);
        return;
    }
    settings_hover_progress_ = action == ui::SettingsAction::None ? std::min(settings_hover_progress_, 1.0F) : 0.0F;
    SetTimer(settings_window_, kHoverAnimationTimer, 16, nullptr);
    InvalidateRect(settings_window_, nullptr, FALSE);
}

void App::SetFloatBarHover(ui::FloatBarAction action) {
    if (float_bar_hover_ == action) return;
    float_bar_hover_ = action;
    if (!AnimationsEnabled()) {
        KillTimer(float_bar_window_, kHoverAnimationTimer);
        float_bar_hover_progress_ = action == ui::FloatBarAction::None ? 0.0F : 1.0F;
        InvalidateRect(float_bar_window_, nullptr, FALSE);
        return;
    }
    float_bar_hover_progress_ = action == ui::FloatBarAction::None ?
        std::min(float_bar_hover_progress_, 1.0F) : 0.0F;
    SetTimer(float_bar_window_, kHoverAnimationTimer, 16, nullptr);
    InvalidateRect(float_bar_window_, nullptr, FALSE);
}

void App::SetPopupAccessibleFocus(ui::PopupAction action) {
    if (popup_accessible_focus_ == action) return;
    popup_accessible_focus_ = action;
    const long child_id = accessibility::PopupChildId(action);
    if (child_id > 0) NotifyWinEvent(EVENT_OBJECT_FOCUS, popup_, OBJID_CLIENT, child_id);
}

void App::SetSettingsAccessibleFocus(ui::SettingsAction action) {
    if (settings_accessible_focus_ == action) return;
    settings_accessible_focus_ = action;
    if (action == ui::SettingsAction::SelectUsageSpend) {
        usage_chart_hover_.reset();
        usage_chart_progress_ = AnimationsEnabled() ? 0.0F : 1.0F;
        if (usage_chart_progress_ < 1.0F) SetTimer(settings_window_, kUsageChartAnimationTimer, 16, nullptr);
    }
    const long child_id = accessibility::SettingsChildId(settings_tab_, action);
    if (child_id > 0) NotifyWinEvent(EVENT_OBJECT_FOCUS, settings_window_, OBJID_CLIENT, child_id);
}

void App::SetFloatBarAccessibleFocus(ui::FloatBarAction action) {
    if (float_bar_accessible_focus_ == action) return;
    float_bar_accessible_focus_ = action;
    const long child_id = accessibility::FloatBarChildId(action);
    if (child_id > 0) NotifyWinEvent(EVENT_OBJECT_FOCUS, float_bar_window_, OBJID_CLIENT, child_id);
}

void App::TickPopupHoverAnimation() {
    const float target = popup_hover_ == ui::PopupAction::None ? 0.0F : 1.0F;
    popup_hover_progress_ += target > popup_hover_progress_ ? 0.16F : -0.16F;
    popup_hover_progress_ = std::clamp(popup_hover_progress_, 0.0F, 1.0F);
    if (popup_hover_progress_ == target) KillTimer(popup_, kHoverAnimationTimer);
    InvalidateRect(popup_, nullptr, FALSE);
}

void App::TickSettingsHoverAnimation() {
    const float target = settings_hover_ == ui::SettingsAction::None ? 0.0F : 1.0F;
    settings_hover_progress_ += target > settings_hover_progress_ ? 0.16F : -0.16F;
    settings_hover_progress_ = std::clamp(settings_hover_progress_, 0.0F, 1.0F);
    if (settings_hover_progress_ == target) KillTimer(settings_window_, kHoverAnimationTimer);
    InvalidateRect(settings_window_, nullptr, FALSE);
}

void App::TickFloatBarHoverAnimation() {
    const float target = float_bar_hover_ == ui::FloatBarAction::None ? 0.0F : 1.0F;
    float_bar_hover_progress_ += target > float_bar_hover_progress_ ? 0.16F : -0.16F;
    float_bar_hover_progress_ = std::clamp(float_bar_hover_progress_, 0.0F, 1.0F);
    if (float_bar_hover_progress_ == target) KillTimer(float_bar_window_, kHoverAnimationTimer);
    InvalidateRect(float_bar_window_, nullptr, FALSE);
}

void App::ApplyTheme(HWND window) const {
    const bool light = ShouldUseLightTheme(settings_.theme);
    const BOOL dark = light ? FALSE : TRUE;
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    const COLORREF caption = light ? RGB(246, 248, 251) : RGB(24, 25, 28);
    const COLORREF caption_text = light ? RGB(28, 31, 36) : RGB(244, 245, 247);
    DwmSetWindowAttribute(window, 34, &caption, sizeof(caption));
    DwmSetWindowAttribute(window, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(window, 36, &caption_text, sizeof(caption_text));
}

bool App::SaveSettings() {
    const SettingsCommitResult commit = ResolveSettingsCommit(
        persisted_settings_, settings_, settings_store_.Save(settings_));
    settings_ = commit.effective;
    persisted_settings_ = commit.persisted;
    settings_persistence_ = commit.feedback;
    SyncSettingsPresentation();
    KillTimer(settings_window_, kSavedFeedbackTimer);
    if (settings_persistence_ == SettingsPersistenceState::Saved) {
        SetTimer(settings_window_, kSavedFeedbackTimer, 1600, nullptr);
    }
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings_window_, OBJID_CLIENT, 60);
    InvalidateRect(popup_, nullptr, FALSE);
    InvalidateRect(settings_window_, nullptr, FALSE);
    InvalidateRect(float_bar_window_, nullptr, FALSE);
    return settings_persistence_ == SettingsPersistenceState::Saved;
}

void App::SyncSettingsPresentation() {
    ApplyTheme(popup_);
    ApplyTheme(settings_window_);
    ApplyTheme(float_bar_window_);
    SetWindowTextW(settings_window_, ShouldUseChinese(settings_.language) ? L"Codex Partner 设置" : L"Codex Partner Settings");
    SetWindowTextW(float_bar_window_, ShouldUseChinese(settings_.language) ? L"Codex Partner 浮动用量条" : L"Codex Partner Floating Usage Bar");
    UpdateTrayTooltip();
    RestartRefreshTimer();
    if (settings_.show_float_bar) ShowFloatBar(false);
    else HideFloatBar(false);
}

void App::ShowSettingsSaveFailure() {
    settings_persistence_ = SettingsPersistenceState::Failed;
    KillTimer(settings_window_, kSavedFeedbackTimer);
    InvalidateRect(settings_window_, nullptr, FALSE);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings_window_, OBJID_CLIENT, 60);
}

void App::RestartRefreshTimer() {
    KillTimer(popup_, kRefreshTimer);
    SetTimer(popup_, kRefreshTimer, static_cast<UINT>(settings_.refresh_minutes * 60 * 1000), nullptr);
}

void App::SyncNotificationSnoozeTimer() {
    KillTimer(settings_window_, kSnoozeStatusTimer);
    if (IsWindowVisible(settings_window_) &&
        IsNotificationSnoozed(settings_.notification_snoozed_until)) {
        SetTimer(settings_window_, kSnoozeStatusTimer, 30'000, nullptr);
    }
}

bool App::LaunchExternal(HWND owner, const wchar_t* target, const wchar_t* parameters) const {
    if (!target || target[0] == L'\0') return false;
    return ShellLaunchSucceeded(reinterpret_cast<std::intptr_t>(
        ShellExecuteW(owner, L"open", target, parameters, nullptr, SW_SHOWNORMAL)));
}

void App::SetSettingsExternalFeedback(ExternalAction action, bool opened) {
    settings_external_feedback_ = {action, opened ? ExternalActionOutcome::Opened : ExternalActionOutcome::Failed};
    KillTimer(settings_window_, kExternalFeedbackTimer);
    if (opened) SetTimer(settings_window_, kExternalFeedbackTimer, 1600, nullptr);
    InvalidateRect(settings_window_, nullptr, FALSE);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings_window_, OBJID_CLIENT, 60);
}

void App::LaunchCodexLogin() {
    SetSettingsExternalFeedback(ExternalAction::CodexLogin,
        LaunchExternal(settings_window_, L"cmd.exe", L"/k codex login"));
}

void App::OpenCodexFolder() {
    std::filesystem::path path;
    const std::wstring configured = EnvironmentValue(L"CODEX_HOME");
    if (!configured.empty()) path = configured;
    else {
        const std::wstring profile = EnvironmentValue(L"USERPROFILE");
        if (!profile.empty()) path = std::filesystem::path(profile) / L".codex";
    }
    SetSettingsExternalFeedback(ExternalAction::CodexFolder,
        !path.empty() && LaunchExternal(settings_window_, path.c_str()));
}

void App::OpenProjectSite() {
    SetSettingsExternalFeedback(ExternalAction::ProjectSite,
        LaunchExternal(settings_window_, L"https://github.com/70666/Codex-Partner"));
}

void App::ReportIssue() {
    const bool copied = CopyDiagnostics();
    const bool opened = LaunchExternal(settings_window_, IssueReportUrl());
    diagnostics_copied_ = copied;
    settings_external_feedback_ = {ExternalAction::IssuePage,
        opened ? ExternalActionOutcome::Opened : ExternalActionOutcome::Failed, true, copied};
    KillTimer(settings_window_, kExternalFeedbackTimer);
    KillTimer(settings_window_, kSavedFeedbackTimer);
    if (ExternalActionFullySucceeded(settings_external_feedback_)) {
        SetTimer(settings_window_, kExternalFeedbackTimer, 10'000, nullptr);
        SetTimer(settings_window_, kSavedFeedbackTimer, 10'000, nullptr);
    }
    InvalidateRect(settings_window_, nullptr, FALSE);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings_window_, OBJID_CLIENT, 54);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings_window_, OBJID_CLIENT, 60);
}

void App::CheckForUpdates() {
    if (update_check_.status == UpdateCheckStatus::Checking) return;
    if (update_thread_.joinable()) update_thread_.join();
    update_check_ = {UpdateCheckStatus::Checking, {}, {}, {}};
    InvalidateRect(settings_window_, nullptr, FALSE);
    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, settings_window_, OBJID_CLIENT, 53);
    update_thread_ = std::jthread([this](std::stop_token stop) {
        UpdateCheckState result = FetchLatestRelease(stop);
        if (stop.stop_requested()) return;
        {
            std::scoped_lock lock(update_mutex_);
            pending_update_check_ = std::move(result);
        }
        PostMessageW(settings_window_, kUpdateChecked, 0, 0);
    });
}

void App::OpenAvailableRelease() {
    const UpdateNavigationTarget target = ResolveUpdateNavigation(update_check_);
    const ExternalAction action = target.kind == UpdateNavigationKind::NativeDownload ?
        ExternalAction::NativeUpdateDownload : ExternalAction::ReleasePage;
    SetSettingsExternalFeedback(action,
        target.kind != UpdateNavigationKind::None && LaunchExternal(settings_window_, target.url.c_str()));
}

bool App::CopyUsageSummary() {
    const std::wstring summary = BuildUsageShareSummary(
        SnapshotCopy(), ShouldUseChinese(settings_.language), settings_.hide_identity);
    return CopyTextToClipboard(popup_, summary);
}

bool App::CopyDiagnostics() {
    return CopyTextToClipboard(settings_window_, BuildDiagnosticSummary(settings_, SnapshotCopy()));
}

bool App::LoadProofSeed() {
    const std::wstring configured = EnvironmentValue(L"CODEX_PARTNER_SEED_USAGE_JSON");
    if (configured.empty()) return false;
    std::ifstream stream(std::filesystem::path(configured), std::ios::binary);
    if (!stream) return false;
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto parsed = ParseJson(contents);
    if (!parsed.ok()) return false;
    const auto number = [](const JsonValue* value) -> std::optional<double> {
        return value ? value->as_number() : std::nullopt;
    };
    const auto wide_text = [](const JsonValue* value) -> std::wstring {
        if (!value) return {};
        const std::string_view text = value->as_string().value_or("");
        if (text.empty()) return {};
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0) return {};
        std::wstring result(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
        return result;
    };
    const auto make_window = [&](const JsonValue* value, const wchar_t* title) -> std::optional<RateWindow> {
        if (!value) return std::nullopt;
        const auto percent = number(value->find("usedPercent"));
        if (!percent) return std::nullopt;
        RateWindow window;
        window.title = title;
        window.used_percent = std::clamp(*percent, 0.0, 100.0);
        if (const auto minutes = number(value->find("windowMinutes"))) window.window_minutes = static_cast<int>(*minutes);
        window.resets_at = std::chrono::system_clock::now() + std::chrono::minutes(window.window_minutes > 0 && window.window_minutes < 1440 ? 135 : 3 * 24 * 60);
        return window;
    };
    UsageSnapshot seeded;
    seeded.loading = false;
    seeded.updated_at = std::chrono::system_clock::now();
    const JsonValue* setup_value = parsed.value.find("setupRequired");
    const bool setup_required = setup_value && setup_value->as_bool().value_or(false);
    seeded.connection = setup_required ? ProviderConnectionState::NeedsLogin :
        ProviderConnectionState::CredentialsDetected;
    if (setup_required) seeded.error = L"Codex is not signed in. Run 'codex login' and refresh.";
    else seeded.error = wide_text(parsed.value.find("error"));
    seeded.session = make_window(parsed.value.find("primary"), L"Session");
    seeded.weekly = make_window(parsed.value.find("secondary"), L"Weekly");
    seeded.plan = wide_text(parsed.value.find("planName"));
    if (const JsonValue* cost = parsed.value.find("cost")) {
        seeded.credits = number(cost->find("balance"));
        SpendSummary spend;
        spend.one_day_usd = number(cost->find("oneDayUsd"));
        spend.seven_day_usd = number(cost->find("sevenDayUsd"));
        spend.thirty_day_usd = number(cost->find("thirtyDayUsd"));
        spend.files_scanned = 12;
        const auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
        constexpr std::array models{"gpt-5.6-sol", "gpt-5.6-luna", "gpt-5.6-terra", "gpt-5.4"};
        for (std::size_t day = 0; day < 30; ++day) {
            DailyModelUsage daily{today - std::chrono::days{29 - static_cast<int>(day)}, {}};
            for (std::size_t model = 0; model < models.size(); ++model) {
                const std::size_t pulse = (day * (model + 3) + model * 7) % 13;
                const std::size_t usage = pulse < 3 ? 0 : (pulse + 1) * (4 - model);
                if (usage == 0) continue;
                daily.models.push_back(ModelUsageAmount{models[model], usage,
                    static_cast<double>(usage) * (0.018 - static_cast<double>(model) * 0.003), false});
                spend.priced_events += usage;
            }
            spend.daily_model_usage.push_back(std::move(daily));
        }
        const double project_total = spend.thirty_day_usd.value_or(31.58);
        spend.top_projects = {
            {"codex_usage", 418, project_total * 0.38, 38.0, false},
            {"partner-dashboard", 302, project_total * 0.25, 25.0, false},
            {"native-shell", 219, project_total * 0.17, 17.0, false},
            {"analytics-lab", 161, project_total * 0.12, 12.0, false},
            {"docs-and-release", 97, project_total * 0.08, 8.0, false},
        };
        if (spend.one_day_usd || spend.seven_day_usd || spend.thirty_day_usd) seeded.spend = spend;
    }
    if (!seeded.session && !seeded.weekly && !setup_required) return false;
    seeded.stale = !seeded.error.empty() && (seeded.session || seeded.weekly);
    std::scoped_lock lock(snapshot_mutex_);
    snapshot_ = std::move(seeded);
    return true;
}

void App::Quit() {
    if (refresh_thread_.joinable()) refresh_thread_.request_stop();
    if (update_thread_.joinable()) update_thread_.request_stop();
    RemoveTrayIcon();
    DestroyWindow(settings_window_);
    DestroyWindow(popup_);
    PostQuitMessage(0);
}

POINT App::LogicalPoint(HWND window, LPARAM lparam) const noexcept {
    const UINT dpi = GetDpiForWindow(window);
    const float x = static_cast<float>(MulDiv(GET_X_LPARAM(lparam), 96, static_cast<int>(dpi))) / ui::kContentScale;
    const float y = static_cast<float>(MulDiv(GET_Y_LPARAM(lparam), 96, static_cast<int>(dpi))) / ui::kContentScale;
    return {static_cast<LONG>(std::lround(x)), static_cast<LONG>(std::lround(y))};
}

UsageSnapshot App::SnapshotCopy() const {
    std::scoped_lock lock(snapshot_mutex_);
    return snapshot_;
}

RefreshPhase App::CurrentRefreshPhase() const noexcept {
    return DeriveRefreshPhase(usage_refreshing_.load(), spend_refreshing_.load());
}

LRESULT CALLBACK App::PopupProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app ? app->OnPopupMessage(window, message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK App::SettingsProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app ? app->OnSettingsMessage(window, message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK App::FloatBarProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app ? app->OnFloatBarMessage(window, message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT App::OnPopupMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (taskbar_created_message_ && message == taskbar_created_message_) {
        tray_added_ = false;
        (void)AddTrayIcon();
        return 0;
    }
    switch (message) {
    case WM_POWERBROADCAST:
        if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND ||
            wparam == PBT_APMRESUMECRITICAL) {
            HandleSystemResume();
        }
        return TRUE;
    case WM_TIMECHANGE:
        UpdateTrayTooltip();
        InvalidateRect(window, nullptr, FALSE);
        InvalidateRect(settings_window_, nullptr, FALSE);
        InvalidateRect(float_bar_window_, nullptr, FALSE);
        return 0;
    case WM_HOTKEY:
        if (wparam == kGlobalShortcutId) {
            if (IsWindowVisible(popup_) && GetForegroundWindow() == popup_) HidePopup();
            else ShowPopup();
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_GETOBJECT: {
        const LRESULT result = accessibility::HandleGetObject(window, wparam, lparam);
        return result != 0 ? result : DefWindowProcW(window, message, wparam, lparam);
    }
    case accessibility::kQueryElementsMessage: {
        auto* elements = reinterpret_cast<std::vector<accessibility::Element>*>(lparam);
        if (!elements) return FALSE;
        *elements = accessibility::BuildPopupElements(SnapshotCopy(), ShouldUseChinese(settings_.language),
            settings_.hide_identity, CurrentRefreshPhase(), usage_summary_copy_state_, popup_external_feedback_,
            GetFocus() == window ? popup_accessible_focus_ : ui::PopupAction::None, popup_pressed_,
            refresh_coordinator_.queued());
        return TRUE;
    }
    case accessibility::kActivateElementMessage: {
        const auto elements = accessibility::BuildPopupElements(SnapshotCopy(), ShouldUseChinese(settings_.language),
            settings_.hide_identity, CurrentRefreshPhase(), usage_summary_copy_state_, popup_external_feedback_, popup_hover_, popup_pressed_,
            refresh_coordinator_.queued());
        const auto found = std::find_if(elements.begin(), elements.end(), [wparam](const accessibility::Element& element) {
            return element.child_id == static_cast<long>(wparam);
        });
        if (found != elements.end() && found->command != 0) {
            ActivatePopupAction(static_cast<ui::PopupAction>(found->command));
        }
        return 0;
    }
    case accessibility::kFocusElementMessage: {
        const auto elements = accessibility::BuildPopupElements(SnapshotCopy(), ShouldUseChinese(settings_.language),
            settings_.hide_identity, CurrentRefreshPhase(), usage_summary_copy_state_, popup_external_feedback_, popup_hover_, popup_pressed_,
            refresh_coordinator_.queued());
        const auto found = std::find_if(elements.begin(), elements.end(), [wparam](const accessibility::Element& element) {
            return element.child_id == static_cast<long>(wparam);
        });
        if (found != elements.end() && found->command != 0) {
            SetFocus(window);
            SetPopupAccessibleFocus(static_cast<ui::PopupAction>(found->command));
            SetPopupHover(static_cast<ui::PopupAction>(found->command));
        }
        return 0;
    }
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTTAB;
    case WM_ERASEBKGND: return 1;
    case WM_PRINTCLIENT: {
        const UsageSnapshot snapshot = SnapshotCopy();
        ui::PaintPopup(window, reinterpret_cast<HDC>(wparam), snapshot, ShouldUseLightTheme(settings_.theme),
            ShouldUseChinese(settings_.language), settings_.hide_identity, CurrentRefreshPhase(), usage_summary_copy_state_, popup_external_feedback_,
            popup_hover_, popup_pressed_, popup_hover_progress_, refresh_angle_, refresh_coordinator_.queued(), registered_global_shortcut_);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        const UsageSnapshot snapshot = SnapshotCopy();
        PaintDoubleBuffered(window, dc, [&](HDC buffer) {
            ui::PaintPopup(window, buffer, snapshot, ShouldUseLightTheme(settings_.theme), ShouldUseChinese(settings_.language), settings_.hide_identity, CurrentRefreshPhase(), usage_summary_copy_state_, popup_external_feedback_, popup_hover_, popup_pressed_, popup_hover_progress_, refresh_angle_, refresh_coordinator_.queued(), registered_global_shortcut_);
        });
        EndPaint(window, &paint);
        return 0;
    }
    case WM_NCHITTEST: {
        const LRESULT result = DefWindowProcW(window, message, wparam, lparam);
        if (result != HTCLIENT) return result;
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(window, &point);
        const UINT dpi = GetDpiForWindow(window);
        point.x = static_cast<LONG>(std::lround(
            static_cast<float>(MulDiv(point.x, 96, static_cast<int>(dpi))) / ui::kContentScale));
        point.y = static_cast<LONG>(std::lround(
            static_cast<float>(MulDiv(point.y, 96, static_cast<int>(dpi))) / ui::kContentScale));
        if (point.y >= 0 && point.y < 62 && ui::HitTestPopup(
            point, ui::ResolvePopupLayout(SnapshotCopy()).primary_y) == ui::PopupAction::None) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_NCLBUTTONDOWN:
        popup_activation_handoff_pending_ = false;
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_MOUSEMOVE: {
        const ui::PopupAction action = ui::HitTestPopup(
            LogicalPoint(window, lparam), ui::ResolvePopupLayout(SnapshotCopy()).primary_y);
        SetPopupHover(action);
        SetCursor(LoadCursorW(nullptr, action == ui::PopupAction::None ? IDC_ARROW : IDC_HAND));
        if (!popup_tracking_mouse_) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            popup_tracking_mouse_ = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        popup_tracking_mouse_ = false;
        SetPopupHover(ui::PopupAction::None);
        return 0;
    case WM_LBUTTONDOWN:
        popup_activation_handoff_pending_ = false;
        popup_pressed_ = ui::HitTestPopup(
            LogicalPoint(window, lparam), ui::ResolvePopupLayout(SnapshotCopy()).primary_y);
        if (popup_pressed_ != ui::PopupAction::None) {
            SetFocus(window);
            SetPopupAccessibleFocus(popup_pressed_);
            SetCapture(window);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const ui::PopupAction action = ui::HitTestPopup(
            LogicalPoint(window, lparam), ui::ResolvePopupLayout(SnapshotCopy()).primary_y);
        const bool activate = action != ui::PopupAction::None && action == popup_pressed_;
        if (GetCapture() == window) ReleaseCapture();
        if (!activate) {
            popup_pressed_ = ui::PopupAction::None;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        SetTimer(window, kPressFeedbackTimer, 90, nullptr);
        ActivatePopupAction(action);
        return 0;
    }
    case WM_KEYDOWN:
        popup_activation_handoff_pending_ = false;
        if (wparam == VK_ESCAPE) HidePopup();
        else if (wparam == VK_F5) {
            RestartRefreshTimer();
            RefreshAsync();
        }
        else if (wparam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            ActivatePopupAction(ui::PopupAction::CopySummary);
        }
        else if (wparam == VK_TAB || wparam == VK_RIGHT || wparam == VK_DOWN) {
            const int direction = wparam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
            const ui::PopupAction next = ui::StepPopupAction(popup_accessible_focus_, direction);
            SetPopupAccessibleFocus(next);
            SetPopupHover(next);
        } else if (wparam == VK_LEFT || wparam == VK_UP) {
            const ui::PopupAction next = ui::StepPopupAction(popup_accessible_focus_, -1);
            SetPopupAccessibleFocus(next);
            SetPopupHover(next);
        } else if ((wparam == VK_RETURN || wparam == VK_SPACE) && popup_hover_ != ui::PopupAction::None) {
            popup_pressed_ = popup_hover_;
            SetTimer(window, kPressFeedbackTimer, 90, nullptr);
            ActivatePopupAction(popup_hover_);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && ui::ShouldDismissPopupOnDeactivate(
            IsWindowVisible(settings_window_) != FALSE,
            !EnvironmentValue(L"CODEX_PARTNER_PROOF_MODE").empty(),
            popup_activation_handoff_pending_)) {
            HidePopup();
        }
        return 0;
    case WM_TIMER:
        if (wparam == kRefreshTimer) RefreshAsync(false);
        else if (wparam == kPressFeedbackTimer) {
            KillTimer(window, kPressFeedbackTimer);
            popup_pressed_ = ui::PopupAction::None;
            InvalidateRect(window, nullptr, FALSE);
        } else if (wparam == kRefreshAnimationTimer) {
            refresh_angle_ = std::fmod(refresh_angle_ + 12.0F, 360.0F);
            InvalidateRect(window, nullptr, FALSE);
        } else if (wparam == kHoverAnimationTimer) TickPopupHoverAnimation();
        else if (wparam == kCopyFeedbackTimer) {
            KillTimer(window, kCopyFeedbackTimer);
            usage_summary_copy_state_ = ui::CopySummaryState::Idle;
            InvalidateRect(window, nullptr, FALSE);
            NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window, OBJID_CLIENT, 4);
        }
        return 0;
    case kUsageUpdated:
        if (RefreshIsActive(CurrentRefreshPhase()) && IsWindowVisible(window) && AnimationsEnabled()) {
            SetTimer(window, kRefreshAnimationTimer, 16, nullptr);
        } else if (!RefreshIsActive(CurrentRefreshPhase())) {
            KillTimer(window, kRefreshAnimationTimer);
            refresh_angle_ = 0.0F;
        }
        UpdateTrayTooltip();
        MaybeNotifyUsage(SnapshotCopy());
        InvalidateRect(window, nullptr, FALSE);
        InvalidateRect(settings_window_, nullptr, FALSE);
        InvalidateRect(float_bar_window_, nullptr, FALSE);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window, OBJID_CLIENT, CHILDID_SELF);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings_window_, OBJID_CLIENT, CHILDID_SELF);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, float_bar_window_, OBJID_CLIENT, 1);
        return 0;
    case kSpendUpdated:
        // The presentation phase changes on the UI thread so an input event
        // cannot observe a false idle gap between worker completion and this
        // cycle's serialized coordinator transition.
        spend_refreshing_ = false;
        if (refresh_coordinator_.FinishCycle()) {
            StartRefreshCycle();
        } else {
            KillTimer(window, kRefreshAnimationTimer);
            refresh_angle_ = 0.0F;
        }
        UpdateTrayTooltip();
        InvalidateRect(window, nullptr, FALSE);
        InvalidateRect(settings_window_, nullptr, FALSE);
        usage_chart_progress_ = AnimationsEnabled() ? 0.0F : 1.0F;
        if (usage_chart_progress_ < 1.0F && settings_tab_ == ui::SettingsTab::UsageSpend &&
            IsWindowVisible(settings_window_)) SetTimer(settings_window_, kUsageChartAnimationTimer, 16, nullptr);
        InvalidateRect(float_bar_window_, nullptr, FALSE);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window, OBJID_CLIENT, 13);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, settings_window_, OBJID_CLIENT, 40);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, float_bar_window_, OBJID_CLIENT, 1);
        return 0;
    case kActivateExisting:
        // A process launched from Explorer, a terminal, or automation may briefly
        // receive foreground ownership and lose it again while it exits. Keep the
        // requested surface visible until the user actually acknowledges it.
        popup_activation_handoff_pending_ = true;
        ShowPopup();
        if (EnvironmentValue(L"CODEX_PARTNER_PROOF_MODE") == L"handoff") SaveRequestedProof(popup_);
        return 0;
    case kTrayMessage: {
        const UINT event = LOWORD(lparam);
        if (event == WM_LBUTTONDBLCLK) {
            tray_double_click_pending_ = true;
            ShowPopup();
        } else if (event == WM_LBUTTONUP) {
            if (tray_double_click_pending_) {
                tray_double_click_pending_ = false;
                ShowPopup();
            } else TogglePopup();
        } else if (event == NIN_SELECT || event == NIN_KEYSELECT) TogglePopup();
        else if (event == NIN_BALLOONUSERCLICK) ShowPopup();
        else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
            POINT point{GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)};
            if (point.x == 0 && point.y == 0) GetCursorPos(&point);
            ShowTrayMenu(point);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SETTINGCHANGE:
        ApplyTheme(window);
        InvalidateRect(window, nullptr, FALSE);
        InvalidateRect(settings_window_, nullptr, FALSE);
        ApplyTheme(float_bar_window_);
        InvalidateRect(float_bar_window_, nullptr, FALSE);
        return 0;
    case WM_DESTROY: return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}

LRESULT App::OnSettingsMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_GETOBJECT: {
        const LRESULT result = accessibility::HandleGetObject(window, wparam, lparam);
        return result != 0 ? result : DefWindowProcW(window, message, wparam, lparam);
    }
    case accessibility::kQueryElementsMessage: {
        auto* elements = reinterpret_cast<std::vector<accessibility::Element>*>(lparam);
        if (!elements) return FALSE;
        *elements = accessibility::BuildSettingsElements(settings_, SnapshotCopy(), update_check_, settings_persistence_, settings_external_feedback_, CurrentRefreshPhase(), settings_tab_,
            ShouldUseChinese(settings_.language), GetFocus() == window ? settings_accessible_focus_ : ui::SettingsAction::None, settings_pressed_, global_shortcut_status_);
        return TRUE;
    }
    case accessibility::kActivateElementMessage: {
        const auto elements = accessibility::BuildSettingsElements(settings_, SnapshotCopy(), update_check_, settings_persistence_, settings_external_feedback_, CurrentRefreshPhase(), settings_tab_,
            ShouldUseChinese(settings_.language), settings_hover_, settings_pressed_, global_shortcut_status_);
        const auto found = std::find_if(elements.begin(), elements.end(), [wparam](const accessibility::Element& element) {
            return element.child_id == static_cast<long>(wparam);
        });
        if (found != elements.end() && found->command != 0) {
            ActivateSettingsAction(static_cast<ui::SettingsAction>(found->command));
        }
        return 0;
    }
    case accessibility::kFocusElementMessage: {
        const auto elements = accessibility::BuildSettingsElements(settings_, SnapshotCopy(), update_check_, settings_persistence_, settings_external_feedback_, CurrentRefreshPhase(), settings_tab_,
            ShouldUseChinese(settings_.language), settings_hover_, settings_pressed_, global_shortcut_status_);
        const auto found = std::find_if(elements.begin(), elements.end(), [wparam](const accessibility::Element& element) {
            return element.child_id == static_cast<long>(wparam);
        });
        if (found != elements.end() && found->command != 0) {
            SetFocus(window);
            SetSettingsAccessibleFocus(static_cast<ui::SettingsAction>(found->command));
            SetSettingsHover(static_cast<ui::SettingsAction>(found->command));
        }
        return 0;
    }
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTTAB;
    case WM_ERASEBKGND: return 1;
    case WM_PRINTCLIENT: {
        const UsageSnapshot snapshot = SnapshotCopy();
        ui::PaintSettings(window, reinterpret_cast<HDC>(wparam), settings_, snapshot, update_check_, settings_tab_,
            ShouldUseLightTheme(settings_.theme), ShouldUseChinese(settings_.language), settings_persistence_,
            diagnostics_copied_, settings_external_feedback_, CurrentRefreshPhase(), settings_hover_, settings_pressed_, settings_hover_progress_, global_shortcut_status_,
            usage_chart_hover_, usage_chart_progress_);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        const UsageSnapshot snapshot = SnapshotCopy();
        PaintDoubleBuffered(window, dc, [&](HDC buffer) {
            ui::PaintSettings(window, buffer, settings_, snapshot, update_check_, settings_tab_, ShouldUseLightTheme(settings_.theme), ShouldUseChinese(settings_.language), settings_persistence_, diagnostics_copied_, settings_external_feedback_, CurrentRefreshPhase(), settings_hover_, settings_pressed_, settings_hover_progress_, global_shortcut_status_, usage_chart_hover_, usage_chart_progress_);
        });
        EndPaint(window, &paint);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const POINT logical = LogicalPoint(window, lparam);
        const ui::SettingsAction action = ui::HitTestSettings(logical, settings_tab_);
        const auto chart_hover = settings_tab_ == ui::SettingsTab::UsageSpend ? ui::HitTestUsageChart(logical) : std::nullopt;
        if (chart_hover != usage_chart_hover_) {
            usage_chart_hover_ = chart_hover;
            InvalidateRect(window, nullptr, FALSE);
        }
        SetSettingsHover(action);
        SetCursor(LoadCursorW(nullptr, action == ui::SettingsAction::None ?
            (chart_hover ? IDC_CROSS : IDC_ARROW) : IDC_HAND));
        if (!settings_tracking_mouse_) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            settings_tracking_mouse_ = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        settings_tracking_mouse_ = false;
        usage_chart_hover_.reset();
        SetSettingsHover(ui::SettingsAction::None);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        settings_pressed_ = ui::HitTestSettings(LogicalPoint(window, lparam), settings_tab_);
        if (settings_pressed_ != ui::SettingsAction::None) {
            SetFocus(window);
            SetSettingsAccessibleFocus(settings_pressed_);
            SetCapture(window);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const ui::SettingsAction action = ui::HitTestSettings(LogicalPoint(window, lparam), settings_tab_);
        const bool activate = action != ui::SettingsAction::None && action == settings_pressed_;
        if (GetCapture() == window) ReleaseCapture();
        if (!activate) {
            settings_pressed_ = ui::SettingsAction::None;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        SetTimer(window, kPressFeedbackTimer, 90, nullptr);
        ActivateSettingsAction(action);
        return 0;
    }
    case WM_TIMER:
        if (wparam == kPressFeedbackTimer) {
            KillTimer(window, kPressFeedbackTimer);
            settings_pressed_ = ui::SettingsAction::None;
            InvalidateRect(window, nullptr, FALSE);
        } else if (wparam == kHoverAnimationTimer) TickSettingsHoverAnimation();
        else if (wparam == kUsageChartAnimationTimer) {
            usage_chart_progress_ = std::min(1.0F, usage_chart_progress_ + 0.055F);
            if (usage_chart_progress_ >= 1.0F) KillTimer(window, kUsageChartAnimationTimer);
            InvalidateRect(window, nullptr, FALSE);
        }
        else if (wparam == kExternalFeedbackTimer) {
            KillTimer(window, kExternalFeedbackTimer);
            if (settings_external_feedback_.outcome == ExternalActionOutcome::Opened) {
                settings_external_feedback_ = {};
                NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window, OBJID_CLIENT, 60);
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        else if (wparam == kSavedFeedbackTimer) {
            KillTimer(window, kSavedFeedbackTimer);
            if (settings_persistence_ == SettingsPersistenceState::Saved) {
                settings_persistence_ = SettingsPersistenceState::Idle;
                NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window, OBJID_CLIENT, 60);
            }
            diagnostics_copied_ = false;
            InvalidateRect(window, nullptr, FALSE);
        } else if (wparam == kSnoozeStatusTimer) {
            if (!IsNotificationSnoozed(settings_.notification_snoozed_until)) {
                KillTimer(window, kSnoozeStatusTimer);
            }
            InvalidateRect(window, nullptr, FALSE);
            NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window, OBJID_CLIENT, 33);
        }
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) HideSettings();
        else if (wparam == VK_TAB || wparam == VK_RIGHT || wparam == VK_DOWN) {
            const int direction = wparam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
            const ui::SettingsAction next = ui::StepSettingsAction(settings_tab_, settings_accessible_focus_, direction);
            SetSettingsAccessibleFocus(next);
            SetSettingsHover(next);
        } else if (wparam == VK_LEFT || wparam == VK_UP) {
            const ui::SettingsAction next = ui::StepSettingsAction(settings_tab_, settings_accessible_focus_, -1);
            SetSettingsAccessibleFocus(next);
            SetSettingsHover(next);
        } else if ((wparam == VK_RETURN || wparam == VK_SPACE) && settings_hover_ != ui::SettingsAction::None) {
            settings_pressed_ = settings_hover_;
            SetTimer(window, kPressFeedbackTimer, 90, nullptr);
            ActivateSettingsAction(settings_hover_);
        }
        return 0;
    case kUpdateChecked: {
        std::optional<UpdateCheckState> completed;
        {
            std::scoped_lock lock(update_mutex_);
            completed = std::move(pending_update_check_);
            pending_update_check_.reset();
        }
        if (completed) update_check_ = std::move(*completed);
        InvalidateRect(window, nullptr, FALSE);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window, OBJID_CLIENT, 53);
        NotifyWinEvent(EVENT_OBJECT_STATECHANGE, window, OBJID_CLIENT, 53);
        return 0;
    }
    case WM_CLOSE:
        HideSettings();
        return 0;
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SETTINGCHANGE:
        ApplyTheme(window);
        InvalidateRect(window, nullptr, FALSE);
        InvalidateRect(popup_, nullptr, FALSE);
        ApplyTheme(float_bar_window_);
        InvalidateRect(float_bar_window_, nullptr, FALSE);
        return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}

LRESULT App::OnFloatBarMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_GETOBJECT: {
        const LRESULT result = accessibility::HandleGetObject(window, wparam, lparam);
        return result != 0 ? result : DefWindowProcW(window, message, wparam, lparam);
    }
    case accessibility::kQueryElementsMessage: {
        auto* elements = reinterpret_cast<std::vector<accessibility::Element>*>(lparam);
        if (!elements) return FALSE;
        *elements = accessibility::BuildFloatBarElements(SnapshotCopy(), ShouldUseChinese(settings_.language),
            settings_.hide_identity, CurrentRefreshPhase(), GetFocus() == window ? float_bar_accessible_focus_ : ui::FloatBarAction::None,
            float_bar_pressed_);
        return TRUE;
    }
    case accessibility::kActivateElementMessage: {
        const auto elements = accessibility::BuildFloatBarElements(SnapshotCopy(), ShouldUseChinese(settings_.language),
            settings_.hide_identity, CurrentRefreshPhase(), float_bar_hover_, float_bar_pressed_);
        const auto found = std::find_if(elements.begin(), elements.end(), [wparam](const accessibility::Element& element) {
            return element.child_id == static_cast<long>(wparam);
        });
        if (found != elements.end() && found->command != 0) {
            ActivateFloatBarAction(static_cast<ui::FloatBarAction>(found->command));
        }
        return 0;
    }
    case accessibility::kFocusElementMessage: {
        const auto elements = accessibility::BuildFloatBarElements(SnapshotCopy(), ShouldUseChinese(settings_.language),
            settings_.hide_identity, CurrentRefreshPhase(), float_bar_hover_, float_bar_pressed_);
        const auto found = std::find_if(elements.begin(), elements.end(), [wparam](const accessibility::Element& element) {
            return element.child_id == static_cast<long>(wparam);
        });
        if (found != elements.end() && found->command != 0) {
            SetFocus(window);
            SetFloatBarAccessibleFocus(static_cast<ui::FloatBarAction>(found->command));
            SetFloatBarHover(static_cast<ui::FloatBarAction>(found->command));
        }
        return 0;
    }
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTTAB;
    case WM_ERASEBKGND: return 1;
    case WM_PRINTCLIENT: {
        const UsageSnapshot snapshot = SnapshotCopy();
        ui::PaintFloatBar(window, reinterpret_cast<HDC>(wparam), snapshot, ShouldUseLightTheme(settings_.theme),
            ShouldUseChinese(settings_.language), settings_.hide_identity, CurrentRefreshPhase(), float_bar_hover_, float_bar_pressed_,
            float_bar_hover_progress_);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        const UsageSnapshot snapshot = SnapshotCopy();
        PaintDoubleBuffered(window, dc, [&](HDC buffer) {
            ui::PaintFloatBar(window, buffer, snapshot, ShouldUseLightTheme(settings_.theme),
                ShouldUseChinese(settings_.language), settings_.hide_identity, CurrentRefreshPhase(), float_bar_hover_,
                float_bar_pressed_, float_bar_hover_progress_);
        });
        EndPaint(window, &paint);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (GetCapture() == window && float_bar_pressed_ == ui::FloatBarAction::OpenPopup &&
            (wparam & MK_LBUTTON) != 0) {
            POINT cursor{};
            GetCursorPos(&cursor);
            const int distance_x = std::abs(cursor.x - float_bar_press_screen_.x);
            const int distance_y = std::abs(cursor.y - float_bar_press_screen_.y);
            if (distance_x >= GetSystemMetrics(SM_CXDRAG) || distance_y >= GetSystemMetrics(SM_CYDRAG)) {
                float_bar_dragging_ = true;
                float_bar_pressed_ = ui::FloatBarAction::None;
                ReleaseCapture();
                SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(cursor.x, cursor.y));
                return 0;
            }
        }
        const ui::FloatBarAction action = ui::HitTestFloatBar(LogicalPoint(window, lparam));
        SetFloatBarHover(action);
        SetCursor(LoadCursorW(nullptr, action == ui::FloatBarAction::Hide ? IDC_HAND : IDC_SIZEALL));
        if (!float_bar_tracking_mouse_) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            float_bar_tracking_mouse_ = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        float_bar_tracking_mouse_ = false;
        SetFloatBarHover(ui::FloatBarAction::None);
        return 0;
    case WM_LBUTTONDOWN:
        float_bar_pressed_ = ui::HitTestFloatBar(LogicalPoint(window, lparam));
        if (float_bar_pressed_ != ui::FloatBarAction::None) {
            GetCursorPos(&float_bar_press_screen_);
            SetFocus(window);
            SetFloatBarAccessibleFocus(float_bar_pressed_);
            SetCapture(window);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const ui::FloatBarAction action = ui::HitTestFloatBar(LogicalPoint(window, lparam));
        const bool activate = !float_bar_dragging_ && action != ui::FloatBarAction::None && action == float_bar_pressed_;
        if (GetCapture() == window) ReleaseCapture();
        float_bar_dragging_ = false;
        if (!activate) {
            float_bar_pressed_ = ui::FloatBarAction::None;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        SetTimer(window, kPressFeedbackTimer, 90, nullptr);
        ActivateFloatBarAction(action);
        return 0;
    }
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) HideFloatBar(true);
        else if (wparam == VK_TAB || wparam == VK_RIGHT || wparam == VK_DOWN) {
            const int direction = wparam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
            const ui::FloatBarAction next = ui::StepFloatBarAction(float_bar_accessible_focus_, direction);
            SetFloatBarAccessibleFocus(next);
            SetFloatBarHover(next);
        } else if (wparam == VK_LEFT || wparam == VK_UP) {
            const ui::FloatBarAction next = ui::StepFloatBarAction(float_bar_accessible_focus_, -1);
            SetFloatBarAccessibleFocus(next);
            SetFloatBarHover(next);
        } else if ((wparam == VK_RETURN || wparam == VK_SPACE) && float_bar_hover_ != ui::FloatBarAction::None) {
            float_bar_pressed_ = float_bar_hover_;
            SetTimer(window, kPressFeedbackTimer, 90, nullptr);
            ActivateFloatBarAction(float_bar_hover_);
        }
        return 0;
    case WM_TIMER:
        if (wparam == kPressFeedbackTimer) {
            KillTimer(window, kPressFeedbackTimer);
            float_bar_pressed_ = ui::FloatBarAction::None;
            InvalidateRect(window, nullptr, FALSE);
        } else if (wparam == kHoverAnimationTimer) TickFloatBarHoverAnimation();
        return 0;
    case WM_ENTERSIZEMOVE:
        float_bar_dragging_ = true;
        return 0;
    case WM_EXITSIZEMOVE:
        float_bar_dragging_ = false;
        float_bar_pressed_ = ui::FloatBarAction::None;
        PersistFloatBarPosition();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        HideFloatBar(true);
        return 0;
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        positioning_float_bar_ = true;
        SetWindowPos(window, HWND_TOPMOST, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOACTIVATE);
        positioning_float_bar_ = false;
        PersistFloatBarPosition();
        return 0;
    }
    case WM_SETTINGCHANGE:
        ApplyTheme(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

}  // namespace codex_partner
