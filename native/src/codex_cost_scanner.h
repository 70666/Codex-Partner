#pragma once

#include "usage_model.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string_view>

namespace codex_partner {

struct SpendScanDiagnostics {
    std::size_t candidate_files = 0;
    std::size_t parsed_files = 0;
    std::size_t reused_files = 0;
    std::size_t resumed_files = 0;
};

[[nodiscard]] std::optional<double> EstimateCodexCostUsd(
    std::string_view model,
    std::uint64_t input_tokens,
    std::uint64_t cached_input_tokens,
    std::uint64_t output_tokens,
    std::chrono::sys_days usage_day = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())) noexcept;

[[nodiscard]] std::optional<double> EstimateCodexCostUsd(
    std::string_view model,
    std::uint64_t input_tokens,
    std::uint64_t cached_input_tokens,
    std::uint64_t cache_write_input_tokens,
    std::uint64_t output_tokens,
    std::chrono::sys_days usage_day) noexcept;

[[nodiscard]] SpendSummary ScanCodexSpend(
    const std::filesystem::path& codex_home,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now(),
    std::stop_token stop = {},
    SpendScanDiagnostics* diagnostics = nullptr);

}  // namespace codex_partner
