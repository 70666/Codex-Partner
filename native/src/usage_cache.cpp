#include "usage_cache.h"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>

namespace codex_partner {
namespace {

constexpr wchar_t kSection[] = L"UsageCache";
constexpr std::chrono::hours kMaximumCacheAge{24 * 7};

std::filesystem::path DefaultCachePath() {
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local))) {
        std::filesystem::path directory(local);
        CoTaskMemFree(local);
        directory /= L"CodexPartner";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (!error) return directory / L"usage-cache.ini";
    }
    std::error_code error;
    auto temporary = std::filesystem::temp_directory_path(error);
    if (!error) return temporary / L"CodexPartner-usage-cache.ini";
    return std::filesystem::current_path() / L"CodexPartner-usage-cache.ini";
}

std::wstring ReadText(const std::filesystem::path& path, const wchar_t* key) {
    std::array<wchar_t, 512> buffer{};
    GetPrivateProfileStringW(kSection, key, L"", buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

std::wstring SafeLabel(std::wstring value) {
    std::erase_if(value, [](wchar_t character) { return character < 0x20 || character == 0x7f; });
    if (value.size() > 128) value.resize(128);
    return value;
}

std::size_t ToSize(std::uint64_t value) {
    return static_cast<std::size_t>(std::min<std::uint64_t>(value, std::numeric_limits<std::size_t>::max()));
}

std::optional<double> ReadDouble(const std::filesystem::path& path, const wchar_t* key) {
    const std::wstring text = ReadText(path, key);
    if (text.empty()) return std::nullopt;
    try {
        std::size_t consumed = 0;
        const double value = std::stod(text, &consumed);
        if (consumed != text.size() || !std::isfinite(value) || value < 0.0) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint64_t> ReadUnsigned(const std::filesystem::path& path, const wchar_t* key) {
    const std::wstring text = ReadText(path, key);
    if (text.empty()) return std::nullopt;
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(text, &consumed);
        if (consumed != text.size()) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::int64_t> ReadSigned(const std::filesystem::path& path, const wchar_t* key) {
    const std::wstring text = ReadText(path, key);
    if (text.empty()) return std::nullopt;
    try {
        std::size_t consumed = 0;
        const auto value = std::stoll(text, &consumed);
        if (consumed != text.size()) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

bool WriteText(const std::filesystem::path& path, const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(kSection, key, value.c_str(), path.c_str()) != FALSE;
}

bool WriteOptional(const std::filesystem::path& path, const wchar_t* key, const std::optional<double>& value) {
    return WriteText(path, key, value ? std::to_wstring(*value) : L"");
}

std::int64_t EpochSeconds(std::chrono::system_clock::time_point value) {
    return std::chrono::duration_cast<std::chrono::seconds>(value.time_since_epoch()).count();
}

std::optional<RateWindow> ReadWindow(const std::filesystem::path& path, const wchar_t* prefix, std::wstring title) {
    const std::wstring present_key = std::wstring(prefix) + L"Present";
    if (ReadText(path, present_key.c_str()) != L"1") return std::nullopt;
    const std::wstring percent_key = std::wstring(prefix) + L"Percent";
    const auto percent = ReadDouble(path, percent_key.c_str());
    if (!percent) return std::nullopt;
    RateWindow window;
    window.title = std::move(title);
    window.used_percent = std::clamp(*percent, 0.0, 100.0);
    const std::wstring minutes_key = std::wstring(prefix) + L"Minutes";
    if (const auto minutes = ReadUnsigned(path, minutes_key.c_str())) {
        window.window_minutes = static_cast<int>(std::min<std::uint64_t>(*minutes, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
    }
    const std::wstring reset_key = std::wstring(prefix) + L"Reset";
    if (const auto reset = ReadSigned(path, reset_key.c_str()); reset && *reset > 0) {
        window.resets_at = std::chrono::system_clock::time_point{std::chrono::seconds{*reset}};
    }
    return window;
}

bool WriteWindow(const std::filesystem::path& path, const wchar_t* prefix, const std::optional<RateWindow>& window) {
    const std::wstring present_key = std::wstring(prefix) + L"Present";
    bool ok = WriteText(path, present_key.c_str(), window ? L"1" : L"0");
    if (!window) return ok;
    const std::wstring percent_key = std::wstring(prefix) + L"Percent";
    const std::wstring minutes_key = std::wstring(prefix) + L"Minutes";
    const std::wstring reset_key = std::wstring(prefix) + L"Reset";
    ok = WriteText(path, percent_key.c_str(), std::to_wstring(window->used_percent)) && ok;
    ok = WriteText(path, minutes_key.c_str(), std::to_wstring(window->window_minutes)) && ok;
    ok = WriteText(path, reset_key.c_str(), window->resets_at ? std::to_wstring(EpochSeconds(*window->resets_at)) : L"") && ok;
    return ok;
}

}  // namespace

UsageCache::UsageCache() : path_(DefaultCachePath()) {}

std::optional<UsageSnapshot> UsageCache::Load() const {
    if (ReadText(path_, L"Version") != L"1") return std::nullopt;
    const auto updated = ReadSigned(path_, L"UpdatedAt");
    if (!updated || *updated <= 0) return std::nullopt;
    const auto updated_at = std::chrono::system_clock::time_point{std::chrono::seconds{*updated}};
    const auto now = std::chrono::system_clock::now();
    if (updated_at > now + std::chrono::minutes{5} || now - updated_at > kMaximumCacheAge) return std::nullopt;

    UsageSnapshot snapshot;
    snapshot.loading = false;
    snapshot.stale = true;
    snapshot.connection = ProviderConnectionState::CredentialsDetected;
    snapshot.updated_at = updated_at;
    snapshot.plan = SafeLabel(ReadText(path_, L"Plan"));
    snapshot.session = ReadWindow(path_, L"Session", L"Session");
    snapshot.weekly = ReadWindow(path_, L"Weekly", L"Weekly");
    snapshot.credits = ReadDouble(path_, L"Credits");
    if (ReadText(path_, L"SpendPresent") == L"1") {
        SpendSummary spend;
        spend.one_day_usd = ReadDouble(path_, L"Spend1Day");
        spend.seven_day_usd = ReadDouble(path_, L"Spend7Day");
        spend.thirty_day_usd = ReadDouble(path_, L"Spend30Day");
        spend.files_scanned = ToSize(ReadUnsigned(path_, L"SpendFiles").value_or(0));
        spend.priced_events = ToSize(ReadUnsigned(path_, L"SpendPricedEvents").value_or(0));
        spend.unpriced_events = ToSize(ReadUnsigned(path_, L"SpendUnpricedEvents").value_or(0));
        spend.priced_input_tokens = ReadUnsigned(path_, L"SpendPricedInput").value_or(0);
        spend.priced_cached_input_tokens = ReadUnsigned(path_, L"SpendPricedCached").value_or(0);
        spend.priced_cache_write_input_tokens = ReadUnsigned(path_, L"SpendPricedCacheWrite").value_or(0);
        spend.priced_output_tokens = ReadUnsigned(path_, L"SpendPricedOutput").value_or(0);
        spend.unpriced_input_tokens = ReadUnsigned(path_, L"SpendUnpricedInput").value_or(0);
        spend.unpriced_cached_input_tokens = ReadUnsigned(path_, L"SpendUnpricedCached").value_or(0);
        spend.unpriced_cache_write_input_tokens = ReadUnsigned(path_, L"SpendUnpricedCacheWrite").value_or(0);
        spend.unpriced_output_tokens = ReadUnsigned(path_, L"SpendUnpricedOutput").value_or(0);
        spend.partial = ReadText(path_, L"SpendPartial") == L"1";
        const std::wstring one_day_partial = ReadText(path_, L"Spend1DayPartial");
        const std::wstring seven_day_partial = ReadText(path_, L"Spend7DayPartial");
        const std::wstring thirty_day_partial = ReadText(path_, L"Spend30DayPartial");
        const bool legacy_partial = spend.partial && one_day_partial.empty() && seven_day_partial.empty() && thirty_day_partial.empty();
        spend.one_day_partial = legacy_partial || one_day_partial == L"1";
        spend.seven_day_partial = legacy_partial || seven_day_partial == L"1";
        spend.thirty_day_partial = legacy_partial || thirty_day_partial == L"1";
        spend.stale = true;
        snapshot.spend = std::move(spend);
    }
    if (!snapshot.session && !snapshot.weekly && !snapshot.spend) return std::nullopt;
    return snapshot;
}

bool UsageCache::Save(const UsageSnapshot& snapshot) const {
    if (snapshot.updated_at.time_since_epoch() <= std::chrono::system_clock::duration::zero() ||
        (!snapshot.session && !snapshot.weekly && !snapshot.spend)) return false;
    std::error_code directory_error;
    std::filesystem::create_directories(path_.parent_path(), directory_error);
    if (directory_error) return false;
    std::filesystem::path temporary = path_;
    temporary += L".tmp";
    DeleteFileW(temporary.c_str());
    bool ok = WriteText(temporary, L"Version", L"1");
    ok = WriteText(temporary, L"UpdatedAt", std::to_wstring(EpochSeconds(snapshot.updated_at))) && ok;
    ok = WriteText(temporary, L"Plan", SafeLabel(snapshot.plan)) && ok;
    ok = WriteWindow(temporary, L"Session", snapshot.session) && ok;
    ok = WriteWindow(temporary, L"Weekly", snapshot.weekly) && ok;
    ok = WriteOptional(temporary, L"Credits", snapshot.credits) && ok;
    ok = WriteText(temporary, L"SpendPresent", snapshot.spend ? L"1" : L"0") && ok;
    if (snapshot.spend) {
        const SpendSummary& spend = *snapshot.spend;
        ok = WriteOptional(temporary, L"Spend1Day", spend.one_day_usd) && ok;
        ok = WriteOptional(temporary, L"Spend7Day", spend.seven_day_usd) && ok;
        ok = WriteOptional(temporary, L"Spend30Day", spend.thirty_day_usd) && ok;
        ok = WriteText(temporary, L"SpendFiles", std::to_wstring(spend.files_scanned)) && ok;
        ok = WriteText(temporary, L"SpendPricedEvents", std::to_wstring(spend.priced_events)) && ok;
        ok = WriteText(temporary, L"SpendUnpricedEvents", std::to_wstring(spend.unpriced_events)) && ok;
        ok = WriteText(temporary, L"SpendPricedInput", std::to_wstring(spend.priced_input_tokens)) && ok;
        ok = WriteText(temporary, L"SpendPricedCached", std::to_wstring(spend.priced_cached_input_tokens)) && ok;
        ok = WriteText(temporary, L"SpendPricedCacheWrite", std::to_wstring(spend.priced_cache_write_input_tokens)) && ok;
        ok = WriteText(temporary, L"SpendPricedOutput", std::to_wstring(spend.priced_output_tokens)) && ok;
        ok = WriteText(temporary, L"SpendUnpricedInput", std::to_wstring(spend.unpriced_input_tokens)) && ok;
        ok = WriteText(temporary, L"SpendUnpricedCached", std::to_wstring(spend.unpriced_cached_input_tokens)) && ok;
        ok = WriteText(temporary, L"SpendUnpricedCacheWrite", std::to_wstring(spend.unpriced_cache_write_input_tokens)) && ok;
        ok = WriteText(temporary, L"SpendUnpricedOutput", std::to_wstring(spend.unpriced_output_tokens)) && ok;
        ok = WriteText(temporary, L"SpendPartial", spend.partial ? L"1" : L"0") && ok;
        ok = WriteText(temporary, L"Spend1DayPartial", spend.one_day_partial ? L"1" : L"0") && ok;
        ok = WriteText(temporary, L"Spend7DayPartial", spend.seven_day_partial ? L"1" : L"0") && ok;
        ok = WriteText(temporary, L"Spend30DayPartial", spend.thirty_day_partial ? L"1" : L"0") && ok;
    }
    if (ok) WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporary.c_str());
    if (ok) ok = MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

}  // namespace codex_partner
