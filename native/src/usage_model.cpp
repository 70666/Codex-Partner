#include "usage_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace codex_partner {

double MostConstrainedPercent(const UsageSnapshot& snapshot) noexcept {
    double result = 0.0;
    if (snapshot.session) result = std::max(result, snapshot.session->used_percent);
    if (snapshot.weekly) result = std::max(result, snapshot.weekly->used_percent);
    return std::clamp(result, 0.0, 100.0);
}

std::optional<int> SpendPricingCoveragePercent(const SpendSummary& spend) noexcept {
    if (spend.priced_events == 0 && spend.unpriced_events == 0) return std::nullopt;
    if (spend.unpriced_events == 0) return 100;
    if (spend.priced_events == 0) return 0;
    const long double priced = static_cast<long double>(spend.priced_events);
    const long double total = priced + static_cast<long double>(spend.unpriced_events);
    const int rounded = static_cast<int>(std::lround(100.0L * priced / total));
    return std::clamp(rounded, 0, 99);
}

std::optional<int> SpendTokenCoveragePercent(const SpendSummary& spend) noexcept {
    const long double priced = static_cast<long double>(spend.priced_input_tokens) +
        static_cast<long double>(spend.priced_output_tokens);
    const long double unpriced = static_cast<long double>(spend.unpriced_input_tokens) +
        static_cast<long double>(spend.unpriced_output_tokens);
    if (priced == 0.0L && unpriced == 0.0L) return std::nullopt;
    if (unpriced == 0.0L) return 100;
    if (priced == 0.0L) return 0;
    const int rounded = static_cast<int>(std::lround(100.0L * priced / (priced + unpriced)));
    return std::clamp(rounded, 0, 99);
}

std::optional<SpendPaceInsight> DeriveSpendPaceInsight(const SpendSummary& spend) noexcept {
    if (!spend.one_day_usd || !spend.seven_day_usd) return std::nullopt;
    const double last_day = *spend.one_day_usd;
    const double seven_days = *spend.seven_day_usd;
    if (!std::isfinite(last_day) || !std::isfinite(seven_days) || last_day < 0.0 || seven_days < 0.0) {
        return std::nullopt;
    }

    // The rolling windows are nested. Excluding the latest 24 hours before
    // dividing keeps the comparison baseline independent from the value being
    // assessed. Reject contradictory snapshots instead of manufacturing a
    // trend from a corrupt or mismatched cache.
    constexpr double kRoundingToleranceUsd = 0.000001;
    if (seven_days + kRoundingToleranceUsd < last_day) return std::nullopt;
    const double prior_six_days = std::max(0.0, seven_days - last_day);
    const double baseline = prior_six_days / 6.0;
    const bool partial = spend.one_day_partial || spend.seven_day_partial;

    constexpr double kMeaningfulUsd = 0.01;
    if (baseline < kMeaningfulUsd) {
        if (last_day < kMeaningfulUsd) return std::nullopt;
        return SpendPaceInsight{last_day, baseline, std::nullopt, SpendPaceLevel::NewActivity, partial};
    }

    const double multiple = last_day / baseline;
    SpendPaceLevel level = SpendPaceLevel::Typical;
    if (multiple < 0.75) level = SpendPaceLevel::Quiet;
    else if (multiple <= 1.25) level = SpendPaceLevel::Typical;
    else if (multiple < 2.0) level = SpendPaceLevel::Elevated;
    else level = SpendPaceLevel::High;
    return SpendPaceInsight{last_day, baseline, multiple, level, partial};
}

std::string SummarizeUnpricedModels(const SpendSummary& spend, std::size_t maximum_characters) {
    if (spend.unpriced_models.empty() || maximum_characters == 0) return {};
    std::string result;
    for (std::size_t index = 0; index < spend.unpriced_models.size(); ++index) {
        std::string safe;
        safe.reserve(spend.unpriced_models[index].size());
        for (const unsigned char character : spend.unpriced_models[index]) {
            safe.push_back(std::isalnum(character) || character == '-' || character == '_' ||
                    character == '.' ? static_cast<char>(character) : '?');
        }
        if (safe.empty()) safe = "unknown";
        const std::string separator = result.empty() ? "" : ", ";
        if (result.size() + separator.size() + safe.size() > maximum_characters) {
            const std::size_t remaining = spend.unpriced_models.size() - index;
            if (result.empty()) {
                const std::size_t prefix = maximum_characters > 3 ? maximum_characters - 3 : 0;
                result = safe.substr(0, prefix);
                if (maximum_characters >= 3) result += "...";
            } else {
                result += " +" + std::to_string(remaining);
            }
            break;
        }
        result += separator + safe;
    }
    return result;
}

bool NeedsProviderSetup(const UsageSnapshot& snapshot) noexcept {
    return snapshot.connection == ProviderConnectionState::NeedsLogin;
}

UsagePrimaryTarget ResolveUsagePrimaryTarget(const UsageSnapshot& snapshot) noexcept {
    if (NeedsProviderSetup(snapshot)) return UsagePrimaryTarget::ProviderSetup;
    if (snapshot.stale || !snapshot.error.empty()) return UsagePrimaryTarget::RefreshUsage;
    return UsagePrimaryTarget::UsageAnalytics;
}

