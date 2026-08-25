#pragma once

#include "usage_provider.h"

#include <chrono>
#include <filesystem>
#include <mutex>

namespace codex_partner {

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
