#pragma once

#include "usage_provider.h"

#include <chrono>
#include <filesystem>
#include <mutex>

namespace codex_partner {

// Resolves the variants observed in the Codex usage response. Absolute reset
// values may be Unix seconds or milliseconds; relative values are seconds from
// the time the response was received.
[[nodiscard]] std::optional<std::chrono::system_clock::time_point> ResolveCodexResetTime(
    std::optional<double> reset_at,
    std::optional<double> reset_after_seconds,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) noexcept;

class CodexProvider final : public IUsageProvider {
public:
    [[nodiscard]] std::wstring_view id() const noexcept override { return L"codex"; }
    [[nodiscard]] UsageSnapshot Fetch(std::stop_token stop = {}) const override;
    [[nodiscard]] std::optional<SpendSummary> FetchSpend(std::stop_token stop = {}) const override;

private:
    [[nodiscard]] static std::filesystem::path CodexHome();

    mutable std::mutex spend_cache_mutex_;
    mutable std::optional<SpendSummary> spend_cache_;
    mutable std::chrono::steady_clock::time_point spend_cache_time_{};
};

}  // namespace codex_partner