std::optional<PaceForecast> ForecastPaceRisk(const RateWindow& window,
    std::chrono::system_clock::time_point now) noexcept {
    if (!window.resets_at || window.window_minutes < 30 || window.used_percent < 2.0 ||
        window.used_percent >= 100.0 || !std::isfinite(window.used_percent)) {
        return std::nullopt;
    }
    const auto total = std::chrono::minutes{window.window_minutes};
    const auto start = *window.resets_at - total;
    if (now <= start || now >= *window.resets_at) return std::nullopt;
    const auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - start);
    const auto minimum_evidence = std::max(std::chrono::minutes{15}, total / 20);
    if (elapsed < minimum_evidence) return std::nullopt;

    const double elapsed_minutes = static_cast<double>(elapsed.count());
    const double total_minutes = static_cast<double>(total.count());
    const double projected = window.used_percent * total_minutes / elapsed_minutes;
    if (!std::isfinite(projected) || projected < 100.0) return std::nullopt;

    const double minutes_from_start = elapsed_minutes * 100.0 / window.used_percent;
    const auto exhaustion = start + std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::duration<double, std::ratio<60>>(minutes_from_start));
    if (exhaustion <= now || exhaustion >= *window.resets_at) return std::nullopt;
    const auto remaining = std::chrono::duration_cast<std::chrono::minutes>(exhaustion - now);
    return PaceForecast{window.title, window.window_minutes, projected, exhaustion,
        std::max(std::chrono::minutes{1}, remaining)};
}

std::optional<PaceForecast> MostUrgentPaceForecast(const UsageSnapshot& snapshot,
    std::chrono::system_clock::time_point now) noexcept {
    if (snapshot.loading || snapshot.stale || !snapshot.error.empty()) return std::nullopt;
    std::optional<PaceForecast> result;
    const auto consider = [&](const std::optional<RateWindow>& window) {
        if (!window) return;
        const auto forecast = ForecastPaceRisk(*window, now);
        if (forecast && (!result || forecast->until_exhaustion < result->until_exhaustion)) result = forecast;
    };
    consider(snapshot.session);
    consider(snapshot.weekly);
    return result;
}

UsageHealth DeriveHealth(const UsageSnapshot& snapshot, double watch_threshold, double critical_threshold,
    std::chrono::system_clock::time_point now) noexcept {
    if (snapshot.loading) return UsageHealth::Loading;
    if (!snapshot.error.empty() || (!snapshot.session && !snapshot.weekly)) return UsageHealth::Unavailable;
    const double used = MostConstrainedPercent(snapshot);
    if (used >= 100.0) return UsageHealth::Exhausted;
    if (used >= critical_threshold) return UsageHealth::Critical;
    if (used >= watch_threshold) return UsageHealth::Watch;
    if (MostUrgentPaceForecast(snapshot, now)) return UsageHealth::Watch;
    return UsageHealth::Healthy;
}

UsageAlertLevel DeriveUsageAlertLevel(const UsageSnapshot& snapshot, double warning_threshold, double critical_threshold) noexcept {
    if (snapshot.loading || snapshot.stale || !snapshot.error.empty() || (!snapshot.session && !snapshot.weekly)) return UsageAlertLevel::None;
    const double used = MostConstrainedPercent(snapshot);
    if (used >= critical_threshold) return UsageAlertLevel::Critical;
    if (used >= warning_threshold) return UsageAlertLevel::Warning;
    return UsageAlertLevel::None;
}

std::wstring HealthHeadline(UsageHealth health) {
    switch (health) {
    case UsageHealth::Loading: return L"Checking your Codex capacity";
    case UsageHealth::Healthy: return L"Plenty of room to keep working";
    case UsageHealth::Watch: return L"Worth watching";
    case UsageHealth::Critical: return L"Approaching the limit";
    case UsageHealth::Exhausted: return L"Limit reached";
    case UsageHealth::Unavailable: return L"Usage is temporarily unavailable";
    }
    return L"Codex capacity";
}

std::wstring FormatReset(const RateWindow& window, std::chrono::system_clock::time_point now) {
    if (!window.resets_at) return L"Reset time unavailable";
    const auto remaining = *window.resets_at - now;
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(remaining).count();
    if (minutes <= 0) return L"Resetting now";
    if (minutes < 60) return L"Resets in " + std::to_wstring(minutes) + L" min";
    const auto hours = minutes / 60;
    if (hours < 48) return L"Resets in " + std::to_wstring(hours) + L" hr";
    const auto days = hours / 24;
    return L"Resets in " + std::to_wstring(days) + L" days";
}

UsageSnapshot MergeUsageRefresh(const UsageSnapshot& previous, UsageSnapshot incoming) {
    if (!incoming.spend) incoming.spend = previous.spend;
    if (!incoming.error.empty() && (previous.session || previous.weekly)) {
        incoming.plan = previous.plan;
        incoming.session = previous.session;
        incoming.weekly = previous.weekly;
        incoming.credits = previous.credits;
        incoming.updated_at = previous.updated_at;
        incoming.stale = true;
    } else {
        if (incoming.plan.empty() && incoming.error.empty()) incoming.plan = previous.plan;
        incoming.stale = false;
    }
    return incoming;
}

std::optional<SpendSummary> MergeSpendRefresh(
    const std::optional<SpendSummary>& previous,
    std::optional<SpendSummary> incoming) {
    const auto has_evidence = [](const SpendSummary& spend) {
        return spend.one_day_usd || spend.seven_day_usd || spend.thirty_day_usd ||
            spend.files_scanned > 0 || spend.priced_events > 0 || spend.unpriced_events > 0;
    };
    if (incoming && (has_evidence(*incoming) || !previous)) {
        incoming->stale = false;
        return incoming;
    }
    if (previous) {
        SpendSummary retained = *previous;
        retained.stale = true;
        return retained;
    }
    return incoming;
}

}  // namespace codex_partner
