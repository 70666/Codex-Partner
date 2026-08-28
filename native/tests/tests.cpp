#include "json.h"
#include "accessibility_model.h"
#include "backdrop_policy.h"
#include "codex_cost_scanner.h"
#include "codex_provider.h"
#include "diagnostics.h"
#include "float_bar_model.h"
#include "interaction_model.h"
#include "notification_snooze.h"
#include "premultiplied_surface.h"
#include "refresh_coordinator.h"
#include "resume_refresh_model.h"
#include "settings_store.h"
#include "tray_icon_model.h"
#include "tray_icon_win32.h"
#include "usage_cache.h"
#include "usage_freshness.h"
#include "usage_model.h"
#include "usage_summary.h"
#include "update_check.h"
#include "window_placement.h"
#include "win_http.h"

#include <Windows.h>

#include <array>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <string_view>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--scan") {
        const auto spend = codex_partner::ScanCodexSpend(std::filesystem::path(argv[2]));
        std::cout << std::fixed << std::setprecision(4)
                  << "one_day_usd=" << spend.one_day_usd.value_or(0.0) << '\n'
                  << "seven_day_usd=" << spend.seven_day_usd.value_or(0.0) << '\n'
                  << "thirty_day_usd=" << spend.thirty_day_usd.value_or(0.0) << '\n'
                  << "files_scanned=" << spend.files_scanned << '\n'
                  << "priced_events=" << spend.priced_events << '\n'
                  << "unpriced_events=" << spend.unpriced_events << '\n'
                  << "priced_input_tokens=" << spend.priced_input_tokens << '\n'
                  << "priced_cached_input_tokens=" << spend.priced_cached_input_tokens << '\n'
                  << "priced_cache_write_input_tokens=" << spend.priced_cache_write_input_tokens << '\n'
                  << "priced_output_tokens=" << spend.priced_output_tokens << '\n'
                  << "unpriced_input_tokens=" << spend.unpriced_input_tokens << '\n'
                  << "unpriced_cached_input_tokens=" << spend.unpriced_cached_input_tokens << '\n'
                  << "unpriced_cache_write_input_tokens=" << spend.unpriced_cache_write_input_tokens << '\n'
                  << "unpriced_output_tokens=" << spend.unpriced_output_tokens << '\n'
                  << "unpriced_models=";
        for (std::size_t index = 0; index < spend.unpriced_models.size(); ++index) {
            if (index > 0) std::cout << ',';
            std::cout << spend.unpriced_models[index];
        }
        std::cout << '\n'
                  << "one_day_partial=" << (spend.one_day_partial ? "true" : "false") << '\n'
                  << "seven_day_partial=" << (spend.seven_day_partial ? "true" : "false") << '\n'
                  << "thirty_day_partial=" << (spend.thirty_day_partial ? "true" : "false") << '\n'
                  << "partial=" << (spend.partial ? "true" : "false") << '\n';
        return 0;
    }
    {
        using codex_partner::ui::BackdropStyle;
        Require(codex_partner::ui::ResolveBackdropStyle(false, true, false) == BackdropStyle::Solid,
            "glass capability is required for a transparent surface");
        Require(codex_partner::ui::ResolveBackdropStyle(true, true, false) == BackdropStyle::AcrylicGlass,
            "an active interactive panel uses system acrylic");
        Require(codex_partner::ui::ResolveBackdropStyle(true, false, false) == BackdropStyle::TransparentGlass,
            "an inactive panel avoids acrylic's opaque fallback");
        Require(codex_partner::ui::ResolveBackdropStyle(true, true, true) == BackdropStyle::TransparentGlass,
            "the persistent floating bar remains transparent");
        Require(codex_partner::ui::BackdropPreservesAlpha(BackdropStyle::AcrylicGlass) &&
                codex_partner::ui::BackdropPreservesAlpha(BackdropStyle::TransparentGlass) &&
                !codex_partner::ui::BackdropPreservesAlpha(BackdropStyle::Solid),
            "only glass surfaces preserve the redirection bitmap alpha");

        std::array<std::uint32_t, 4> glass_pixels{
            0x00123456U, 0x807D6E76U, 0x8040C020U, 0xFF342A32U};
        codex_partner::rendering::FinalizeBgra(glass_pixels, true);
        Require(glass_pixels[0] == 0U, "fully transparent glass pixels have zero RGB");
        Require(glass_pixels[1] == 0x807D6E76U,
            "already-premultiplied GDI+ pixels are not multiplied twice");
        Require(glass_pixels[2] == 0x80408020U,
            "out-of-contract channels are clamped to the premultiplied invariant");
        Require(codex_partner::rendering::IsPremultipliedBgra(glass_pixels),
            "every glass-buffer channel is bounded by its alpha value");

        std::array<std::uint32_t, 2> solid_pixels{0x00112233U, 0x80112233U};
        codex_partner::rendering::FinalizeBgra(solid_pixels, false);
        Require(solid_pixels[0] == 0xFF112233U && solid_pixels[1] == 0xFF112233U,
            "solid-buffer finalization preserves RGB and forces opaque alpha");
    }
    const auto parsed = codex_partner::ParseJson(R"({"tokens":{"access_token":"secret","account_id":"acct"},"rate_limit":{"primary_window":{"used_percent":46,"limit_window_seconds":18000}}})");
    Require(parsed.ok(), "representative Codex JSON parses");
    const auto* tokens = parsed.value.find("tokens");
    Require(tokens != nullptr, "tokens object exists");
    Require(tokens->find("access_token")->as_string() == "secret", "nested string is available");

    const auto reset_now = std::chrono::system_clock::time_point{std::chrono::seconds{1'800'000'000}};
    const auto relative_reset = codex_partner::ResolveCodexResetTime(std::nullopt, 3600.0, reset_now);
    Require(relative_reset && *relative_reset == reset_now + std::chrono::hours{1},
        "relative Codex reset_after_seconds becomes an absolute reset time");
    const auto millisecond_reset = codex_partner::ResolveCodexResetTime(1'800'007'200'000.0, std::nullopt, reset_now);
    Require(millisecond_reset && *millisecond_reset == reset_now + std::chrono::hours{2},
        "millisecond Codex reset_at values are normalized to Unix seconds");
    const auto absolute_reset = codex_partner::ResolveCodexResetTime(1'800'010'800.0, 20.0, reset_now);
    Require(absolute_reset && *absolute_reset == reset_now + std::chrono::hours{3},
        "absolute Codex reset_at takes precedence over a relative fallback");

    auto sensitive_json = codex_partner::ParseJson(R"({"token":"must-not-remain","nested":["also-secret"]})");
    Require(sensitive_json.ok(), "sensitive JSON fixture parses");
    sensitive_json.value.secure_clear();
    Require(sensitive_json.value.type() == codex_partner::JsonValue::Type::Null && sensitive_json.value.size() == 0, "sensitive JSON trees can be recursively cleared after credential extraction");

    const auto invalid = codex_partner::ParseJson("{broken");
    Require(!invalid.ok(), "invalid JSON is rejected");

    const auto snooze_now = std::chrono::sys_days{std::chrono::year{2026} / 8 / 24} +
        std::chrono::hours{12};
    const auto snooze_one_hour = codex_partner::ResolveNotificationSnooze(
        codex_partner::NotificationSnoozePreset::OneHour, snooze_now);
    const auto snooze_four_hours = codex_partner::ResolveNotificationSnooze(
        codex_partner::NotificationSnoozePreset::FourHours, snooze_now);
    const auto snooze_twenty_four_hours = codex_partner::ResolveNotificationSnooze(
        codex_partner::NotificationSnoozePreset::TwentyFourHours, snooze_now);
    Require(codex_partner::ResolveNotificationSnooze(codex_partner::NotificationSnoozePreset::Active, snooze_now) ==
            std::chrono::system_clock::time_point{} &&
            snooze_one_hour == snooze_now + std::chrono::hours{1} &&
            snooze_four_hours == snooze_now + std::chrono::hours{4} &&
            snooze_twenty_four_hours == snooze_now + std::chrono::hours{24},
        "notification snooze presets resolve into bounded automatic-resume times");
    Require(codex_partner::IsNotificationSnoozed(snooze_one_hour, snooze_now) &&
            !codex_partner::IsNotificationSnoozed(snooze_one_hour, snooze_one_hour) &&
            !codex_partner::IsNotificationSnoozed(snooze_now + std::chrono::hours{24 * 8}, snooze_now),
        "notification snooze is active only before its exact expiry and seven-day safety boundaries");
    Require(codex_partner::FormatNotificationSnooze({}, false, snooze_now) == L"Active" &&
            codex_partner::FormatNotificationSnooze(snooze_now + std::chrono::minutes{58} +
                std::chrono::seconds{30}, false, snooze_now) == L"Paused · 59 min left" &&
            codex_partner::FormatNotificationSnooze(snooze_four_hours, false, snooze_now) ==
                L"Paused · 4h left" &&
            codex_partner::FormatNotificationSnooze(snooze_one_hour, true, snooze_now).find(L"后恢复") !=
                std::wstring::npos &&
            codex_partner::FormatNotificationSnoozeCompact(snooze_now + std::chrono::minutes{61},
                false, snooze_now) == L"2h" &&
            codex_partner::FormatNotificationSnoozeCompact(snooze_one_hour, true, snooze_now) == L"1 小时",
        "notification snooze labels are concise, rounded up, and localized");
    Require(codex_partner::ShouldDeliverUsageAlert(true, {}, codex_partner::UsageAlertLevel::Warning,
                codex_partner::UsageAlertLevel::None, snooze_now) &&
            !codex_partner::ShouldDeliverUsageAlert(true, snooze_one_hour,
                codex_partner::UsageAlertLevel::Critical, codex_partner::UsageAlertLevel::None, snooze_now) &&
            !codex_partner::ShouldDeliverUsageAlert(false, {}, codex_partner::UsageAlertLevel::Critical,
                codex_partner::UsageAlertLevel::None, snooze_now) &&
            !codex_partner::ShouldDeliverUsageAlert(true, {}, codex_partner::UsageAlertLevel::Warning,
                codex_partner::UsageAlertLevel::Warning, snooze_now) &&
            codex_partner::ShouldDeliverUsageAlert(true, snooze_one_hour,
                codex_partner::UsageAlertLevel::Critical, codex_partner::UsageAlertLevel::Warning,
                snooze_one_hour),
        "only enabled, unsnoozed upward threshold crossings deliver usage alerts");

    const auto resume_wall_now = std::chrono::sys_days{std::chrono::year{2026} / 8 / 24} +
        std::chrono::hours{12};
    const auto resume_steady_now = std::chrono::steady_clock::time_point{std::chrono::hours{100}};
    codex_partner::UsageSnapshot resume_snapshot;
    resume_snapshot.loading = false;
    resume_snapshot.connection = codex_partner::ProviderConnectionState::CredentialsDetected;
    resume_snapshot.session = codex_partner::RateWindow{L"Session", 44.0, 300, std::nullopt};
    resume_snapshot.updated_at = resume_wall_now - std::chrono::hours{3};
    const auto old_resume = codex_partner::EvaluateResumeRefresh(
        resume_snapshot, std::nullopt, resume_steady_now, resume_wall_now);
    Require(old_resume.accepted && old_resume.mark_usage_stale,
        "a resume event immediately degrades hours-old live usage before refreshing");
    resume_snapshot.updated_at = resume_wall_now - std::chrono::minutes{1};
    const auto fresh_resume = codex_partner::EvaluateResumeRefresh(
        resume_snapshot, std::nullopt, resume_steady_now, resume_wall_now);
    Require(fresh_resume.accepted && !fresh_resume.mark_usage_stale,
        "recent usage still refreshes on resume without briefly looking stale");
    resume_snapshot.updated_at = resume_wall_now - std::chrono::minutes{2};
    Require(codex_partner::EvaluateResumeRefresh(resume_snapshot, std::nullopt,
                resume_steady_now, resume_wall_now).mark_usage_stale,
        "the two-minute resume freshness boundary is inclusive");
    resume_snapshot.updated_at = {};
    Require(codex_partner::EvaluateResumeRefresh(resume_snapshot, std::nullopt,
                resume_steady_now, resume_wall_now).mark_usage_stale,
        "usage without a successful timestamp fails closed after resume");
    resume_snapshot.updated_at = resume_wall_now + std::chrono::minutes{6};
    Require(codex_partner::EvaluateResumeRefresh(resume_snapshot, std::nullopt,
                resume_steady_now, resume_wall_now).mark_usage_stale,
        "implausibly future usage timestamps fail closed after a clock change");
    resume_snapshot.updated_at = resume_wall_now - std::chrono::hours{3};
    const auto duplicate_resume = codex_partner::EvaluateResumeRefresh(resume_snapshot,
        resume_steady_now - std::chrono::seconds{29}, resume_steady_now, resume_wall_now);
    const auto boundary_resume = codex_partner::EvaluateResumeRefresh(resume_snapshot,
        resume_steady_now - std::chrono::seconds{30}, resume_steady_now, resume_wall_now);
    Require(!duplicate_resume.accepted && boundary_resume.accepted && boundary_resume.mark_usage_stale,
        "paired Windows resume notifications coalesce for 30 seconds and re-arm at the boundary");
    const auto reversed_steady_resume = codex_partner::EvaluateResumeRefresh(resume_snapshot,
        resume_steady_now + std::chrono::seconds{1}, resume_steady_now, resume_wall_now);
    Require(reversed_steady_resume.accepted,
        "an impossible reversed steady-clock sample fails open to one safe refresh");
    resume_snapshot.stale = true;
    const auto already_stale_resume = codex_partner::EvaluateResumeRefresh(
        resume_snapshot, std::nullopt, resume_steady_now, resume_wall_now);
    Require(already_stale_resume.accepted && !already_stale_resume.mark_usage_stale,
        "already degraded data refreshes without a redundant state transition");

    codex_partner::RefreshCoordinator refresh_coordinator;
    Require(!refresh_coordinator.active() && !refresh_coordinator.queued() &&
            refresh_coordinator.Request() == codex_partner::RefreshRequestDisposition::Started &&
            refresh_coordinator.active() && !refresh_coordinator.queued(),
        "the first refresh request starts one serialized cycle");
    Require(refresh_coordinator.Request() == codex_partner::RefreshRequestDisposition::Queued &&
            refresh_coordinator.active() && refresh_coordinator.queued(),
        "one request arriving during an active refresh is retained for a trailing cycle");
    for (int index = 0; index < 100; ++index) {
        Require(refresh_coordinator.Request() == codex_partner::RefreshRequestDisposition::Coalesced,
            "repeated refresh requests coalesce once a trailing cycle is queued");
    }
    Require(refresh_coordinator.FinishCycle() && refresh_coordinator.active() &&
            !refresh_coordinator.queued(),
        "finishing the active cycle consumes exactly one queued refresh without going idle");
    Require(refresh_coordinator.Request(false) == codex_partner::RefreshRequestDisposition::Coalesced &&
            !refresh_coordinator.queued(),
        "a periodic timer tick during active work is skipped instead of creating an endless trailing loop");
    Require(refresh_coordinator.Request() == codex_partner::RefreshRequestDisposition::Queued &&
            refresh_coordinator.FinishCycle() && !refresh_coordinator.queued() &&
            !refresh_coordinator.FinishCycle() && !refresh_coordinator.active(),
        "requests during a trailing cycle remain lossless and the coordinator returns to idle afterward");
    Require(codex_partner::DeriveRefreshPhase(false, false) == codex_partner::RefreshPhase::Idle &&
            codex_partner::DeriveRefreshPhase(true, false) == codex_partner::RefreshPhase::FetchingUsage &&
            codex_partner::DeriveRefreshPhase(true, true) == codex_partner::RefreshPhase::FetchingUsage &&
            codex_partner::DeriveRefreshPhase(false, true) == codex_partner::RefreshPhase::ScanningSpend &&
            !codex_partner::RefreshIsActive(codex_partner::RefreshPhase::Idle) &&
            codex_partner::RefreshIsActive(codex_partner::RefreshPhase::FetchingUsage) &&
            codex_partner::RefreshIsActive(codex_partner::RefreshPhase::ScanningSpend),
        "refresh phases keep live-limit fetches distinct from the longer local-spend scan");

    codex_partner::UsageSnapshot tray_snapshot;
    tray_snapshot.loading = false;
    tray_snapshot.connection = codex_partner::ProviderConnectionState::CredentialsDetected;
    tray_snapshot.session = codex_partner::RateWindow{L"Session", 46.0, 300, std::nullopt};
    tray_snapshot.weekly = codex_partner::RateWindow{L"Weekly", 82.0, 10080, std::nullopt};
    const auto tray_model = codex_partner::BuildTrayIconModel(tray_snapshot);
    Require(std::abs(tray_model.primary_used_percent - 46.0) < 0.001 &&
            tray_model.secondary_used_percent &&
            std::abs(*tray_model.secondary_used_percent - 82.0) < 0.001 &&
            !tray_model.degraded,
        "tray icon maps live session and weekly limits into two healthy bars");
    const auto tray_pixels = codex_partner::RenderTrayIconRgba(tray_model);
    const auto tray_pixel = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t offset = (static_cast<std::size_t>(y) * codex_partner::kTrayIconSize + x) * 4;
        return std::array<std::uint8_t, 4>{tray_pixels[offset], tray_pixels[offset + 1],
            tray_pixels[offset + 2], tray_pixels[offset + 3]};
    };
    Require(tray_pixels.size() == codex_partner::kTrayIconPixelBytes &&
            tray_pixel(0, 0) == std::array<std::uint8_t, 4>{0, 0, 0, 0},
        "tray icon keeps a transparent border in its fixed 32-pixel canvas");
    Require(tray_pixel(8, 10) == std::array<std::uint8_t, 4>{76, 175, 80, 255} &&
            tray_pixel(8, 20) == std::array<std::uint8_t, 4>{255, 152, 0, 255} &&
            tray_pixel(20, 10) == std::array<std::uint8_t, 4>{80, 80, 90, 255},
        "tray bars communicate healthy, urgent, and unfilled capacity with stable colors");
    const auto threshold_pixel = [](double percent) {
        const auto pixels = codex_partner::RenderTrayIconRgba({percent, std::nullopt, false});
        const std::size_t offset = (10U * codex_partner::kTrayIconSize + 4U) * 4U;
        return std::array<std::uint8_t, 3>{pixels[offset], pixels[offset + 1], pixels[offset + 2]};
    };
    Require(threshold_pixel(49.9) == std::array<std::uint8_t, 3>{76, 175, 80} &&
            threshold_pixel(50.0) == std::array<std::uint8_t, 3>{255, 193, 7} &&
            threshold_pixel(80.0) == std::array<std::uint8_t, 3>{255, 152, 0} &&
            threshold_pixel(95.0) == std::array<std::uint8_t, 3>{244, 67, 54},
        "tray pressure colors switch at the documented 50, 80, and 95 percent boundaries");

    codex_partner::UsageSnapshot weekly_only_tray = tray_snapshot;
    weekly_only_tray.session.reset();
    const auto weekly_only_model = codex_partner::BuildTrayIconModel(weekly_only_tray);
    Require(std::abs(weekly_only_model.primary_used_percent - 82.0) < 0.001 &&
            !weekly_only_model.secondary_used_percent && !weekly_only_model.degraded,
        "weekly-only data remains glanceable as one full-height tray bar");
    codex_partner::UsageSnapshot unsafe_tray = tray_snapshot;
    unsafe_tray.session->used_percent = std::numeric_limits<double>::infinity();
    unsafe_tray.weekly->used_percent = 170.0;
    const auto safe_tray_model = codex_partner::BuildTrayIconModel(unsafe_tray);
    Require(safe_tray_model.primary_used_percent == 0.0 &&
            safe_tray_model.secondary_used_percent == 100.0,
        "tray percentages sanitize non-finite and out-of-range provider values");

    codex_partner::UsageSnapshot degraded_tray = tray_snapshot;
    degraded_tray.stale = true;
    const auto degraded_model = codex_partner::BuildTrayIconModel(degraded_tray);
    const auto degraded_pixels = codex_partner::RenderTrayIconRgba(degraded_model);
    const std::size_t degraded_fill = (10U * codex_partner::kTrayIconSize + 8U) * 4U;
    Require(degraded_model.degraded &&
            degraded_pixels[degraded_fill] == degraded_pixels[degraded_fill + 1] &&
            degraded_pixels[degraded_fill + 1] == degraded_pixels[degraded_fill + 2],
        "last-known tray data is visibly desaturated instead of appearing live");
    degraded_tray.stale = false;
    degraded_tray.error = L"offline";
    Require(codex_partner::BuildTrayIconModel(degraded_tray).degraded,
        "provider errors degrade the tray state even when cached windows remain available");
    degraded_tray.error.clear();
    degraded_tray.loading = true;
    Require(codex_partner::BuildTrayIconModel(degraded_tray).degraded,
        "refreshing data uses the degraded tray state until the live snapshot arrives");

    const auto inspect_and_destroy_tray_icon = [&] {
        HICON icon = codex_partner::CreateUsageTrayIconHandle(tray_snapshot);
        Require(icon != nullptr, "usage pixels produce a native Windows tray icon");
        ICONINFO icon_info{};
        Require(GetIconInfo(icon, &icon_info) != FALSE, "native tray icon exposes valid bitmap metadata");
        BITMAP color_bitmap{};
        Require(GetObjectW(icon_info.hbmColor, sizeof(color_bitmap), &color_bitmap) == sizeof(color_bitmap) &&
                color_bitmap.bmWidth == static_cast<LONG>(codex_partner::kTrayIconSize) &&
                std::abs(color_bitmap.bmHeight) == static_cast<LONG>(codex_partner::kTrayIconSize),
            "native tray icon preserves the intended 32-pixel dimensions");
        DeleteObject(icon_info.hbmColor);
        DeleteObject(icon_info.hbmMask);
        DestroyIcon(icon);
    };
    inspect_and_destroy_tray_icon();
    const DWORD tray_gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD tray_user_before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int index = 0; index < 64; ++index) inspect_and_destroy_tray_icon();
    Require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= tray_gdi_before + 1 &&
            GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= tray_user_before + 1,
        "repeated tray icon refreshes release their GDI and USER resources");

    const auto version_054 = codex_partner::ParseSemanticVersion("0.54.0");
    const auto version_v123 = codex_partner::ParseSemanticVersion("v1.2.3");
    const auto version_max = codex_partner::ParseSemanticVersion("4294967295.0.7");
    Require(version_054 && version_054->major == 0 && version_054->minor == 54 && version_054->patch == 0,
        "strict semantic version parser accepts the native application version");
    Require(version_v123 && version_v123->major == 1 && version_v123->minor == 2 && version_v123->patch == 3,
        "strict semantic version parser accepts a lowercase release tag");
    Require(version_max && version_max->major == std::numeric_limits<std::uint32_t>::max(),
        "strict semantic version parser accepts the uint32 boundary");
    for (const std::string_view rejected_version : std::array<std::string_view, 13>{
             "", "v", "1", "1.2", "1.2.3.4", "1.2.3-beta", "1.2.3+build", "V1.2.3",
             "01.2.3", "1.02.3", "1.2.03", "4294967296.0.0", "1.two.3"}) {
        Require(!codex_partner::ParseSemanticVersion(rejected_version), "ambiguous or non-stable semantic versions are rejected");
    }
    const auto available_release = codex_partner::EvaluateLatestRelease(
        R"({"tag_name":"v0.55.0","html_url":"https://evil.example/download.exe"})", "0.54.0");
    Require(available_release.status == codex_partner::UpdateCheckStatus::Available &&
        available_release.latest_version == L"v0.55.0" &&
        available_release.release_url == L"https://github.com/70666/Codex-Partner/releases/tag/v0.55.0" &&
        available_release.native_download_url.empty(),
        "available releases use a locally constructed canonical URL and ignore remote navigation fields");
    const auto direct_native_release = codex_partner::EvaluateLatestRelease(
        R"({"tag_name":"v0.55.0","assets":[{"name":"Codex-Partner-0.55.0-native-windows-x64.exe","browser_download_url":"https://evil.example/payload.exe"},{"name":"Codex-Partner-0.55.0-native-windows-x64.exe.sha256"}]})",
        "0.54.0");
    Require(direct_native_release.status == codex_partner::UpdateCheckStatus::Available &&
        direct_native_release.native_download_url ==
            L"https://github.com/70666/Codex-Partner/releases/download/v0.55.0/Codex-Partner-0.55.0-native-windows-x64.exe",
        "an exact EXE and sidecar pair enables only the locally constructed canonical Native download");
    const auto direct_navigation = codex_partner::ResolveUpdateNavigation(direct_native_release);
    Require(direct_navigation.kind == codex_partner::UpdateNavigationKind::NativeDownload &&
        direct_navigation.url == direct_native_release.native_download_url,
        "validated direct-update state resolves to the exact Native download");
    Require(codex_partner::ResolveUpdateNavigation(available_release).kind ==
            codex_partner::UpdateNavigationKind::ReleasePage,
        "a release without the complete Native pair safely falls back to its canonical release page");

    const std::array<std::string_view, 6> incomplete_asset_responses{
        R"({"tag_name":"v0.55.0","assets":[{"name":"Codex-Partner-0.55.0-native-windows-x64.exe"}]})",
        R"({"tag_name":"v0.55.0","assets":[{"name":"Codex-Partner-0.55.0-native-windows-x64.exe.sha256"}]})",
        R"({"tag_name":"v0.55.0","assets":[{"name":"Codex-Partner-9.9.9-native-windows-x64.exe"},{"name":"Codex-Partner-9.9.9-native-windows-x64.exe.sha256"}]})",
        R"({"tag_name":"v0.55.0","assets":[{"name":"Codex-Partner-0.55.0-native-windows-x64.exe"},{"name":"Codex-Partner-0.55.0-native-windows-x64.exe"},{"name":"Codex-Partner-0.55.0-native-windows-x64.exe.sha256"}]})",
        R"({"tag_name":"v0.55.0","assets":[{"name":"Codex-Partner-0.55.0-native-windows-x64.exe"},{"name":"Codex-Partner-0.55.0-native-windows-x64.exe.sha256"},{"name":"Codex-Partner-0.55.0-native-windows-x64.exe.sha256"}]})",
        R"({"tag_name":"v0.55.0","assets":{"name":"Codex-Partner-0.55.0-native-windows-x64.exe"}})",
    };
    for (const std::string_view response : incomplete_asset_responses) {
        const auto result = codex_partner::EvaluateLatestRelease(response, "0.54.0");
        Require(result.status == codex_partner::UpdateCheckStatus::Available && result.native_download_url.empty() &&
                codex_partner::ResolveUpdateNavigation(result).kind == codex_partner::UpdateNavigationKind::ReleasePage,
            "missing, mismatched, duplicate, or malformed Native assets never produce a direct download");
    }
    const auto current_with_assets = codex_partner::EvaluateLatestRelease(
        R"({"tag_name":"v0.54.0","assets":[{"name":"Codex-Partner-0.54.0-native-windows-x64.exe"},{"name":"Codex-Partner-0.54.0-native-windows-x64.exe.sha256"}]})",
        "0.54.0");
    Require(current_with_assets.status == codex_partner::UpdateCheckStatus::UpToDate &&
            current_with_assets.native_download_url.empty(),
        "an already-installed release never offers its own binary as an update");
    std::string oversized_assets = R"({"tag_name":"v0.55.0","assets":[)";
    for (std::size_t index = 0; index < 513; ++index) {
        if (index > 0) oversized_assets.push_back(',');
        oversized_assets += R"({"name":"noise"})";
    }
    oversized_assets += "]}";
    Require(codex_partner::EvaluateLatestRelease(oversized_assets, "0.54.0").native_download_url.empty(),
        "oversized public asset lists do not trigger direct-download scanning");
    auto tampered_direct_release = direct_native_release;
    tampered_direct_release.native_download_url = L"https://evil.example/payload.exe";
    Require(codex_partner::ResolveUpdateNavigation(tampered_direct_release).kind ==
            codex_partner::UpdateNavigationKind::ReleasePage,
        "a tampered direct URL is ignored in favor of the revalidated canonical release page");
    tampered_direct_release.release_url = L"https://evil.example/release";
    Require(codex_partner::ResolveUpdateNavigation(tampered_direct_release).kind ==
            codex_partner::UpdateNavigationKind::None,
        "a tampered release URL fails closed before crossing the Windows shell boundary");
    Require(codex_partner::EvaluateLatestRelease(R"({"tag_name":"v0.54.0"})", "0.54.0").status ==
            codex_partner::UpdateCheckStatus::UpToDate &&
        codex_partner::EvaluateLatestRelease(R"({"tag_name":"v0.53.9"})", "0.54.0").status ==
            codex_partner::UpdateCheckStatus::UpToDate,
        "equal and older stable releases do not produce a false update prompt");
    for (const std::string_view bad_release : std::array<std::string_view, 5>{
             "{broken", R"({})", R"({"tag_name":"0.55.0"})", R"({"tag_name":"v0.55.0-beta"})",
             R"({"tag_name":"v0.55.0/../../download"})"}) {
        const auto result = codex_partner::EvaluateLatestRelease(bad_release, "0.54.0");
        Require(result.status == codex_partner::UpdateCheckStatus::Failed && result.release_url.empty(),
            "malformed or unsafe latest-release responses fail closed");
    }

    const std::filesystem::path missing_auth_home = std::filesystem::temp_directory_path() /
        ("codex_partner-native-no-auth-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(missing_auth_home);
    std::wstring previous_codex_home(32768, L'\0');
    const DWORD previous_codex_home_length = GetEnvironmentVariableW(
        L"CODEX_HOME", previous_codex_home.data(), static_cast<DWORD>(previous_codex_home.size()));
    SetEnvironmentVariableW(L"CODEX_HOME", missing_auth_home.c_str());
    const codex_partner::UsageSnapshot missing_auth_snapshot = codex_partner::CodexProvider{}.Fetch();
    if (previous_codex_home_length > 0 && previous_codex_home_length < previous_codex_home.size()) {
        SetEnvironmentVariableW(L"CODEX_HOME", previous_codex_home.substr(0, previous_codex_home_length).c_str());
    } else {
        SetEnvironmentVariableW(L"CODEX_HOME", nullptr);
    }
    std::filesystem::remove_all(missing_auth_home);
    Require(codex_partner::NeedsProviderSetup(missing_auth_snapshot) && !missing_auth_snapshot.error.empty() &&
        !missing_auth_snapshot.loading,
        "the real Codex provider classifies a missing auth file as an actionable sign-in state without network access");

    codex_partner::UsageSnapshot snapshot;
    snapshot.loading = false;
    snapshot.connection = codex_partner::ProviderConnectionState::CredentialsDetected;
    snapshot.session = codex_partner::RateWindow{L"Session", 46.0, 300, std::nullopt};
    snapshot.weekly = codex_partner::RateWindow{L"Weekly", 82.0, 10080, std::nullopt};
    Require(codex_partner::DeriveHealth(snapshot) == codex_partner::UsageHealth::Watch, "most constrained window drives briefing");
    snapshot.weekly->used_percent = 94.0;
    Require(codex_partner::DeriveHealth(snapshot) == codex_partner::UsageHealth::Critical, "critical threshold is respected");
    snapshot.weekly->used_percent = 100.0;
    Require(codex_partner::DeriveHealth(snapshot) == codex_partner::UsageHealth::Exhausted, "exhausted state is explicit");
    snapshot.weekly->used_percent = 79.0;
    Require(codex_partner::DeriveUsageAlertLevel(snapshot, 80.0) == codex_partner::UsageAlertLevel::None, "usage below the chosen threshold stays quiet");
    snapshot.weekly->used_percent = 80.0;
    Require(codex_partner::DeriveUsageAlertLevel(snapshot, 80.0) == codex_partner::UsageAlertLevel::Warning, "warning begins exactly at the chosen threshold");
    snapshot.weekly->used_percent = 95.0;
    Require(codex_partner::DeriveUsageAlertLevel(snapshot, 80.0) == codex_partner::UsageAlertLevel::Critical, "critical alert begins at 95 percent");
    snapshot.error = L"offline";
    Require(codex_partner::DeriveUsageAlertLevel(snapshot, 80.0) == codex_partner::UsageAlertLevel::None, "provider errors never generate usage alerts");
    snapshot.error.clear();
    snapshot.stale = true;
    Require(codex_partner::DeriveUsageAlertLevel(snapshot, 80.0) == codex_partner::UsageAlertLevel::None, "cached usage never generates threshold alerts");
    snapshot.stale = false;

    codex_partner::UsageSnapshot setup_snapshot;
    setup_snapshot.loading = false;
    setup_snapshot.connection = codex_partner::ProviderConnectionState::NeedsLogin;
    setup_snapshot.error = L"sign-in required";
    Require(codex_partner::NeedsProviderSetup(setup_snapshot),
        "structured connection state identifies a genuine first-run or expired-session setup need");
    Require(codex_partner::ResolveUsagePrimaryTarget(setup_snapshot) == codex_partner::UsagePrimaryTarget::ProviderSetup,
        "first-run primary action routes directly to provider setup instead of a generic page");
    codex_partner::UsageSnapshot transient_failure;
    transient_failure.loading = false;
    transient_failure.connection = codex_partner::ProviderConnectionState::CredentialsDetected;
    transient_failure.error = L"offline";
    Require(!codex_partner::NeedsProviderSetup(transient_failure),
        "a network failure with detected credentials never sends users through sign-in again");
    Require(codex_partner::ResolveUsagePrimaryTarget(transient_failure) == codex_partner::UsagePrimaryTarget::RefreshUsage,
        "transient refresh failures route the large primary action to recovery instead of an unrelated web page");
    codex_partner::UsageSnapshot cached_usage = transient_failure;
    cached_usage.error.clear();
    cached_usage.stale = true;
    cached_usage.session = codex_partner::RateWindow{L"Session", 42.0, 300, std::nullopt};
    Require(codex_partner::ResolveUsagePrimaryTarget(cached_usage) == codex_partner::UsagePrimaryTarget::RefreshUsage,
        "last-known cached usage keeps a one-click refresh recovery action");
    cached_usage.stale = false;
    Require(codex_partner::ResolveUsagePrimaryTarget(cached_usage) == codex_partner::UsagePrimaryTarget::UsageAnalytics,
        "healthy live usage routes to the detailed in-app analytics surface");

    const auto pace_now = std::chrono::sys_days{std::chrono::year{2026} / 8 / 24} + std::chrono::hours{12};
    codex_partner::UsageSnapshot pace_snapshot;
    pace_snapshot.loading = false;
    pace_snapshot.session = codex_partner::RateWindow{L"Session", 30.0, 300, pace_now + std::chrono::hours{4}};
    const auto pace_risk = codex_partner::MostUrgentPaceForecast(pace_snapshot, pace_now);
    Require(pace_risk && pace_risk->window_title == L"Session" &&
        std::abs(pace_risk->projected_used_percent - 150.0) < 0.001 &&
        pace_risk->until_exhaustion == std::chrono::minutes{140},
        "pace forecast predicts exhaustion from elapsed-window evidence");
    Require(codex_partner::DeriveHealth(pace_snapshot, 70.0, 90.0, pace_now) == codex_partner::UsageHealth::Watch,
        "an otherwise healthy percentage is elevated to watch when its pace cannot last until reset");
    Require(codex_partner::DeriveUsageAlertLevel(pace_snapshot, 80.0) == codex_partner::UsageAlertLevel::None,
        "pace risk remains a quiet visual forecast below the configured notification threshold");
    pace_snapshot.weekly = codex_partner::RateWindow{L"Weekly", 20.0, 10080, pace_now + std::chrono::hours{24 * 6}};
    const auto most_urgent_pace = codex_partner::MostUrgentPaceForecast(pace_snapshot, pace_now);
    Require(most_urgent_pace && most_urgent_pace->window_title == L"Session",
        "the earliest projected exhaustion wins when multiple windows are running hot");
    pace_snapshot.weekly.reset();
    pace_snapshot.session->used_percent = 20.0;
    Require(!codex_partner::MostUrgentPaceForecast(pace_snapshot, pace_now) &&
        codex_partner::DeriveHealth(pace_snapshot, 70.0, 90.0, pace_now) == codex_partner::UsageHealth::Healthy,
        "a pace that reaches the limit exactly at reset is not presented as early exhaustion");
    pace_snapshot.session = codex_partner::RateWindow{L"Session", 20.0, 300, pace_now + std::chrono::minutes{295}};
    Require(!codex_partner::MostUrgentPaceForecast(pace_snapshot, pace_now),
        "the first few minutes of a window do not produce a volatile pace forecast");
    pace_snapshot.session = codex_partner::RateWindow{L"Session", 30.0, 300, pace_now + std::chrono::hours{4}};
    pace_snapshot.stale = true;
    Require(!codex_partner::MostUrgentPaceForecast(pace_snapshot, pace_now),
        "last-known cached usage never produces a live pace claim");
    pace_snapshot.stale = false;
    pace_snapshot.error = L"offline";
    Require(!codex_partner::MostUrgentPaceForecast(pace_snapshot, pace_now),
        "provider errors suppress pace forecasts even when retained windows exist");
    codex_partner::RateWindow invalid_pace{L"Session", std::numeric_limits<double>::quiet_NaN(), 300,
        pace_now + std::chrono::hours{4}};
    Require(!codex_partner::ForecastPaceRisk(invalid_pace, pace_now),
        "non-finite provider percentages never escape into a forecast");

    codex_partner::UsageSnapshot previous;
    previous.loading = false;
    previous.connection = codex_partner::ProviderConnectionState::CredentialsDetected;
    previous.plan = L"ChatGPT Pro";
    previous.session = codex_partner::RateWindow{L"Session", 42.0, 300, std::nullopt};
    previous.weekly = codex_partner::RateWindow{L"Weekly", 64.0, 10080, std::nullopt};
    previous.updated_at = std::chrono::system_clock::time_point{std::chrono::seconds{1234}};
    codex_partner::SpendSummary previous_spend;
    previous_spend.thirty_day_usd = 123.45;
    previous_spend.files_scanned = 7;
    previous.spend = previous_spend;
    codex_partner::UsageSnapshot failed_refresh;
    failed_refresh.loading = false;
    failed_refresh.connection = codex_partner::ProviderConnectionState::NeedsLogin;
    failed_refresh.error = L"offline";
    const auto retained = codex_partner::MergeUsageRefresh(previous, failed_refresh);
    Require(retained.stale && retained.session && retained.weekly, "failed refresh retains the last usable rate windows");
    Require(retained.plan == L"ChatGPT Pro" && retained.updated_at == previous.updated_at, "failed refresh keeps plan and last successful timestamp");
    Require(retained.spend && retained.spend->thirty_day_usd == 123.45, "usage refresh never erases local spend");
    Require(codex_partner::NeedsProviderSetup(retained),
        "an expired login remains actionable even while last-known quota values stay visible");
    codex_partner::UsageSnapshot successful_refresh;
    successful_refresh.loading = false;
    successful_refresh.connection = codex_partner::ProviderConnectionState::CredentialsDetected;
    successful_refresh.session = codex_partner::RateWindow{L"Session", 12.0, 300, std::nullopt};
    const auto refreshed = codex_partner::MergeUsageRefresh(previous, successful_refresh);
    Require(!refreshed.stale && refreshed.session->used_percent == 12.0, "successful refresh replaces live rate windows");
    Require(refreshed.plan == L"ChatGPT Pro", "temporarily omitted plan labels do not flicker away");
    const auto stale_spend = codex_partner::MergeSpendRefresh(previous.spend, std::nullopt);
    Require(stale_spend && stale_spend->stale && !stale_spend->partial && stale_spend->thirty_day_usd == 123.45, "failed spend scan retains prior data as stale without falsely calling it a lower bound");
    codex_partner::SpendSummary fresh_spend;
    fresh_spend.thirty_day_usd = 222.0;
    fresh_spend.files_scanned = 9;
    const auto merged_spend = codex_partner::MergeSpendRefresh(previous.spend, fresh_spend);
    Require(merged_spend && !merged_spend->stale && merged_spend->thirty_day_usd == 222.0, "successful spend scan replaces cached analytics");

    codex_partner::SpendSummary coverage_spend;
    Require(!codex_partner::SpendPricingCoveragePercent(coverage_spend),
        "pricing coverage is unavailable when no token events were detected");
    coverage_spend.priced_events = 55'741;
    coverage_spend.unpriced_events = 4'275;
    coverage_spend.priced_input_tokens = 8'027'876'807ULL;
    coverage_spend.priced_output_tokens = 25'292'185ULL;
    coverage_spend.unpriced_input_tokens = 583'557'218ULL;
    coverage_spend.unpriced_output_tokens = 347'435ULL;
    coverage_spend.unpriced_models = {"codex-auto-review"};
    Require(codex_partner::SpendPricingCoveragePercent(coverage_spend) == 93 &&
            codex_partner::SpendTokenCoveragePercent(coverage_spend) == 93 &&
            codex_partner::SummarizeUnpricedModels(coverage_spend) == "codex-auto-review",
        "event and token coverage explain the real lower bound without inventing an unknown model price");
    coverage_spend.priced_events = std::numeric_limits<std::size_t>::max();
    coverage_spend.unpriced_events = 1;
    Require(codex_partner::SpendPricingCoveragePercent(coverage_spend) == 99,
        "partial coverage never rounds up to a misleading complete 100 percent or overflows");
    coverage_spend.unpriced_events = 0;
    coverage_spend.unpriced_input_tokens = 0;
    coverage_spend.unpriced_output_tokens = 0;
    Require(codex_partner::SpendPricingCoveragePercent(coverage_spend) == 100,
        "fully priced detected events report complete coverage");
    Require(codex_partner::SpendTokenCoveragePercent(coverage_spend) == 100,
        "fully priced token volume reports complete coverage independently of event counts");
    coverage_spend.unpriced_models = {"unsafe/model\nname", std::string(80, 'x'), "third"};
    const std::string bounded_models = codex_partner::SummarizeUnpricedModels(coverage_spend, 32);
    Require(bounded_models.size() <= 36 && bounded_models.find('/') == std::string::npos &&
            bounded_models.find('\n') == std::string::npos,
        "missing-price model summaries are bounded and safe for one-line UI surfaces");

    codex_partner::SpendSummary pace_spend;
    Require(!codex_partner::DeriveSpendPaceInsight(pace_spend),
        "spend pace stays hidden until both nested rolling windows are available");
    pace_spend.one_day_usd = 12.0;
    pace_spend.seven_day_usd = 42.0;
    pace_spend.one_day_partial = true;
    const auto high_spend_pace = codex_partner::DeriveSpendPaceInsight(pace_spend);
    Require(high_spend_pace && high_spend_pace->level == codex_partner::SpendPaceLevel::High &&
            high_spend_pace->partial && high_spend_pace->multiple &&
            std::abs(*high_spend_pace->multiple - 2.4) < 0.000001 &&
            std::abs(high_spend_pace->prior_six_day_daily_average_usd - 5.0) < 0.000001,
        "spend pace compares the latest 24 hours with an independent preceding six-day baseline");
    Require(codex_partner::FormatSpendPaceInsight(pace_spend, false, true).find(L"Known-priced pace · 2.4× prior 6-day avg · $5.00/day") != std::wstring::npos &&
            codex_partner::FormatSpendPaceInsight(pace_spend, true).find(L"已知计价速度：近 24 小时是此前 6 天日均的 2.4×") != std::wstring::npos,
        "spend pace remains explicit about lower-bound inputs in both localized surfaces");
    pace_spend.one_day_usd = 2.9;
    pace_spend.seven_day_usd = 26.9;
    Require(codex_partner::DeriveSpendPaceInsight(pace_spend)->level == codex_partner::SpendPaceLevel::Quiet,
        "spend pace distinguishes a meaningfully quieter day");
    pace_spend.one_day_usd = 5.0;
    pace_spend.seven_day_usd = 29.0;
    Require(codex_partner::DeriveSpendPaceInsight(pace_spend)->level == codex_partner::SpendPaceLevel::Typical,
        "spend pace keeps modest variation in a quiet typical band");
    pace_spend.one_day_usd = 6.0;
    pace_spend.seven_day_usd = 30.0;
    Require(codex_partner::DeriveSpendPaceInsight(pace_spend)->level == codex_partner::SpendPaceLevel::Elevated,
        "spend pace distinguishes an elevated day before the high band");
    pace_spend.one_day_usd = 12.0;
    pace_spend.seven_day_usd = 6.0;
    Require(!codex_partner::DeriveSpendPaceInsight(pace_spend),
        "contradictory nested spend windows fail closed instead of manufacturing a trend");
    pace_spend.one_day_usd = 4.0;
    pace_spend.seven_day_usd = 4.0;
    const auto new_activity = codex_partner::DeriveSpendPaceInsight(pace_spend);
    Require(new_activity && new_activity->level == codex_partner::SpendPaceLevel::NewActivity && !new_activity->multiple,
        "new activity is described without an infinite or misleading pace multiple");

    codex_partner::UsageSnapshot share_snapshot = previous;
    share_snapshot.updated_at = pace_now - std::chrono::minutes{17};
    share_snapshot.session->resets_at = pace_now + std::chrono::minutes{90};
    share_snapshot.weekly->resets_at = pace_now + std::chrono::hours{24 * 5 + 3};
    share_snapshot.spend->one_day_usd = 12.34;
    share_snapshot.spend->seven_day_usd = 45.67;
    share_snapshot.spend->thirty_day_usd = 89.10;
    share_snapshot.spend->one_day_partial = true;
    share_snapshot.spend->partial = true;
    share_snapshot.spend->priced_events = 18;
    share_snapshot.spend->unpriced_events = 2;
    share_snapshot.spend->priced_input_tokens = 900;
    share_snapshot.spend->priced_output_tokens = 90;
    share_snapshot.spend->unpriced_input_tokens = 100;
    share_snapshot.spend->unpriced_output_tokens = 10;
    share_snapshot.spend->priced_cache_write_input_tokens = 125;
    share_snapshot.spend->unpriced_models = {"codex-auto-review"};
    const std::wstring english_share = codex_partner::BuildUsageShareSummary(share_snapshot, false, false, pace_now);
    Require(english_share.find(L"ChatGPT Pro") != std::wstring::npos &&
        english_share.find(L"Freshness: Updated 17 min ago") != std::wstring::npos &&
        english_share.find(L"58% remaining · resets in 1h 30m") != std::wstring::npos &&
        english_share.find(L"not a ChatGPT subscription invoice") != std::wstring::npos &&
        english_share.find(L"1 day: ≥ $12.34") != std::wstring::npos &&
        english_share.find(L"Known-priced pace: the last 24 hours are 2.2×") != std::wstring::npos &&
        english_share.find(L"90% of detected events priced") != std::wstring::npos &&
        english_share.find(L"token coverage: 90%") != std::wstring::npos &&
        english_share.find(L"Cache writes priced: 125 tokens") != std::wstring::npos &&
        english_share.find(L"codex-auto-review") != std::wstring::npos,
        "English share summary is concise, actionable, and explicit about API-equivalent lower bounds");
    const std::wstring chinese_share = codex_partner::BuildUsageShareSummary(share_snapshot, true, false, pace_now);
    Require(chinese_share.find(L"58% 剩余 · 1 小时 30 分钟后重置") != std::wstring::npos &&
        chinese_share.find(L"数据新鲜度：17 分钟前更新") != std::wstring::npos &&
        chinese_share.find(L"不是 ChatGPT 订阅账单") != std::wstring::npos &&
        chinese_share.find(L"近 30 天：$89.10") != std::wstring::npos &&
        chinese_share.find(L"已知计价速度：近 24 小时是此前 6 天日均的 2.2×") != std::wstring::npos &&
        chinese_share.find(L"remaining") == std::wstring::npos,
        "Chinese share summary localizes quota, reset, and spend language end to end");
    const std::wstring hidden_share = codex_partner::BuildUsageShareSummary(share_snapshot, false, true, pace_now);
    Require(hidden_share.find(L"ChatGPT Pro") == std::wstring::npos &&
        hidden_share.find(L"Identity: Hidden by privacy setting") != std::wstring::npos,
        "share summaries honor Hide identity instead of leaking the plan through a convenience action");
    const auto coverage_popup_accessibility = codex_partner::accessibility::BuildPopupElements(
        share_snapshot, false, false, false, codex_partner::ui::CopySummaryState::Idle,
        codex_partner::ui::PopupAction::None, codex_partner::ui::PopupAction::None);
    Require(coverage_popup_accessibility.back().value.find(L"event pricing coverage 90%") != std::wstring::npos &&
            coverage_popup_accessibility.back().value.find(L"token pricing coverage 90%") != std::wstring::npos &&
            coverage_popup_accessibility.back().value.find(L"priced cache-write tokens 125") != std::wstring::npos &&
            coverage_popup_accessibility.back().value.find(L"Known-priced pace: the last 24 hours are 2.2×") != std::wstring::npos &&
            coverage_popup_accessibility.back().value.find(L"known-priced lower bound") != std::wstring::npos &&
            coverage_popup_accessibility.back().value.find(L"codex-auto-review") != std::wstring::npos,
        "screen readers receive the same pricing coverage, lower-bound reason, and missing model as pixels");
    share_snapshot.error = L"Bearer secret-from-raw-provider-error";
    const std::wstring private_share = codex_partner::BuildUsageShareSummary(share_snapshot, false, false, pace_now);
    Require(private_share.find(L"secret-from-raw-provider-error") == std::wstring::npos &&
        private_share.find(L"Credentials and account identifiers are not included") != std::wstring::npos,
        "share summaries never copy raw provider errors or account secrets");
    share_snapshot.plan = L"ChatGPT Pro\r\nInjected heading";
    share_snapshot.session->used_percent = std::numeric_limits<double>::quiet_NaN();
    share_snapshot.credits = std::numeric_limits<double>::infinity();
    const std::wstring hostile_share = codex_partner::BuildUsageShareSummary(share_snapshot, false, false, pace_now);
    Require(hostile_share.find(L"Plan: ChatGPT Pro Injected heading\r\n") != std::wstring::npos &&
        hostile_share.find(L"\r\nInjected heading") == std::wstring::npos &&
        hostile_share.find(L"Current session: Unavailable") != std::wstring::npos &&
        hostile_share.find(L"Credits:") == std::wstring::npos,
        "share summaries flatten control characters and reject non-finite provider values");

    const auto freshness_now = std::chrono::system_clock::time_point{std::chrono::hours{500'000}};
    Require(codex_partner::FormatUsageFreshness({}, false, freshness_now) == L"No successful update yet" &&
        codex_partner::FormatUsageFreshness({}, true, freshness_now) == L"尚无成功更新",
        "missing timestamps never masquerade as ancient successful data");
    Require(codex_partner::FormatUsageFreshness(freshness_now - std::chrono::seconds{45}, false, freshness_now) == L"Updated just now" &&
        codex_partner::FormatUsageFreshness(freshness_now - std::chrono::minutes{23}, true, freshness_now) == L"23 分钟前更新",
        "freshness labels distinguish just-now and minute-scale data in both languages");
    Require(codex_partner::FormatUsageFreshness(freshness_now - std::chrono::hours{1}, false, freshness_now) == L"Updated 1 hr ago" &&
        codex_partner::FormatUsageFreshness(freshness_now - std::chrono::hours{48}, false, freshness_now) == L"Updated 2 days ago",
        "freshness labels use correct singular hours and rolling day ages");
    Require(codex_partner::FormatUsageFreshness(freshness_now + std::chrono::minutes{3}, false, freshness_now) == L"Updated just now" &&
        codex_partner::FormatUsageFreshness(freshness_now + std::chrono::minutes{6}, false, freshness_now) == L"Update time unavailable",
        "small clock skew is tolerated while implausible future timestamps fail closed");

    const auto open_wall_now = std::chrono::system_clock::time_point{std::chrono::hours{600'000}};
    const auto open_steady_now = std::chrono::steady_clock::time_point{std::chrono::hours{100}};
    codex_partner::UsageSnapshot open_snapshot;
    open_snapshot.updated_at = open_wall_now - std::chrono::minutes{4} - std::chrono::seconds{59};
    auto open_decision = codex_partner::EvaluateOpenRefresh(
        open_snapshot, false, std::nullopt, open_steady_now, open_wall_now);
    Require(!open_decision.should_refresh && open_decision.reason == codex_partner::OpenRefreshReason::Fresh,
        "revealing a snapshot younger than five minutes performs no background I/O");

    open_snapshot.updated_at = open_wall_now - std::chrono::minutes{5};
    open_decision = codex_partner::EvaluateOpenRefresh(
        open_snapshot, false, std::nullopt, open_steady_now, open_wall_now);
    Require(open_decision.should_refresh && open_decision.reason == codex_partner::OpenRefreshReason::AgedSnapshot,
        "revealing a snapshot at the five-minute boundary refreshes it");

    open_snapshot.error = L"temporary provider failure";
    open_decision = codex_partner::EvaluateOpenRefresh(
        open_snapshot, false, std::nullopt, open_steady_now, open_wall_now);
    Require(open_decision.should_refresh && open_decision.reason == codex_partner::OpenRefreshReason::DegradedSnapshot,
        "revealing an errored snapshot retries in the background");

    open_decision = codex_partner::EvaluateOpenRefresh(open_snapshot, true, std::nullopt, open_steady_now, open_wall_now);
    Require(!open_decision.should_refresh &&
            open_decision.reason == codex_partner::OpenRefreshReason::AlreadyRefreshing,
        "revealing during active refresh never queues duplicate work");

    open_decision = codex_partner::EvaluateOpenRefresh(open_snapshot, false,
        open_steady_now - std::chrono::seconds{29}, open_steady_now, open_wall_now);
    Require(!open_decision.should_refresh && open_decision.reason == codex_partner::OpenRefreshReason::RetryCooldown,
        "rapid reopen attempts stay inside the thirty-second retry cooldown");
    open_decision = codex_partner::EvaluateOpenRefresh(open_snapshot, false,
        open_steady_now - std::chrono::seconds{30}, open_steady_now, open_wall_now);
    Require(open_decision.should_refresh && open_decision.reason == codex_partner::OpenRefreshReason::DegradedSnapshot,
        "the retry cooldown permits a new degraded-data attempt at thirty seconds");
    open_decision = codex_partner::EvaluateOpenRefresh(open_snapshot, false,
        open_steady_now + std::chrono::seconds{1}, open_steady_now, open_wall_now);
    Require(!open_decision.should_refresh && open_decision.reason == codex_partner::OpenRefreshReason::RetryCooldown,
        "a backwards monotonic-clock observation fails closed instead of request-storming");

    open_snapshot.error.clear();
    open_snapshot.stale = true;
    open_decision = codex_partner::EvaluateOpenRefresh(
        open_snapshot, false, std::nullopt, open_steady_now, open_wall_now);
    Require(open_decision.should_refresh && open_decision.reason == codex_partner::OpenRefreshReason::DegradedSnapshot,
        "explicitly stale cached data refreshes when revealed");
    open_snapshot.stale = false;
    open_snapshot.updated_at = {};
    open_decision = codex_partner::EvaluateOpenRefresh(
        open_snapshot, false, std::nullopt, open_steady_now, open_wall_now);
    Require(open_decision.should_refresh && open_decision.reason == codex_partner::OpenRefreshReason::MissingTimestamp,
        "data without a successful-update timestamp refreshes when revealed");
    open_snapshot.updated_at = open_wall_now + std::chrono::minutes{6};
    open_decision = codex_partner::EvaluateOpenRefresh(
        open_snapshot, false, std::nullopt, open_steady_now, open_wall_now);
    Require(open_decision.should_refresh && open_decision.reason == codex_partner::OpenRefreshReason::InvalidTimestamp,
        "implausibly future-dated data refreshes rather than remaining fresh forever");
    open_snapshot.updated_at = open_wall_now + std::chrono::minutes{3};
    open_decision = codex_partner::EvaluateOpenRefresh(
        open_snapshot, false, std::nullopt, open_steady_now, open_wall_now);
    Require(!open_decision.should_refresh && open_decision.reason == codex_partner::OpenRefreshReason::Fresh,
        "small wall-clock skew remains a fresh immediate-open experience");

    using codex_partner::ui::PopupAction;
    using codex_partner::ui::SettingsAction;
    using codex_partner::ui::SettingsTab;
    codex_partner::UsageSnapshot popup_layout_snapshot;
    popup_layout_snapshot.connection = codex_partner::ProviderConnectionState::CredentialsDetected;
    popup_layout_snapshot.weekly = codex_partner::RateWindow{L"Weekly", 36.0, 10080, std::nullopt};
    const auto weekly_only_layout = codex_partner::ui::ResolvePopupLayout(popup_layout_snapshot);
    Require(weekly_only_layout.second_card_y < 0.0F && weekly_only_layout.spend_y == 256.0F &&
            weekly_only_layout.content_height == 400,
        "missing current-cycle data removes the fake zero-percent card and compacts the popup");
    popup_layout_snapshot.session = codex_partner::RateWindow{L"Session", 18.0, 300, std::nullopt};
    const auto two_window_layout = codex_partner::ui::ResolvePopupLayout(popup_layout_snapshot);
    Require(two_window_layout.second_card_y == 254.0F && two_window_layout.spend_y == 376.0F &&
            two_window_layout.content_height == 520,
        "two real quota windows retain separate readable cards without changing the information order");
    Require(codex_partner::ui::StepPopupAction(PopupAction::None, 1) == PopupAction::CopySummary, "popup keyboard focus starts at the leftmost copy action");
    Require(codex_partner::ui::StepPopupAction(PopupAction::CopySummary, 1) == PopupAction::Refresh, "popup keyboard focus follows visual header order");
    Require(codex_partner::ui::StepPopupAction(PopupAction::Primary, 1) == PopupAction::CopySummary, "popup keyboard focus wraps forward");
    Require(codex_partner::ui::StepPopupAction(PopupAction::CopySummary, -1) == PopupAction::Primary, "popup keyboard focus wraps backward");
    Require(codex_partner::ui::StepFloatBarAction(codex_partner::ui::FloatBarAction::None, 1) == codex_partner::ui::FloatBarAction::OpenPopup,
        "floating bar keyboard focus starts at the open action");
    Require(codex_partner::ui::StepFloatBarAction(codex_partner::ui::FloatBarAction::Hide, 1) == codex_partner::ui::FloatBarAction::OpenPopup,
        "floating bar keyboard focus wraps after hide");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::General, SettingsAction::SelectAbout, 1) == SettingsAction::CycleLanguage, "general keyboard focus reaches language after tabs");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::General, SettingsAction::CycleTheme, 1) == SettingsAction::ChooseGlobalShortcut &&
        codex_partner::ui::StepSettingsAction(SettingsTab::General, SettingsAction::ChooseGlobalShortcut, 1) == SettingsAction::CycleRefresh,
        "general keyboard focus reaches quick peek in the same order as the visible controls");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::General, SettingsAction::TogglePrivacy, 1) == SettingsAction::SelectGeneral, "general keyboard focus wraps");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::Notifications, SettingsAction::SelectAbout, 1) == SettingsAction::ToggleUsageNotifications, "notification controls are keyboard reachable");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::Notifications, SettingsAction::ChooseUsageWarning, 1) == SettingsAction::ChooseNotificationSnooze,
        "notification keyboard order reaches the temporary pause control before the test action");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::Notifications, SettingsAction::TestNotification, 1) == SettingsAction::SelectGeneral, "notification keyboard focus wraps after the test action");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::Providers, SettingsAction::SelectAbout, 1) == SettingsAction::LaunchCodexLogin &&
        codex_partner::ui::StepSettingsAction(SettingsTab::Providers, SettingsAction::LaunchCodexLogin, 1) == SettingsAction::OpenCodexFolder,
        "provider setup and recovery actions are reachable in intent order from the keyboard");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::UsageSpend, SettingsAction::SelectAbout, 1) == SettingsAction::SelectGeneral, "usage page keyboard navigation wraps across its tabs");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::About, SettingsAction::SelectAbout, 1) == SettingsAction::CheckForUpdates, "about page exposes update checking to keyboard users");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::About, SettingsAction::CheckForUpdates, 1) == SettingsAction::ReportIssue,
        "about keyboard order reaches the primary report action after update checking");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::About, SettingsAction::ReportIssue, 1) == SettingsAction::OpenProjectSite,
        "about keyboard order follows the visible report and project actions");
    Require(codex_partner::ui::StepSettingsAction(SettingsTab::About, SettingsAction::OpenProjectSite, 1) == SettingsAction::CopyDiagnostics, "about page exposes safe diagnostics to keyboard users");
    Require(!codex_partner::ui::ShouldDismissPopupOnDeactivate(false, false, false), "ordinary popup deactivation keeps the inspectable surface visible");
    Require(!codex_partner::ui::ShouldDismissPopupOnDeactivate(true, false, false), "opening settings keeps popup deactivation from racing the new surface");
    Require(!codex_partner::ui::ShouldDismissPopupOnDeactivate(false, true, false), "proof mode remains stable for automation");
    Require(!codex_partner::ui::ShouldDismissPopupOnDeactivate(false, false, true), "second-instance handoff remains visible until acknowledged by user input");

    using codex_partner::windowing::CalculatePopupPlacement;
    using codex_partner::windowing::PixelRect;
    const auto bottom_right = CalculatePopupPlacement(
        PixelRect{1880, 1040, 1904, 1064}, PixelRect{0, 0, 1920, 1040}, 400, 640);
    Require(bottom_right.x == 1504 && bottom_right.y == 384,
        "bottom-right tray popup preserves a 16-pixel DWM safety inset");
    const auto bottom_right_125 = CalculatePopupPlacement(
        PixelRect{1880, 1040, 1904, 1064}, PixelRect{0, 0, 1920, 1040}, 500, 800);
    Require(bottom_right_125.x == 1404 && bottom_right_125.y == 224,
        "target-monitor scaled dimensions are constrained before a mixed-DPI move");
    const auto top_tray = CalculatePopupPlacement(
        PixelRect{1800, 0, 1824, 40}, PixelRect{0, 40, 1920, 1080}, 400, 640);
    Require(top_tray.x == 1436 && top_tray.y == 56,
        "top taskbars place the popup below the tray icon instead of at the monitor bottom");
    const auto right_tray = CalculatePopupPlacement(
        PixelRect{1840, 900, 1880, 924}, PixelRect{0, 0, 1840, 1080}, 400, 640);
    Require(right_tray.x == 1424 && right_tray.y == 252,
        "right taskbars constrain the complete popup to the reduced work area");
    const auto negative_monitor = CalculatePopupPlacement(
        PixelRect{-40, 1000, -16, 1024}, PixelRect{-1920, 0, 0, 1040}, 600, 720);
    Require(negative_monitor.x == -616 && negative_monitor.y == 272,
        "negative-coordinate monitors retain complete popup bounds");
    const auto tiny_work_area = CalculatePopupPlacement(
        PixelRect{280, 200, 304, 224}, PixelRect{0, 0, 320, 240}, 400, 640);
    Require(tiny_work_area.x == 16 && tiny_work_area.y == 16,
        "oversized popups use a stable top-left fallback without invalid clamps");

    const std::filesystem::path settings_root = std::filesystem::temp_directory_path() / ("codex_partner-native-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(settings_root);
    codex_partner::SettingsStore settings_store(settings_root / "settings.ini");
    codex_partner::AppSettings saved_settings;
    saved_settings.theme = codex_partner::ThemeMode::Dark;
    saved_settings.language = codex_partner::LanguageMode::SimplifiedChinese;
    saved_settings.refresh_minutes = 30;
    saved_settings.usage_notifications = false;
    saved_settings.usage_warning_percent = 90;
    saved_settings.notification_snoozed_until =
        std::chrono::system_clock::now() + std::chrono::hours{4};
    saved_settings.start_minimized = false;
    saved_settings.hide_identity = false;
    saved_settings.global_shortcut = codex_partner::GlobalShortcut::CtrlAltU;
    saved_settings.show_float_bar = true;
    saved_settings.float_bar_x = -1234;
    saved_settings.float_bar_y = 246;
    codex_partner::AppSettings attempted_settings = saved_settings;
    attempted_settings.refresh_minutes = 5;
    attempted_settings.hide_identity = true;
    const auto successful_commit = codex_partner::ResolveSettingsCommit(saved_settings, attempted_settings, true);
    Require(successful_commit.feedback == codex_partner::SettingsPersistenceState::Saved &&
        successful_commit.effective == attempted_settings && successful_commit.persisted == attempted_settings,
        "a successful settings commit advances both visible and persisted state");
    const auto failed_commit = codex_partner::ResolveSettingsCommit(saved_settings, attempted_settings, false);
    Require(failed_commit.feedback == codex_partner::SettingsPersistenceState::Failed &&
        failed_commit.effective == saved_settings && failed_commit.persisted == saved_settings,
        "a failed settings commit rolls visible intent back to the last durable state");
    codex_partner::SettingsStore missing_parent_store(settings_root / "missing-parent" / "settings.ini");
    Require(!missing_parent_store.Save(attempted_settings) &&
        !std::filesystem::exists(missing_parent_store.path().wstring() + L".tmp"),
        "settings persistence fails closed and removes its temporary file when the destination is unavailable");

    const auto ctrl_shift_u = codex_partner::BindingForGlobalShortcut(codex_partner::GlobalShortcut::CtrlShiftU);
    const auto ctrl_alt_u = codex_partner::BindingForGlobalShortcut(codex_partner::GlobalShortcut::CtrlAltU);
    const auto ctrl_shift_space = codex_partner::BindingForGlobalShortcut(codex_partner::GlobalShortcut::CtrlShiftSpace);
    Require(!codex_partner::BindingForGlobalShortcut(codex_partner::GlobalShortcut::Disabled) && ctrl_shift_u &&
            ctrl_shift_u->virtual_key == 'U' &&
            ctrl_shift_u->modifiers == (MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT) && ctrl_alt_u &&
            ctrl_alt_u->virtual_key == 'U' &&
            ctrl_alt_u->modifiers == (MOD_CONTROL | MOD_ALT | MOD_NOREPEAT) && ctrl_shift_space &&
            ctrl_shift_space->virtual_key == VK_SPACE &&
            ctrl_shift_space->modifiers == (MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT),
        "global quick peek presets map to repeat-safe Windows hotkey bindings");
    Require(codex_partner::ParseGlobalShortcut(L"disabled") == codex_partner::GlobalShortcut::Disabled &&
            codex_partner::ParseGlobalShortcut(L"ctrl-shift-u") == codex_partner::GlobalShortcut::CtrlShiftU &&
            codex_partner::ParseGlobalShortcut(L"ctrl-alt-u") == codex_partner::GlobalShortcut::CtrlAltU &&
            codex_partner::ParseGlobalShortcut(L"ctrl-shift-space") == codex_partner::GlobalShortcut::CtrlShiftSpace &&
            !codex_partner::ParseGlobalShortcut(L"ctrl-u") &&
            std::wstring(codex_partner::GlobalShortcutLabel(codex_partner::GlobalShortcut::CtrlShiftSpace)) == L"Ctrl+Shift+Space",
        "global shortcut storage values round-trip and malformed combinations fail closed");
    const auto shortcut_registered = codex_partner::ResolveGlobalShortcutChange(
        codex_partner::GlobalShortcut::CtrlShiftU, codex_partner::GlobalShortcut::CtrlAltU, true, false);
    const auto shortcut_conflict_restored = codex_partner::ResolveGlobalShortcutChange(
        codex_partner::GlobalShortcut::CtrlShiftU, codex_partner::GlobalShortcut::CtrlAltU, false, true);
    const auto shortcut_unavailable = codex_partner::ResolveGlobalShortcutChange(
        codex_partner::GlobalShortcut::CtrlShiftU, codex_partner::GlobalShortcut::CtrlAltU, false, false);
    const auto shortcut_disabled = codex_partner::ResolveGlobalShortcutChange(
        codex_partner::GlobalShortcut::CtrlShiftU, codex_partner::GlobalShortcut::Disabled, true, false);
    Require(shortcut_registered.effective == codex_partner::GlobalShortcut::CtrlAltU &&
            shortcut_registered.status == codex_partner::GlobalShortcutStatus::Registered &&
            shortcut_registered.should_persist &&
            shortcut_conflict_restored.effective == codex_partner::GlobalShortcut::CtrlShiftU &&
            shortcut_conflict_restored.status == codex_partner::GlobalShortcutStatus::CandidateUnavailable &&
            !shortcut_conflict_restored.should_persist &&
            shortcut_unavailable.status == codex_partner::GlobalShortcutStatus::Unavailable &&
            shortcut_disabled.effective == codex_partner::GlobalShortcut::Disabled &&
            shortcut_disabled.status == codex_partner::GlobalShortcutStatus::Disabled &&
            shortcut_disabled.should_persist,
        "shortcut changes persist only registered choices and preserve the previous binding after conflicts");

    Require(!codex_partner::ShellLaunchSucceeded(-1) && !codex_partner::ShellLaunchSucceeded(0) &&
        !codex_partner::ShellLaunchSucceeded(32) && codex_partner::ShellLaunchSucceeded(33),
        "Windows ShellExecute result codes are interpreted at the documented success boundary");
    const codex_partner::ExternalActionFeedback folder_failure{
        codex_partner::ExternalAction::CodexFolder, codex_partner::ExternalActionOutcome::Failed};
    Require(std::wstring(codex_partner::ExternalActionStatus(folder_failure, true)).find(L"无法打开") != std::wstring::npos &&
        std::wstring(codex_partner::ExternalActionDescription(folder_failure, false)).find(L"Windows did not launch") != std::wstring::npos,
        "external action failures have concise localized status plus actionable screen-reader guidance");
    const codex_partner::ExternalActionFeedback native_update_opened{
        codex_partner::ExternalAction::NativeUpdateDownload, codex_partner::ExternalActionOutcome::Opened};
    const codex_partner::ExternalActionFeedback native_update_failed{
        codex_partner::ExternalAction::NativeUpdateDownload, codex_partner::ExternalActionOutcome::Failed};
    Require(std::wstring(codex_partner::ExternalActionStatus(native_update_opened, false)) ==
            L"Native update download opened" &&
        std::wstring(codex_partner::ExternalActionStatus(native_update_failed, true)).find(L"无法打开") != std::wstring::npos,
        "Native update navigation reports browser acceptance and failure without claiming installation");
    Require(std::wstring(codex_partner::IssueReportUrl()) ==
        L"https://github.com/70666/Codex-Partner/issues/new?template=bug_report.yml",
        "problem reports always target the canonical structured bug form");
    const codex_partner::ExternalActionFeedback issue_complete{
        codex_partner::ExternalAction::IssuePage, codex_partner::ExternalActionOutcome::Opened, true, true};
    const codex_partner::ExternalActionFeedback issue_copy_failed{
        codex_partner::ExternalAction::IssuePage, codex_partner::ExternalActionOutcome::Opened, true, false};
    const codex_partner::ExternalActionFeedback issue_open_failed{
        codex_partner::ExternalAction::IssuePage, codex_partner::ExternalActionOutcome::Failed, true, true};
    const codex_partner::ExternalActionFeedback issue_failed{
        codex_partner::ExternalAction::IssuePage, codex_partner::ExternalActionOutcome::Failed, true, false};
    Require(codex_partner::ExternalActionFullySucceeded(issue_complete) &&
        !codex_partner::ExternalActionPartiallySucceeded(issue_complete) &&
        std::wstring(codex_partner::ExternalActionStatus(issue_complete, false)).find(L"diagnostics copied") != std::wstring::npos &&
        std::wstring(codex_partner::ExternalActionDescription(issue_complete, false)).find(L"paste") != std::wstring::npos,
        "complete issue reporting confirms both the browser and private clipboard handoff");
    Require(!codex_partner::ExternalActionFullySucceeded(issue_copy_failed) &&
        codex_partner::ExternalActionPartiallySucceeded(issue_copy_failed) &&
        std::wstring(codex_partner::ExternalActionStatus(issue_copy_failed, true)).find(L"复制失败") != std::wstring::npos &&
        std::wstring(codex_partner::ExternalActionDescription(issue_copy_failed, false)).find(L"Copy diagnostics") != std::wstring::npos,
        "an opened issue with a busy clipboard remains an actionable partial success");
    Require(codex_partner::ExternalActionPartiallySucceeded(issue_open_failed) &&
        std::wstring(codex_partner::ExternalActionStatus(issue_open_failed, false)).find(L"Diagnostics copied") != std::wstring::npos &&
        std::wstring(codex_partner::ExternalActionDescription(issue_open_failed, true)).find(L"手动打开") != std::wstring::npos,
        "a copied report with a blocked browser preserves the useful clipboard result");
    Require(!codex_partner::ExternalActionPartiallySucceeded(issue_failed) &&
        std::wstring(codex_partner::ExternalActionStatus(issue_failed, true)).find(L"无法打开") != std::wstring::npos &&
        std::wstring(codex_partner::ExternalActionDescription(issue_failed, false)).find(L"try again") != std::wstring::npos,
        "a total report failure stays explicit instead of claiming that an issue was filed");

    const auto popup_accessibility = codex_partner::accessibility::BuildPopupElements(previous, false, false, false, codex_partner::ui::CopySummaryState::Idle, PopupAction::Settings, PopupAction::None);
    Require(popup_accessibility.size() == 8, "popup accessibility exposes all actions and every usage summary");
    Require(popup_accessibility[2].child_id == 2 && popup_accessibility[2].role == codex_partner::accessibility::Role::PushButton &&
        (popup_accessibility[2].state & codex_partner::accessibility::StateFocused) != 0, "popup settings action has a stable focused button identity");
    Require(popup_accessibility.back().value.find(L"30 days") != std::wstring::npos, "popup accessibility announces rolling spend values");
    const auto copied_popup_accessibility = codex_partner::accessibility::BuildPopupElements(previous, true, false, false, codex_partner::ui::CopySummaryState::Copied,
        PopupAction::CopySummary, PopupAction::None);
    Require(copied_popup_accessibility[0].child_id == 4 && copied_popup_accessibility[0].keyboard_shortcut == L"Ctrl+C" &&
        copied_popup_accessibility[0].name.find(L"已复制") != std::wstring::npos &&
        copied_popup_accessibility[0].value.find(L"可直接粘贴") != std::wstring::npos,
        "copy feedback and shortcut are exposed to screen readers");
    const auto failed_copy_accessibility = codex_partner::accessibility::BuildPopupElements(previous, false, false, false,
        codex_partner::ui::CopySummaryState::Failed, PopupAction::CopySummary, PopupAction::None);
    Require(failed_copy_accessibility[0].name.find(L"Couldn't copy") != std::wstring::npos &&
        failed_copy_accessibility[0].value.find(L"clipboard may be busy") != std::wstring::npos,
        "clipboard failures are explicit to screen readers instead of silently disappearing");
    const auto private_popup_accessibility = codex_partner::accessibility::BuildPopupElements(previous, false, true, false,
        codex_partner::ui::CopySummaryState::Idle, PopupAction::None, PopupAction::None);
    Require(private_popup_accessibility[4].value.find(L"ChatGPT Pro") == std::wstring::npos &&
        private_popup_accessibility[4].value.find(L"Identity hidden by privacy setting") != std::wstring::npos,
        "popup screen-reader output honors the same identity privacy state as pixels");
    const auto setup_popup_accessibility = codex_partner::accessibility::BuildPopupElements(setup_snapshot, true, false, false, codex_partner::ui::CopySummaryState::Idle,
        PopupAction::Primary, PopupAction::None);
    Require(setup_popup_accessibility[3].role == codex_partner::accessibility::Role::PushButton &&
        setup_popup_accessibility[3].name.find(L"连接 Codex") != std::wstring::npos &&
        setup_popup_accessibility[4].value.find(L"无需粘贴 API Key") != std::wstring::npos,
        "first-run screen-reader semantics expose the direct setup action and privacy promise");
    const auto retry_popup_accessibility = codex_partner::accessibility::BuildPopupElements(transient_failure, true,
        false, false, codex_partner::ui::CopySummaryState::Idle, {}, PopupAction::Primary, PopupAction::None);
    Require(retry_popup_accessibility[3].role == codex_partner::accessibility::Role::PushButton &&
        retry_popup_accessibility[3].name.find(L"重试 Codex 刷新") != std::wstring::npos &&
        retry_popup_accessibility[3].description.find(L"不离开此面板") != std::wstring::npos &&
        retry_popup_accessibility[3].default_action == L"重试",
        "refresh failures expose the same direct recovery action to keyboard and screen-reader users");
    const auto refreshing_popup_accessibility = codex_partner::accessibility::BuildPopupElements(transient_failure,
        false, false, true, codex_partner::ui::CopySummaryState::Idle, {}, PopupAction::Primary, PopupAction::None);
    Require(refreshing_popup_accessibility[3].name.find(L"Refreshing Codex usage") != std::wstring::npos &&
        (refreshing_popup_accessibility[3].state & codex_partner::accessibility::StateUnavailable) != 0,
        "an in-flight primary retry is announced as progress and cannot imply that another request was started");
    const auto queued_refresh_accessibility = codex_partner::accessibility::BuildPopupElements(transient_failure,
        false, false, true, codex_partner::ui::CopySummaryState::Idle, {}, PopupAction::Refresh,
        PopupAction::None, true);
    Require(queued_refresh_accessibility[1].name.find(L"refresh queued") != std::wstring::npos &&
            queued_refresh_accessibility[1].description.find(L"trailing refresh") != std::wstring::npos &&
            queued_refresh_accessibility[3].name.find(L"refresh queued") != std::wstring::npos &&
            queued_refresh_accessibility[4].value.find(L"queued after the current cycle") != std::wstring::npos,
        "queued refresh intent is explicit on the button, recovery action, and live status for screen readers");
    const auto spend_scan_accessibility = codex_partner::accessibility::BuildPopupElements(previous,
        false, false, codex_partner::RefreshPhase::ScanningSpend, codex_partner::ui::CopySummaryState::Idle,
        {}, PopupAction::Refresh, PopupAction::None);
    Require(spend_scan_accessibility[1].name.find(L"Queue one more refresh") != std::wstring::npos &&
            spend_scan_accessibility[4].value.find(L"limits ready; local spend scan continues") != std::wstring::npos &&
            spend_scan_accessibility.back().description.find(L"latest estimate remains visible") != std::wstring::npos,
        "the popup distinguishes ready live limits from a continuing spend scan without hiding the last estimate");

    codex_partner::UsageSnapshot accessible_pace_snapshot;
    accessible_pace_snapshot.loading = false;
    const auto accessible_pace_now = std::chrono::system_clock::now();
    accessible_pace_snapshot.session = codex_partner::RateWindow{L"Session", 30.0, 300, accessible_pace_now + std::chrono::hours{4}};
    const auto pace_popup_accessibility = codex_partner::accessibility::BuildPopupElements(accessible_pace_snapshot, true, false, false, codex_partner::ui::CopySummaryState::Idle,
        PopupAction::None, PopupAction::None);
    Require(pace_popup_accessibility[4].value.find(L"按当前速度") != std::wstring::npos &&
        pace_popup_accessibility[4].value.find(L"重置前用尽") != std::wstring::npos,
        "screen readers receive the same actionable pace warning as the visual briefing");

    const codex_partner::UpdateCheckState idle_update;
    const auto general_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous, idle_update, codex_partner::SettingsPersistenceState::Idle, SettingsTab::General, true, SettingsAction::CycleLanguage, SettingsAction::None);
    Require(general_accessibility.size() == 14, "general settings accessibility exposes six tabs, seven controls, and status feedback");
    Require(general_accessibility[0].child_id == 1 && (general_accessibility[0].state & codex_partner::accessibility::StateSelected) != 0, "active settings tab is announced as selected");
    Require(general_accessibility[6].child_id == 10 && general_accessibility[6].value == L"简体中文" &&
        general_accessibility[6].role == codex_partner::accessibility::Role::ComboBox, "language picker exposes its role and selected language");
    Require(general_accessibility[8].child_id == 16 && general_accessibility[8].value == L"Ctrl+Alt+U" &&
            general_accessibility[8].role == codex_partner::accessibility::Role::ComboBox &&
            general_accessibility[8].description.find(L"任意应用") != std::wstring::npos,
        "quick peek exposes its registered choice and system-wide behavior to screen readers");
    const auto shortcut_conflict_accessibility = codex_partner::accessibility::BuildSettingsElements(
        saved_settings, previous, idle_update, codex_partner::SettingsPersistenceState::Idle, {},
        codex_partner::RefreshPhase::Idle, SettingsTab::General, true,
        SettingsAction::ChooseGlobalShortcut, SettingsAction::None,
        codex_partner::GlobalShortcutStatus::CandidateUnavailable);
    Require(shortcut_conflict_accessibility[8].description.find(L"占用") != std::wstring::npos &&
            shortcut_conflict_accessibility[8].value == L"Ctrl+Alt+U" &&
            shortcut_conflict_accessibility.back().value.find(L"已保留") != std::wstring::npos,
        "shortcut conflicts announce that the attempted choice failed and the working binding was kept");

    const auto float_bar_accessibility = codex_partner::accessibility::BuildFloatBarElements(previous, true, false, false,
        codex_partner::ui::FloatBarAction::OpenPopup, codex_partner::ui::FloatBarAction::None);
    Require(float_bar_accessibility.size() == 2 && float_bar_accessibility[0].child_id == 1 &&
        float_bar_accessibility[0].role == codex_partner::accessibility::Role::PushButton &&
        (float_bar_accessibility[0].state & codex_partner::accessibility::StateFocused) != 0,
        "floating bar accessibility exposes a stable focused open action");
    Require(float_bar_accessibility[0].value.find(L"当前周期") != std::wstring::npos &&
        float_bar_accessibility[0].value.find(L"每周额度") != std::wstring::npos,
        "floating bar accessibility announces both rate-limit windows");
    const auto private_float_bar_accessibility = codex_partner::accessibility::BuildFloatBarElements(previous, false, true, false,
        codex_partner::ui::FloatBarAction::None, codex_partner::ui::FloatBarAction::None);
    Require(private_float_bar_accessibility[0].value.find(L"ChatGPT Pro") == std::wstring::npos &&
        private_float_bar_accessibility[0].value.find(L"Identity hidden by privacy setting") != std::wstring::npos,
        "floating-bar screen-reader output announces active privacy without disclosing the plan");
    const auto spend_scan_float_bar_accessibility = codex_partner::accessibility::BuildFloatBarElements(previous,
        false, false, codex_partner::RefreshPhase::ScanningSpend,
        codex_partner::ui::FloatBarAction::None, codex_partner::ui::FloatBarAction::None);
    Require(spend_scan_float_bar_accessibility[0].value.find(L"limits ready; local spend scan continues") != std::wstring::npos,
        "the floating bar announces that limits are usable while local spend scanning continues");

    const auto float_bar_settings_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous,
        idle_update, codex_partner::SettingsPersistenceState::Idle, SettingsTab::FloatBar, true, SettingsAction::ToggleFloatBar, SettingsAction::None);
    Require(float_bar_settings_accessibility.size() == 9 && float_bar_settings_accessibility[6].child_id == 35 &&
        float_bar_settings_accessibility[6].role == codex_partner::accessibility::Role::CheckButton &&
        (float_bar_settings_accessibility[6].state & codex_partner::accessibility::StateChecked) != 0,
        "floating bar setting is a checked, keyboard-focusable accessible control");
    const auto spend_scan_settings_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous,
        idle_update, codex_partner::SettingsPersistenceState::Idle, {}, codex_partner::RefreshPhase::ScanningSpend,
        SettingsTab::UsageSpend, false, SettingsAction::None, SettingsAction::None);
    Require(spend_scan_settings_accessibility[6].description.find(L"local logs are still being scanned") != std::wstring::npos &&
            spend_scan_settings_accessibility[6].description.find(L"current estimate remains visible") != std::wstring::npos,
        "Usage and spend exposes the staged refresh contract to screen readers");

    const auto about_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous, idle_update, codex_partner::SettingsPersistenceState::Idle, SettingsTab::About, true, SettingsAction::CopyDiagnostics, SettingsAction::None);
    Require(about_accessibility.size() == 12 && about_accessibility[7].child_id == 53 &&
        about_accessibility[7].command == static_cast<int>(SettingsAction::CheckForUpdates) &&
        about_accessibility[8].child_id == 54 &&
        about_accessibility[8].command == static_cast<int>(SettingsAction::ReportIssue) &&
        about_accessibility[10].child_id == 52 &&
        about_accessibility[10].command == static_cast<int>(SettingsAction::CopyDiagnostics) &&
        about_accessibility.back().child_id == 60, "about report, diagnostics, and save status have stable accessible identities");
    const auto issue_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous,
        idle_update, codex_partner::SettingsPersistenceState::Idle, issue_complete, SettingsTab::About, false,
        SettingsAction::ReportIssue, SettingsAction::None);
    Require((issue_accessibility[8].state & codex_partner::accessibility::StateFocused) != 0 &&
        issue_accessibility.back().value.find(L"diagnostics copied") != std::wstring::npos &&
        issue_accessibility.back().description.find(L"paste") != std::wstring::npos,
        "screen readers receive the focused one-click report action and its two-part success guidance");
    const codex_partner::ExternalActionFeedback project_opened{
        codex_partner::ExternalAction::ProjectSite, codex_partner::ExternalActionOutcome::Opened};
    const auto opened_project_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous,
        idle_update, codex_partner::SettingsPersistenceState::Idle, project_opened, SettingsTab::About, false,
        SettingsAction::OpenProjectSite, SettingsAction::None);
    Require(opened_project_accessibility.back().value == L"Project page opened" &&
        opened_project_accessibility.back().description.find(L"accepted") != std::wstring::npos,
        "successful external settings actions expose a short nonblocking confirmation to assistive technology");
    const codex_partner::ExternalActionFeedback login_failure{
        codex_partner::ExternalAction::CodexLogin, codex_partner::ExternalActionOutcome::Failed};
    const auto failed_login_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous,
        idle_update, codex_partner::SettingsPersistenceState::Idle, login_failure, SettingsTab::Providers, true,
        SettingsAction::LaunchCodexLogin, SettingsAction::None);
    Require(failed_login_accessibility.back().value.find(L"codex login") != std::wstring::npos &&
        failed_login_accessibility.back().description.find(L"Windows 未能启动") != std::wstring::npos,
        "login launch failures stay nonblocking and provide a manual recovery path to screen readers");
    const codex_partner::UpdateCheckState checking_update{codex_partner::UpdateCheckStatus::Checking, {}, {}, {}};
    const auto checking_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous,
        checking_update, codex_partner::SettingsPersistenceState::Idle, SettingsTab::About, false, SettingsAction::CheckForUpdates, SettingsAction::None);
    Require((checking_accessibility[7].state & codex_partner::accessibility::StateUnavailable) != 0 &&
        checking_accessibility[7].value.find(L"public GitHub") != std::wstring::npos,
        "screen readers announce an in-progress read-only update check as temporarily unavailable");
    const codex_partner::UpdateCheckState available_update{codex_partner::UpdateCheckStatus::Available, L"v0.55.0",
        L"https://github.com/70666/Codex-Partner/releases/tag/v0.55.0",
        L"https://github.com/70666/Codex-Partner/releases/download/v0.55.0/Codex-Partner-0.55.0-native-windows-x64.exe"};
    const auto available_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous,
        available_update, codex_partner::SettingsPersistenceState::Idle, SettingsTab::About, true, SettingsAction::CheckForUpdates, SettingsAction::None);
    Require(available_accessibility[7].name.find(L"v0.55.0") != std::wstring::npos &&
        available_accessibility[7].default_action == L"下载 Native EXE" &&
        available_accessibility[7].description.find(L"精确 Native 版本") != std::wstring::npos,
        "available one-click Native release and its exact safe download action are announced accessibly");
    const auto provider_setup_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings,
        setup_snapshot, idle_update, codex_partner::SettingsPersistenceState::Idle, SettingsTab::Providers, true, SettingsAction::LaunchCodexLogin, SettingsAction::None);
    Require(provider_setup_accessibility.size() == 10 && provider_setup_accessibility[7].child_id == 21 &&
        provider_setup_accessibility[7].role == codex_partner::accessibility::Role::PushButton &&
        (provider_setup_accessibility[7].state & codex_partner::accessibility::StateFocused) != 0 &&
        provider_setup_accessibility[8].child_id == 22,
        "provider settings expose separate focused login and configuration actions to automation");
    Require(codex_partner::accessibility::SettingsChildId(SettingsTab::Providers, SettingsAction::LaunchCodexLogin) == 21 &&
        codex_partner::accessibility::SettingsChildId(SettingsTab::General, SettingsAction::ChooseGlobalShortcut) == 16 &&
        codex_partner::accessibility::SettingsChildId(SettingsTab::Providers, SettingsAction::OpenCodexFolder) == 22 &&
        codex_partner::accessibility::SettingsChildId(SettingsTab::UsageSpend, SettingsAction::OpenCodexFolder) == 0 &&
        codex_partner::accessibility::SettingsChildId(SettingsTab::About, SettingsAction::ReportIssue) == 54 &&
        codex_partner::accessibility::SettingsChildId(SettingsTab::About, SettingsAction::CheckForUpdates) == 53,
        "page-local accessible commands retain distinct stable child IDs");

    const std::array settings_tabs{SettingsTab::General, SettingsTab::Providers, SettingsTab::Notifications, SettingsTab::FloatBar, SettingsTab::UsageSpend, SettingsTab::About};
    for (const SettingsTab page : settings_tabs) {
        const auto elements = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous, idle_update, codex_partner::SettingsPersistenceState::Idle, page, false, SettingsAction::None, SettingsAction::None);
        std::set<long> child_ids;
        for (const auto& element : elements) {
            Require(element.child_id > 0 && child_ids.insert(element.child_id).second, "accessible child IDs are positive and unique within every settings surface");
            Require(element.bounds.width > 0 && element.bounds.height > 0 && !element.name.empty(), "every accessible element has a usable name and hit-test bounds");
        }
    }
    const auto notification_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous, idle_update, codex_partner::SettingsPersistenceState::Idle, SettingsTab::Notifications, false, SettingsAction::None, SettingsAction::None);
    Require(notification_accessibility[6].child_id == 30 && notification_accessibility[6].role == codex_partner::accessibility::Role::CheckButton &&
        (notification_accessibility[6].state & codex_partner::accessibility::StateChecked) == 0, "disabled usage alerts are announced as an unchecked check button");
    Require(notification_accessibility[8].child_id == 33 &&
            (notification_accessibility[8].state & codex_partner::accessibility::StateUnavailable) != 0 &&
            notification_accessibility[8].value == L"Alerts are off",
        "notification pause is explicitly unavailable to screen readers while alerts are disabled");
    codex_partner::AppSettings snoozed_accessibility_settings = saved_settings;
    snoozed_accessibility_settings.usage_notifications = true;
    const auto snoozed_accessibility = codex_partner::accessibility::BuildSettingsElements(
        snoozed_accessibility_settings, previous, idle_update,
        codex_partner::SettingsPersistenceState::Idle, SettingsTab::Notifications, false,
        SettingsAction::None, SettingsAction::None);
    Require(snoozed_accessibility[8].child_id == 33 &&
            (snoozed_accessibility[8].state & codex_partner::accessibility::StateUnavailable) == 0 &&
            snoozed_accessibility[8].role == codex_partner::accessibility::Role::ComboBox &&
            snoozed_accessibility[8].value.find(L"Paused") != std::wstring::npos,
        "screen readers receive the active notification pause and its automatic-resume countdown");
    codex_partner::UsageSnapshot partial_accessibility_snapshot = previous;
    partial_accessibility_snapshot.spend->one_day_usd = 12.5;
    partial_accessibility_snapshot.spend->one_day_partial = true;
    const auto chinese_spend_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, partial_accessibility_snapshot, idle_update, codex_partner::SettingsPersistenceState::Idle, SettingsTab::UsageSpend, true, SettingsAction::None, SettingsAction::None);
    Require(chinese_spend_accessibility[6].value.find(L"≥ $") != std::wstring::npos && chinese_spend_accessibility[6].value.find(L"at least") == std::wstring::npos,
        "Chinese accessibility uses a language-neutral lower-bound symbol instead of an English phrase");
    Require(chinese_spend_accessibility.size() >= 9 &&
            chinese_spend_accessibility[7].child_id == 41 &&
            chinese_spend_accessibility[8].child_id == 42,
        "the model activity chart and top-project ranking are exposed as readable accessibility elements");
    const auto failed_save_accessibility = codex_partner::accessibility::BuildSettingsElements(saved_settings, previous,
        idle_update, codex_partner::SettingsPersistenceState::Failed, SettingsTab::General, true,
        SettingsAction::None, SettingsAction::None);
    Require(failed_save_accessibility.back().child_id == 60 &&
        failed_save_accessibility.back().value.find(L"已恢复") != std::wstring::npos &&
        failed_save_accessibility.back().description.find(L"账户权限") != std::wstring::npos &&
        failed_save_accessibility.back().description.find(L"数据访问权限") != std::wstring::npos,
        "screen readers receive the persistent save failure and recovery guidance");

    Require(settings_store.Save(saved_settings), "settings are committed atomically");
    const auto loaded_settings = settings_store.Load();
    Require(loaded_settings.theme == codex_partner::ThemeMode::Dark && loaded_settings.language == codex_partner::LanguageMode::SimplifiedChinese, "theme and language survive restart");
    Require(loaded_settings.refresh_minutes == 30 && !loaded_settings.usage_notifications && loaded_settings.usage_warning_percent == 90, "refresh and notification preferences survive restart");
    Require(codex_partner::IsNotificationSnoozed(loaded_settings.notification_snoozed_until) &&
            std::chrono::duration_cast<std::chrono::seconds>(
                saved_settings.notification_snoozed_until - loaded_settings.notification_snoozed_until).count() < 2,
        "an active notification pause survives restart with second-level precision");
    Require(!loaded_settings.start_minimized && !loaded_settings.hide_identity, "startup and privacy preferences survive restart");
    Require(loaded_settings.global_shortcut == codex_partner::GlobalShortcut::CtrlAltU,
        "the selected global quick peek shortcut survives restart");
    Require(loaded_settings.show_float_bar && loaded_settings.float_bar_x == -1234 && loaded_settings.float_bar_y == 246,
        "floating bar visibility and signed multi-monitor coordinates survive restart");
    Require(!std::filesystem::exists(settings_store.path().wstring() + L".tmp"), "atomic settings save leaves no temporary file behind");
    Require(WritePrivateProfileStringW(L"CodexPartner", L"NotificationSnoozedUntil",
                L"9223372036854775807", settings_store.path().c_str()) != FALSE &&
            !codex_partner::IsNotificationSnoozed(settings_store.Load().notification_snoozed_until),
        "unreasonably distant persisted pauses fail closed instead of muting alerts indefinitely");
    Require(WritePrivateProfileStringW(L"CodexPartner", L"GlobalShortcut", L"ctrl-u",
                settings_store.path().c_str()) != FALSE &&
            settings_store.Load().global_shortcut == codex_partner::GlobalShortcut::CtrlShiftU,
        "malformed persisted shortcuts recover to the documented safe default");

    const auto default_float_position = codex_partner::ConstrainFloatBarPosition(codex_partner::kUnsetWindowPosition,
        codex_partner::kUnsetWindowPosition, 0, 0, 1920, 1040, 420, 76);
    Require(default_float_position.x == 1484 && default_float_position.y == 16,
        "unset floating bar position chooses a quiet top-right default inside the work area");
    const auto recovered_float_position = codex_partner::ConstrainFloatBarPosition(-2000, 4000, 0, 0, 1920, 1040, 420, 76);
    Require(recovered_float_position.x == 0 && recovered_float_position.y == 964,
        "floating bar positions from removed monitors are recovered fully on-screen");
    const auto negative_monitor_position = codex_partner::ConstrainFloatBarPosition(-1500, 100, -1920, 0, 0, 1080, 420, 76);
    Require(negative_monitor_position.x == -1500 && negative_monitor_position.y == 100,
        "valid negative coordinates remain stable on a left-side monitor");

    previous.spend->one_day_usd = 12.5;
    previous.spend->one_day_partial = true;
    previous.spend->unpriced_models = {"codex-auto-review"};
    codex_partner::AppSettings diagnostic_settings;
    diagnostic_settings.hide_identity = true;
    diagnostic_settings.notification_snoozed_until =
        std::chrono::system_clock::now() + std::chrono::hours{1};
    codex_partner::UsageSnapshot diagnostic_snapshot = previous;
    diagnostic_snapshot.error = L"request failed: Bearer secret-from-provider-error";
    const std::wstring diagnostic = codex_partner::BuildDiagnosticSummary(diagnostic_settings, diagnostic_snapshot);
    Require(diagnostic.find(L"Codex Partner 0.0.0") != std::wstring::npos &&
        diagnostic.find(L"Connection: credentials-detected") != std::wstring::npos &&
        diagnostic.find(L"Spend1Day: >= $12.50") != std::wstring::npos &&
        diagnostic.find(L"NotificationSnoozed: yes") != std::wstring::npos,
        "safe diagnostics include versioned connection and lower-bound spend context");
    Require(diagnostic.find(L"ChatGPT Pro") == std::wstring::npos && diagnostic.find(L"hidden by privacy setting") != std::wstring::npos, "safe diagnostics honor the identity privacy setting");
    Require(diagnostic.find(L"secret-from-provider-error") == std::wstring::npos &&
        diagnostic.find(L"ProviderError: present; raw details omitted") != std::wstring::npos,
        "safe diagnostics classify provider failures without copying potentially secret raw errors");
    Require(diagnostic.find(L"codex-auto-review") != std::wstring::npos, "safe diagnostics identify models with missing public prices");
    Require(diagnostic.find(L"FloatBarVisible: no") != std::wstring::npos && diagnostic.find(L"-1234") == std::wstring::npos,
        "safe diagnostics report floating-bar state without exposing desktop coordinates");
    Require(diagnostic.find(L"PaceRisk: none") != std::wstring::npos,
        "safe diagnostics state explicitly when no live pace risk is supported by the data");
    const std::wstring setup_diagnostic = codex_partner::BuildDiagnosticSummary(diagnostic_settings, setup_snapshot);
    Require(setup_diagnostic.find(L"Connection: needs-login") != std::wstring::npos &&
        setup_diagnostic.find(L"auth.json") == std::wstring::npos,
        "setup diagnostics identify the recovery state without exposing credential paths");
    codex_partner::UsageSnapshot risky_diagnostic_snapshot;
    risky_diagnostic_snapshot.loading = false;
    const auto diagnostic_now = std::chrono::system_clock::now();
    risky_diagnostic_snapshot.session = codex_partner::RateWindow{L"Session", 30.0, 300,
        diagnostic_now + std::chrono::hours{4}};
    const std::wstring risky_diagnostic = codex_partner::BuildDiagnosticSummary(
        diagnostic_settings, risky_diagnostic_snapshot);
    Require(risky_diagnostic.find(L"PaceRisk: Session / projected 150.0% by reset / exhaustion in") !=
            std::wstring::npos,
        "safe diagnostics explain the evidence-backed window and projection when pace risk is live");

    codex_partner::UsageCache usage_cache(settings_root / "usage-cache.ini");
    codex_partner::UsageSnapshot cached_snapshot;
    cached_snapshot.loading = false;
    cached_snapshot.plan = L"ChatGPT Pro\r\nInjected=1";
    cached_snapshot.updated_at = std::chrono::system_clock::now();
    cached_snapshot.session = codex_partner::RateWindow{L"Session", 37.5, 300, cached_snapshot.updated_at + std::chrono::hours{2}};
    cached_snapshot.weekly = codex_partner::RateWindow{L"Weekly", 61.25, 10080, cached_snapshot.updated_at + std::chrono::hours{72}};
    codex_partner::SpendSummary cached_spend;
    cached_spend.one_day_usd = 12.34;
    cached_spend.seven_day_usd = 56.78;
    cached_spend.thirty_day_usd = 90.12;
    cached_spend.files_scanned = 42;
    cached_spend.priced_events = 123;
    cached_spend.unpriced_events = 7;
    cached_spend.priced_input_tokens = 456'789;
    cached_spend.priced_cache_write_input_tokens = 12'345;
    cached_spend.unpriced_cache_write_input_tokens = 678;
    cached_spend.partial = true;
    cached_spend.seven_day_partial = true;
    cached_spend.thirty_day_partial = true;
    cached_snapshot.spend = cached_spend;
    Require(usage_cache.Save(cached_snapshot), "last successful usage is cached atomically without credentials");
    const auto restored_cache = usage_cache.Load();
    Require(restored_cache && restored_cache->stale && !restored_cache->loading, "startup cache is restored explicitly as last-known data");
    Require(restored_cache->connection == codex_partner::ProviderConnectionState::CredentialsDetected,
        "a successful cached snapshot restores non-secret credential-detection context while live refresh begins");
    Require(restored_cache->plan == L"ChatGPT ProInjected=1" && restored_cache->session && std::abs(restored_cache->session->used_percent - 37.5) < 0.001, "cached plan labels are sanitized and rate windows survive restart");
    Require(restored_cache->spend && restored_cache->spend->stale && restored_cache->spend->thirty_day_usd && std::abs(*restored_cache->spend->thirty_day_usd - 90.12) < 0.001, "cached spend remains visible and marked stale");
    Require(restored_cache->spend->priced_events == 123 && restored_cache->spend->priced_input_tokens == 456'789 &&
            restored_cache->spend->priced_cache_write_input_tokens == 12'345 &&
            restored_cache->spend->unpriced_cache_write_input_tokens == 678,
        "cached coverage and cache-write diagnostics survive restart");
    Require(!restored_cache->spend->one_day_partial && restored_cache->spend->seven_day_partial && restored_cache->spend->thirty_day_partial, "cached lower-bound markers remain specific to each time window");
    cached_snapshot.updated_at = std::chrono::system_clock::now() - std::chrono::hours{24 * 8};
    Require(usage_cache.Save(cached_snapshot) && !usage_cache.Load(), "usage caches older than seven days are discarded");
    Require(!std::filesystem::exists(usage_cache.path().wstring() + L".tmp"), "usage cache save leaves no temporary file behind");
    std::filesystem::remove_all(settings_root);

    const auto insecure_response = codex_partner::WinHttpClient{}.Get(L"http://example.invalid/usage", {});
    Require(insecure_response.error == L"Usage requests require HTTPS", "credential-bearing usage requests reject plaintext HTTP before network access");
    const auto injected_header_response = codex_partner::WinHttpClient{}.Get(L"https://localhost/usage", {{L"Authorization", L"Bearer safe\r\nInjected: value"}});
    Require(injected_header_response.error == L"Invalid request header", "request headers reject CRLF injection before network access");
    const auto oversized_header_response = codex_partner::WinHttpClient{}.Get(L"https://localhost/usage", {{L"Authorization", std::wstring(16 * 1024 + 1, L'x')}});
    Require(oversized_header_response.error == L"Invalid request header", "request headers reject unreasonable credential sizes before network access");
    std::stop_source cancelled_request;
    cancelled_request.request_stop();
    const auto cancelled_response = codex_partner::WinHttpClient{}.Get(L"https://localhost/usage", {}, cancelled_request.get_token());
    Require(cancelled_response.error == L"Usage request cancelled", "pre-cancelled network work exits before opening a connection");

    const auto sol_cost = codex_partner::EstimateCodexCostUsd("gpt-5.6-sol", 1'000, 400, 1'000);
    Require(sol_cost.has_value(), "known Codex model is priced");
    Require(std::abs(*sol_cost - 0.02256) < 0.000001, "current cached input and output rates are applied");
    const auto sol_cache_write_cost = codex_partner::EstimateCodexCostUsd(
        "gpt-5.6-sol", 1'000, 400, 200, 1'000,
        std::chrono::sys_days{std::chrono::year{2026} / 8 / 24});
    Require(sol_cache_write_cost && std::abs(*sol_cache_write_cost - 0.02276) < 0.000001,
        "reported GPT-5.6 cache writes use the official 1.25-times uncached input rate");
    const auto clamped_cache_subsets = codex_partner::EstimateCodexCostUsd(
        "gpt-5.6-sol", 1'000, 900, 900, 0,
        std::chrono::sys_days{std::chrono::year{2026} / 8 / 24});
    Require(clamped_cache_subsets && std::abs(*clamped_cache_subsets - 0.00086) < 0.000001,
        "cache-read and cache-write subsets can never exceed total input tokens");
    const auto old_sol_cost = codex_partner::EstimateCodexCostUsd("gpt-5.6-sol", 1'000, 0, 1'000, std::chrono::sys_days{std::chrono::year{2026} / 7 / 29});
    Require(old_sol_cost && std::abs(*old_sol_cost - 0.035) < 0.000001, "pre-reduction GPT-5.6 pricing is retained for historical usage");
    const auto long_55_cost = codex_partner::EstimateCodexCostUsd("gpt-5.5", 300'000, 0, 1'000);
    Require(long_55_cost && std::abs(*long_55_cost - 3.045) < 0.000001, "GPT-5.5 long-context multiplier is applied per call");
    const auto long_54_cost = codex_partner::EstimateCodexCostUsd("gpt-5.4", 300'000, 0, 1'000);
    Require(long_54_cost && std::abs(*long_54_cost - 1.5225) < 0.000001, "GPT-5.4 long-context multiplier is applied per call");
    Require(!codex_partner::EstimateCodexCostUsd("gpt-5.3-codex-spark", 1'000, 0, 500), "models without a public API price stay visibly unpriced instead of becoming zero cost");
    Require(!codex_partner::EstimateCodexCostUsd("deepseek/deepseek-chat", 1'000, 0, 500), "routed provider usage is not attributed to Codex");

    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path cost_root = std::filesystem::temp_directory_path() / ("codex_partner-native-cost-" + unique);
    const std::filesystem::path session_root = cost_root / "sessions" / "2026" / "08" / "24";
    std::filesystem::create_directories(session_root);
    {
        std::ofstream fixture(session_root / "session.jsonl", std::ios::binary);
        fixture << R"({"timestamp":"2026-08-24T07:59:00Z","type":"session_meta","payload":{"cwd":"D:\\Work\\Codex Partner"}})" << '\n';
        fixture << R"({"timestamp":"2026-08-24T08:00:00Z","type":"turn_context","payload":{"model":"gpt-5.6-sol"}})" << '\n';
        fixture << R"({"timestamp":"2026-08-24T08:01:00Z","type":"event_msg","payload":{"type":"token_count","info":{"total_token_usage":{"input_tokens":1000,"cached_input_tokens":400,"cache_write_input_tokens":200,"output_tokens":1000}}}})" << '\n';
        fixture << R"({"timestamp":"2026-08-24T08:02:00Z","type":"event_msg","payload":{"type":"token_count","info":{"model":"gpt-5.6-terra","last_token_usage":{"input_tokens":500,"cached_input_tokens":100,"cache_write_input_tokens":50,"output_tokens":200},"total_token_usage":{"input_tokens":999999999,"cached_input_tokens":800000000,"cache_write_input_tokens":100000000,"output_tokens":50000000}}}})" << '\n';
        fixture << R"({"timestamp":"2026-08-24T08:03:00Z","type":"turn_context","payload":{"model":"codex-auto-review"}})" << '\n';
        fixture << R"({"timestamp":"2026-08-22T08:04:00Z","type":"event_msg","payload":{"type":"token_count","info":{"last_token_usage":{"input_tokens":100,"cached_input_tokens":0,"cache_write_input_tokens":20,"output_tokens":10}}}})" << '\n';
        fixture << R"({"timestamp":"2026-08-23T13:00:00Z","type":"turn_context","payload":{"model":"gpt-5.6-luna"}})" << '\n';
        fixture << R"({"timestamp":"2026-08-23T13:01:00Z","type":"event_msg","payload":{"type":"token_count","info":{"last_token_usage":{"input_tokens":1000,"cached_input_tokens":0,"output_tokens":100}}}})" << '\n';
        fixture << R"({"timestamp":"2026-08-23T20:00:00.250+08:00","type":"event_msg","payload":{"type":"token_count","info":{"model":"gpt-5.6-luna","last_token_usage":{"input_tokens":1000,"cached_input_tokens":0,"output_tokens":100}}}})" << '\n';
        fixture << R"({"timestamp":"2026-08-24T09:00:00Z","type":"event_msg","payload":{"type":"token_count","info":{"model":"deepseek/deepseek-chat","last_token_usage":{"input_tokens":999,"cached_input_tokens":0,"output_tokens":99}}}})" << '\n';
        fixture << R"({"timestamp":"2026-08-24T09:01:00Z","type":"event_msg","payload":{"type":"token_count","info":{"model":"deepseek-v4-flash","last_token_usage":{"input_tokens":999,"cached_input_tokens":0,"output_tokens":99}}}})" << '\n';
    }
    const auto scan_now = std::chrono::sys_days{std::chrono::year{2026} / 8 / 24} + std::chrono::hours{12};
    codex_partner::SpendScanDiagnostics first_scan_diagnostics;
    const auto spend = codex_partner::ScanCodexSpend(cost_root, scan_now, {}, &first_scan_diagnostics);
    Require(first_scan_diagnostics.candidate_files == 1 && first_scan_diagnostics.parsed_files == 1 &&
            first_scan_diagnostics.reused_files == 0 && first_scan_diagnostics.resumed_files == 0,
        "the initial local-spend scan parses each eligible session file exactly once");
    Require(spend.files_scanned == 1, "local Codex session file is scanned");
    Require(spend.one_day_usd && std::abs(*spend.one_day_usd - 0.026325) < 0.000001, "one-day spend includes reported GPT-5.6 cache writes and prefers per-call usage");
    Require(spend.seven_day_usd && std::abs(*spend.seven_day_usd - 0.026645) < 0.000001, "seven-day spend includes cache writes and the exact 24-hour boundary");
    Require(spend.thirty_day_usd && std::abs(*spend.thirty_day_usd - 0.026645) < 0.000001, "thirty-day spend is aggregated with cache-write pricing");
    Require(spend.priced_events == 4, "priced event diagnostics are counted");
    Require(spend.unpriced_events == 1 && spend.partial, "unknown wrapper models are visible as partial coverage");
    Require(!spend.one_day_partial && spend.seven_day_partial && spend.thirty_day_partial, "unknown prices mark only the rolling windows that actually contain those events");
    Require(spend.priced_input_tokens == 3'500 && spend.priced_cached_input_tokens == 500 &&
            spend.priced_cache_write_input_tokens == 250 && spend.priced_output_tokens == 1'400,
        "priced token diagnostics include per-call cache-write usage");
    Require(spend.unpriced_input_tokens == 100 && spend.unpriced_cached_input_tokens == 0 &&
            spend.unpriced_cache_write_input_tokens == 20 && spend.unpriced_output_tokens == 10,
        "unpriced token volume preserves cache-write evidence instead of becoming synthetic zero cost");
    Require(spend.unpriced_models.size() == 1 && spend.unpriced_models.front() == "codex-auto-review", "unpriced model names remain diagnosable while routed providers stay siloed");
    Require(spend.daily_model_usage.size() == 3 &&
            spend.daily_model_usage.back().models.size() == 2,
        "the 30-day chart keeps daily model buckets instead of flattening all activity into one total");
    const auto chart_sol = std::find_if(spend.daily_model_usage.back().models.begin(),
        spend.daily_model_usage.back().models.end(), [](const codex_partner::ModelUsageAmount& amount) {
            return amount.model == "gpt-5.6-sol";
        });
    Require(chart_sol != spend.daily_model_usage.back().models.end() && chart_sol->usage_count == 1 &&
            chart_sol->cost_usd > 0.0 && !chart_sol->partial,
        "each chart tooltip bucket preserves model usage count, priced value, and coverage state");
    Require(spend.top_projects.size() == 1 && spend.top_projects.front().project == "Codex Partner" &&
            spend.top_projects.front().usage_count == 5 &&
            std::abs(spend.top_projects.front().share_percent - 100.0) < 0.001,
        "project ranking stores only the final local folder name and computes its priced share");

    codex_partner::SpendScanDiagnostics repeat_scan_diagnostics;
    const auto repeated_spend = codex_partner::ScanCodexSpend(cost_root, scan_now, {}, &repeat_scan_diagnostics);
    Require(repeat_scan_diagnostics.candidate_files == 1 && repeat_scan_diagnostics.parsed_files == 0 &&
            repeat_scan_diagnostics.reused_files == 1 && repeat_scan_diagnostics.resumed_files == 0,
        "an unchanged session file reuses its derived in-memory events instead of reopening and reparsing JSONL");
    Require(repeated_spend.one_day_usd == spend.one_day_usd && repeated_spend.seven_day_usd == spend.seven_day_usd &&
            repeated_spend.thirty_day_usd == spend.thirty_day_usd && repeated_spend.priced_events == spend.priced_events &&
            repeated_spend.unpriced_events == spend.unpriced_events,
        "cache reuse preserves exact cost and coverage results");

    codex_partner::SpendScanDiagnostics aged_scan_diagnostics;
    const auto aged_spend = codex_partner::ScanCodexSpend(cost_root, scan_now + std::chrono::hours{25}, {}, &aged_scan_diagnostics);
    Require(aged_scan_diagnostics.parsed_files == 0 && aged_scan_diagnostics.reused_files == 1,
        "rolling-window refreshes reuse unchanged parse results");
    Require(aged_spend.one_day_usd && std::abs(*aged_spend.one_day_usd) < 0.000001 &&
            aged_spend.seven_day_usd && *aged_spend.seven_day_usd > 0.0 && !aged_spend.one_day_partial,
        "cached events are re-aggregated against the current time so expired one-day usage is never retained");

    {
        std::ofstream fixture(session_root / "session.jsonl", std::ios::binary | std::ios::app);
        fixture << R"({"timestamp":"2026-08-25T12:30:00Z","type":"event_msg","payload":{"type":"token_count","info":{"model":"gpt-5.6-luna","last_token_usage":{"input_tokens":1000,"cached_input_tokens":0,"output_tokens":100}}}})" << '\n';
    }
    codex_partner::SpendScanDiagnostics changed_scan_diagnostics;
    const auto changed_spend = codex_partner::ScanCodexSpend(cost_root, scan_now + std::chrono::hours{25}, {}, &changed_scan_diagnostics);
    Require(changed_scan_diagnostics.parsed_files == 0 && changed_scan_diagnostics.reused_files == 0 &&
            changed_scan_diagnostics.resumed_files == 1 &&
            changed_spend.priced_events == spend.priced_events + 1 && changed_spend.one_day_usd && *changed_spend.one_day_usd > 0.0,
        "a line-aligned session append resumes from the cached byte boundary and immediately contributes its new priced event");

    {
        std::ofstream fixture(session_root / "session.jsonl", std::ios::binary | std::ios::app);
        fixture << R"({"timestamp":"2026-08-25T12:31:00Z","type":"event_msg","payload":{"type":"token_count","info":{"model":"gpt-5.6-sol","total_token_usage":{"input_tokens":1200,"cached_input_tokens":400,"cache_write_input_tokens":200,"output_tokens":1100}}}})" << '\n';
    }
    codex_partner::SpendScanDiagnostics cumulative_resume_diagnostics;
    const auto cumulative_spend = codex_partner::ScanCodexSpend(
        cost_root, scan_now + std::chrono::hours{25}, {}, &cumulative_resume_diagnostics);
    Require(cumulative_resume_diagnostics.resumed_files == 1 && cumulative_resume_diagnostics.parsed_files == 0 &&
            cumulative_spend.priced_events == changed_spend.priced_events + 1 &&
            cumulative_spend.priced_input_tokens == changed_spend.priced_input_tokens + 200 &&
            cumulative_spend.priced_output_tokens == changed_spend.priced_output_tokens + 100,
        "append resume carries the model and cumulative-token watermark forward without double counting old totals");

    {
        std::ofstream fixture(session_root / "session.jsonl", std::ios::binary | std::ios::trunc);
        fixture << R"({"timestamp":"2026-08-25T12:31:30Z","type":"event_msg","payload":{"type":"token_count","info":{"model":"gpt-5.6-luna","last_token_usage":{"input_tokens":700,"cached_input_tokens":0,"output_tokens":70}}},"padding":")"
                << std::string(6'000, 'x') << R"("})" << '\n';
    }
    codex_partner::SpendScanDiagnostics larger_rewrite_diagnostics;
    const auto larger_rewrite_spend = codex_partner::ScanCodexSpend(
        cost_root, scan_now + std::chrono::hours{25}, {}, &larger_rewrite_diagnostics);
    Require(larger_rewrite_diagnostics.parsed_files == 1 && larger_rewrite_diagnostics.resumed_files == 0 &&
            larger_rewrite_spend.priced_events == 1 && larger_rewrite_spend.priced_input_tokens == 700,
        "a larger in-place rewrite whose cached tail checkpoint changed cannot masquerade as an append");

    {
        std::ofstream fixture(session_root / "session.jsonl", std::ios::binary | std::ios::trunc);
        fixture << R"({"timestamp":"2026-08-25T12:32:00Z","type":"event_msg","payload":{"type":"token_count","info":{"model":"gpt-5.6-luna","total_token_usage":{"input_tokens":3000,"cached_input_tokens":0,"output_tokens":300}}}})" << '\n';
    }
    codex_partner::SpendScanDiagnostics rewritten_scan_diagnostics;
    const auto rewritten_spend = codex_partner::ScanCodexSpend(
        cost_root, scan_now + std::chrono::hours{25}, {}, &rewritten_scan_diagnostics);
    Require(rewritten_scan_diagnostics.parsed_files == 1 && rewritten_scan_diagnostics.resumed_files == 0 &&
            rewritten_spend.priced_events == 1 && rewritten_spend.priced_input_tokens == 3000,
        "a truncated or rewritten session falls back to a full parse instead of extending stale cached totals");

    {
        std::ofstream fixture(session_root / "session.jsonl", std::ios::binary | std::ios::trunc);
        fixture << R"({"timestamp":"2026-08-25T12:33:00Z","type":"event_msg","payload":{"type":"token_count","info":{"model":"gpt-5.6-luna","last_token_usage":{"input_tokens":400,"cached_input_tokens":0,"output_tokens":40}}}})";
    }
    codex_partner::SpendScanDiagnostics incomplete_line_scan_diagnostics;
    const auto incomplete_line_spend = codex_partner::ScanCodexSpend(
        cost_root, scan_now + std::chrono::hours{25}, {}, &incomplete_line_scan_diagnostics);
    Require(incomplete_line_scan_diagnostics.parsed_files == 1 && incomplete_line_spend.priced_events == 1,
        "a valid final JSONL record without a newline remains visible");
    {
        std::ofstream fixture(session_root / "session.jsonl", std::ios::binary | std::ios::app);
        fixture << '\n' << R"({"timestamp":"2026-08-25T12:34:00Z","type":"event_msg","payload":{"type":"token_count","info":{"model":"gpt-5.6-luna","last_token_usage":{"input_tokens":500,"cached_input_tokens":0,"output_tokens":50}}}})" << '\n';
    }
    codex_partner::SpendScanDiagnostics non_boundary_scan_diagnostics;
    const auto non_boundary_spend = codex_partner::ScanCodexSpend(
        cost_root, scan_now + std::chrono::hours{25}, {}, &non_boundary_scan_diagnostics);
    Require(non_boundary_scan_diagnostics.parsed_files == 1 && non_boundary_scan_diagnostics.resumed_files == 0 &&
            non_boundary_spend.priced_events == 2,
        "an append after a non-newline final record reparses safely instead of duplicating or skipping that record");

    std::stop_source cancelled_scan;
    cancelled_scan.request_stop();
    const auto stopped_spend = codex_partner::ScanCodexSpend(cost_root, scan_now, cancelled_scan.get_token());
    Require(stopped_spend.partial && stopped_spend.one_day_partial && stopped_spend.seven_day_partial && stopped_spend.thirty_day_partial && stopped_spend.files_scanned == 0, "pre-cancelled log scans mark every affected window incomplete before reading session files");
    std::filesystem::remove_all(cost_root);

    std::cout << "CodexPartner.Tests: all tests passed\n";
    return 0;
}
