#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace codex_partner {

enum class UsageHealth { Loading, Healthy, Watch, Critical, Exhausted, Unavailable };
enum class UsageAlertLevel { None, Warning, Critical };
enum class ProviderConnectionState { Unknown, CredentialsDetected, NeedsLogin };
enum class UsagePrimaryTarget { UsageAnalytics, ProviderSetup, RefreshUsage };
enum class SpendPaceLevel { Quiet, Typical, Elevated, High, NewActivity };

struct ModelUsageAmount {
    std::string model;
    std::size_t usage_count = 0;
    double cost_usd = 0.0;
    bool partial = false;
};

struct DailyModelUsage {
    std::chrono::sys_days day{};
    std::vector<ModelUsageAmount> models;
};

struct ProjectUsageAmount {
    // Privacy boundary: this is only the final path component (or a friendly
    // fallback), never the complete local working-directory path.
    std::string project;
    std::size_t usage_count = 0;
    double cost_usd = 0.0;
    double share_percent = 0.0;
    bool partial = false;
};

struct RateWindow {
    std::wstring title;
    double used_percent = 0.0;
    int window_minutes = 0;
    std::optional<std::chrono::system_clock::time_point> resets_at;
};

struct SpendSummary {
    std::optional<double> one_day_usd;
    std::optional<double> seven_day_usd;
    std::optional<double> thirty_day_usd;
    std::size_t files_scanned = 0;
    std::size_t priced_events = 0;
    std::size_t unpriced_events = 0;
    std::uint64_t priced_input_tokens = 0;
    std::uint64_t priced_cached_input_tokens = 0;
    std::uint64_t priced_cache_write_input_tokens = 0;
    std::uint64_t priced_output_tokens = 0;
    std::uint64_t unpriced_input_tokens = 0;
    std::uint64_t unpriced_cached_input_tokens = 0;
    std::uint64_t unpriced_cache_write_input_tokens = 0;
    std::uint64_t unpriced_output_tokens = 0;
    std::vector<std::string> unpriced_models;
    std::vector<DailyModelUsage> daily_model_usage;
    std::vector<ProjectUsageAmount> top_projects;
    bool one_day_partial = false;
    bool seven_day_partial = false;
    bool thirty_day_partial = false;
    bool partial = false;
    bool stale = false;
};

struct SpendPaceInsight {
    double last_day_usd = 0.0;
    double prior_six_day_daily_average_usd = 0.0;
    std::optional<double> multiple;
    SpendPaceLevel level = SpendPaceLevel::Typical;
    bool partial = false;
};

struct UsageSnapshot {
    std::wstring provider = L"Codex";
    ProviderConnectionState connection = ProviderConnectionState::Unknown;
    std::wstring plan;
    std::optional<RateWindow> session;
    std::optional<RateWindow> weekly;
    std::optional<double> credits;
    std::optional<SpendSummary> spend;
    std::chrono::system_clock::time_point updated_at{};
    std::wstring error;
    bool loading = true;
    bool stale = false;
};

struct PaceForecast {
    std::wstring window_title;
    int window_minutes = 0;
    double projected_used_percent = 0.0;
    std::chrono::system_clock::time_point projected_exhaustion_at{};
    std::chrono::minutes until_exhaustion{};
};

[[nodiscard]] std::optional<PaceForecast> ForecastPaceRisk(
    const RateWindow& window,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) noexcept;
[[nodiscard]] std::optional<PaceForecast> MostUrgentPaceForecast(
    const UsageSnapshot& snapshot,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) noexcept;
[[nodiscard]] UsageHealth DeriveHealth(
    const UsageSnapshot& snapshot,
    double watch_threshold = 70.0,
    double critical_threshold = 90.0,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) noexcept;
[[nodiscard]] double MostConstrainedPercent(const UsageSnapshot& snapshot) noexcept;
[[nodiscard]] std::optional<int> SpendPricingCoveragePercent(const SpendSummary& spend) noexcept;
[[nodiscard]] std::optional<int> SpendTokenCoveragePercent(const SpendSummary& spend) noexcept;
[[nodiscard]] std::optional<SpendPaceInsight> DeriveSpendPaceInsight(const SpendSummary& spend) noexcept;
[[nodiscard]] std::string SummarizeUnpricedModels(
    const SpendSummary& spend,
    std::size_t maximum_characters = 48);
[[nodiscard]] bool NeedsProviderSetup(const UsageSnapshot& snapshot) noexcept;
[[nodiscard]] UsagePrimaryTarget ResolveUsagePrimaryTarget(const UsageSnapshot& snapshot) noexcept;
[[nodiscard]] UsageAlertLevel DeriveUsageAlertLevel(const UsageSnapshot& snapshot, double warning_threshold = 80.0, double critical_threshold = 95.0) noexcept;
[[nodiscard]] std::wstring HealthHeadline(UsageHealth health);
[[nodiscard]] std::wstring FormatReset(const RateWindow& window, std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
[[nodiscard]] UsageSnapshot MergeUsageRefresh(const UsageSnapshot& previous, UsageSnapshot incoming);
[[nodiscard]] std::optional<SpendSummary> MergeSpendRefresh(
    const std::optional<SpendSummary>& previous,
    std::optional<SpendSummary> incoming);

}  // namespace codex_partner
