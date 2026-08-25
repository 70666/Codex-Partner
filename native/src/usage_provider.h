#pragma once

#include "usage_model.h"

#include <memory>
#include <stop_token>
#include <string_view>
#include <vector>

namespace codex_partner {

class IUsageProvider {
public:
    virtual ~IUsageProvider() = default;
    [[nodiscard]] virtual std::wstring_view id() const noexcept = 0;
    [[nodiscard]] virtual UsageSnapshot Fetch(std::stop_token stop = {}) const = 0;
    [[nodiscard]] virtual std::optional<SpendSummary> FetchSpend(std::stop_token = {}) const { return std::nullopt; }
};

class ProviderRegistry {
public:
    void Add(std::unique_ptr<IUsageProvider> provider) { providers_.push_back(std::move(provider)); }
    [[nodiscard]] const IUsageProvider* Find(std::wstring_view id) const noexcept {
        for (const auto& provider : providers_) if (provider->id() == id) return provider.get();
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<IUsageProvider>> providers_;
};

}  // namespace codex_partner
