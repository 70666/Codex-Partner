#pragma once

#include "usage_model.h"

#include <filesystem>
#include <optional>

namespace codex_partner {

class UsageCache {
public:
    UsageCache();
    explicit UsageCache(std::filesystem::path path) : path_(std::move(path)) {}

    [[nodiscard]] std::optional<UsageSnapshot> Load() const;
    [[nodiscard]] bool Save(const UsageSnapshot& snapshot) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace codex_partner
