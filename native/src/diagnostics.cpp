#include "diagnostics.h"

#include "notification_snooze.h"
#include "version.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace codex_partner {
namespace {

std::wstring SafeLine(std::wstring value) {
    std::erase_if(value, [](wchar_t character) { return character < 0x20 || character == 0x7f; });
    if (value.size() > 256) value.resize(256);
    return value;
}

const wchar_t* ThemeName(ThemeMode mode) noexcept {
    switch (mode) {
    case ThemeMode::System: return L"system";
    case ThemeMode::Light: return L"light";
    case ThemeMode::Dark: return L"dark";
    }
    return L"unknown";
}

const wchar_t* LanguageName(LanguageMode mode) noexcept {
    switch (mode) {
    case LanguageMode::System: return L"system";
    case LanguageMode::SimplifiedChinese: return L"zh-CN";
    case LanguageMode::English: return L"en";
    }
    return L"unknown";
}

const wchar_t* ConnectionName(ProviderConnectionState state) noexcept {
    switch (state) {
    case ProviderConnectionState::Unknown: return L"unknown";
    case ProviderConnectionState::CredentialsDetected: return L"credentials-detected";
    case ProviderConnectionState::NeedsLogin: return L"needs-login";
    }
    return L"unknown";
}

void WriteWindow(std::wostringstream& output, const wchar_t* name, const std::optional<RateWindow>& window) {
    output << name << L": ";
    if (!window) {
        output << L"unavailable\r\n";
        return;
    }
    output << std::fixed << std::setprecision(1) << window->used_percent << L"%";
    if (window->window_minutes > 0) output << L" / " << window->window_minutes << L" min";
    output << L"\r\n";
}

void WriteCost(std::wostringstream& output, const wchar_t* name, const std::optional<double>& cost, bool partial) {
    output << name << L": ";
    if (!cost) output << L"unavailable";
    else output << (partial ? L">= $" : L"$") << std::fixed << std::setprecision(2) << *cost;
    output << L"\r\n";
}

}  // namespace

std::wstring BuildDiagnosticSummary(const AppSettings& settings, const UsageSnapshot& snapshot) {
    std::wostringstream output;
    output << L"Codex Partner " CODEX_PARTNER_VERSION_WIDE L"\r\n"
           << L"Implementation: Native C++/Win32 x64\r\n"
           << L"Theme: " << ThemeName(settings.theme) << L"\r\n"
           << L"Language: " << LanguageName(settings.language) << L"\r\n"
           << L"RefreshMinutes: " << settings.refresh_minutes << L"\r\n"
           << L"UsageNotifications: " << (settings.usage_notifications ? L"enabled" : L"disabled") << L"\r\n"
           << L"NotificationSnoozed: " <<
                (IsNotificationSnoozed(settings.notification_snoozed_until) ? L"yes" : L"no") << L"\r\n"
           << L"FloatBarVisible: " << (settings.show_float_bar ? L"yes" : L"no") << L"\r\n"
           << L"Provider: Codex\r\n"
           << L"Connection: " << ConnectionName(snapshot.connection) << L"\r\n"
           << L"Plan: " << (settings.hide_identity ? L"hidden by privacy setting" : SafeLine(snapshot.plan)) << L"\r\n"
           << L"Loading: " << (snapshot.loading ? L"yes" : L"no") << L"\r\n"
           << L"Stale: " << (snapshot.stale ? L"yes" : L"no") << L"\r\n"
           << L"ProviderError: " << (snapshot.error.empty() ? L"none" : L"present; raw details omitted") << L"\r\n";
    if (snapshot.updated_at.time_since_epoch() > std::chrono::system_clock::duration::zero()) {
        output << L"UpdatedAtEpochSeconds: "
               << std::chrono::duration_cast<std::chrono::seconds>(snapshot.updated_at.time_since_epoch()).count() << L"\r\n";
    }
    WriteWindow(output, L"Session", snapshot.session);
    WriteWindow(output, L"Weekly", snapshot.weekly);
    if (const auto pace = MostUrgentPaceForecast(snapshot)) {
        output << L"PaceRisk: " << SafeLine(pace->window_title) << L" / projected "
               << std::fixed << std::setprecision(1) << pace->projected_used_percent
               << L"% by reset / exhaustion in " << pace->until_exhaustion.count() << L" min\r\n";
    } else {
        output << L"PaceRisk: none\r\n";
    }

    if (!snapshot.spend) {
        output << L"Spend: unavailable\r\n";
        return output.str();
    }
    const SpendSummary& spend = *snapshot.spend;
    output << L"SpendStale: " << (spend.stale ? L"yes" : L"no") << L"\r\n";
    WriteCost(output, L"Spend1Day", spend.one_day_usd, spend.one_day_partial);
    WriteCost(output, L"Spend7Day", spend.seven_day_usd, spend.seven_day_partial);
    WriteCost(output, L"Spend30Day", spend.thirty_day_usd, spend.thirty_day_partial);
    output << L"FilesScanned: " << spend.files_scanned << L"\r\n"
           << L"PricedEvents: " << spend.priced_events << L"\r\n"
           << L"UnpricedEvents: " << spend.unpriced_events << L"\r\n"
           << L"EventPricingCoveragePercent: " << SpendPricingCoveragePercent(spend).value_or(-1) << L"\r\n"
           << L"TokenPricingCoveragePercent: " << SpendTokenCoveragePercent(spend).value_or(-1) << L"\r\n"
           << L"PricedTokens(inputTotal/cacheReadSubset/cacheWriteSubset/output): " << spend.priced_input_tokens << L"/"
           << spend.priced_cached_input_tokens << L"/" << spend.priced_cache_write_input_tokens << L"/"
           << spend.priced_output_tokens << L"\r\n"
           << L"UnpricedTokens(inputTotal/cacheReadSubset/cacheWriteSubset/output): " << spend.unpriced_input_tokens << L"/"
           << spend.unpriced_cached_input_tokens << L"/" << spend.unpriced_cache_write_input_tokens << L"/"
           << spend.unpriced_output_tokens << L"\r\n";
    if (!spend.unpriced_models.empty()) {
        output << L"UnpricedModels: ";
        for (std::size_t index = 0; index < spend.unpriced_models.size(); ++index) {
            if (index > 0) output << L", ";
            const std::string& model = spend.unpriced_models[index];
            output << SafeLine(std::wstring(model.begin(), model.end()));
        }
        output << L"\r\n";
    }
    return output.str();
}

}  // namespace codex_partner
