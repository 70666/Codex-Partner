#include "codex_provider.h"

#include "codex_cost_scanner.h"
#include "json.h"
#include "win_http.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>

namespace codex_partner {
namespace {

constexpr std::uintmax_t kMaxAuthFileBytes = 1024 * 1024;

template <typename String>
class ScopedSecretWiper {
public:
    explicit ScopedSecretWiper(String& value) noexcept : value_(value) {}
    ~ScopedSecretWiper() {
        if (!value_.empty()) SecureZeroMemory(value_.data(), value_.size() * sizeof(typename String::value_type));
    }
    ScopedSecretWiper(const ScopedSecretWiper&) = delete;
    ScopedSecretWiper& operator=(const ScopedSecretWiper&) = delete;

private:
    String& value_;
};

class ScopedJsonWiper {
public:
    explicit ScopedJsonWiper(JsonValue& value) noexcept : value_(value) {}
    ~ScopedJsonWiper() { value_.secure_clear(); }
    ScopedJsonWiper(const ScopedJsonWiper&) = delete;
    ScopedJsonWiper& operator=(const ScopedJsonWiper&) = delete;

private:
    JsonValue& value_;
};

std::wstring Utf8ToWide(std::string_view input) {
    if (input.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), output.data(), size);
    return output;
}

std::optional<double> FlexibleNumber(const JsonValue* value) {
    if (!value) return std::nullopt;
    if (const auto number = value->as_number()) return number;
    if (const auto text = value->as_string()) {
        try { return std::stod(std::string(*text)); } catch (...) { return std::nullopt; }
    }
    return std::nullopt;
}

std::optional<RateWindow> ParseWindow(const JsonValue* value, std::wstring title) {
    if (!value) return std::nullopt;
    const auto percent = FlexibleNumber(value->find("used_percent"));
    const auto fallback_percent = FlexibleNumber(value->find("usage_percent"));
    if (!percent && !fallback_percent) return std::nullopt;
    RateWindow window;
    window.title = std::move(title);
    window.used_percent = std::clamp(percent.value_or(fallback_percent.value_or(0.0)), 0.0, 100.0);
    if (const auto seconds = FlexibleNumber(value->find("limit_window_seconds"))) window.window_minutes = static_cast<int>(*seconds / 60.0);
    if (const auto reset = FlexibleNumber(value->find("reset_at"))) window.resets_at = std::chrono::system_clock::time_point(std::chrono::seconds(static_cast<long long>(*reset)));
    return window;
}

std::wstring PlanLabel(std::string_view plan) {
    if (plan.empty()) return {};
    std::string value(plan);
    std::erase_if(value, [](unsigned char character) { return character < 0x20 || character == 0x7f; });
    if (value.empty()) return {};
    if (value.size() > 64) value.resize(64);
    value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
    return L"ChatGPT " + Utf8ToWide(value);
}

UsageSnapshot ErrorSnapshot(std::wstring error, ProviderConnectionState connection) {
    UsageSnapshot snapshot;
    snapshot.loading = false;
    snapshot.connection = connection;
    snapshot.updated_at = std::chrono::system_clock::now();
    snapshot.error = std::move(error);
    return snapshot;
}

}  // namespace

std::filesystem::path CodexProvider::CodexHome() {
    std::wstring value(32768, L'\0');
    DWORD length = GetEnvironmentVariableW(L"CODEX_HOME", value.data(), static_cast<DWORD>(value.size()));
    if (length > 0 && length < value.size()) return std::filesystem::path(value.data(), value.data() + length);
    length = GetEnvironmentVariableW(L"USERPROFILE", value.data(), static_cast<DWORD>(value.size()));
    if (length > 0 && length < value.size()) return std::filesystem::path(value.data(), value.data() + length) / L".codex";
    return {};
}

UsageSnapshot CodexProvider::Fetch(std::stop_token stop) const {
    const auto fail = [&](std::wstring message, ProviderConnectionState connection = ProviderConnectionState::Unknown) {
        return ErrorSnapshot(std::move(message), connection);
    };
    if (stop.stop_requested()) return fail(L"Usage request cancelled");
    const auto auth_path = CodexHome() / L"auth.json";
    std::error_code size_error;
    const auto auth_size = std::filesystem::file_size(auth_path, size_error);
    if (!size_error && auth_size > kMaxAuthFileBytes) return fail(
        L"Codex auth.json is unexpectedly large and was not opened.", ProviderConnectionState::NeedsLogin);
    std::ifstream stream(auth_path, std::ios::binary);
    if (!stream) return fail(L"Codex is not signed in. Run 'codex login' and refresh.",
        ProviderConnectionState::NeedsLogin);

    std::string token;
    std::string account_id;
    {
        std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        ScopedSecretWiper contents_wiper(contents);
        if (stop.stop_requested()) return fail(L"Usage request cancelled");
        auto auth = ParseJson(contents);
        ScopedJsonWiper auth_wiper(auth.value);
        if (!auth.ok()) return fail(L"Codex auth.json could not be read safely.",
            ProviderConnectionState::NeedsLogin);
        if (const auto* api_key = auth.value.find("OPENAI_API_KEY")) token = std::string(api_key->as_string().value_or(""));
        if (token.empty()) {
            const JsonValue* tokens = auth.value.find("tokens");
            if (tokens) {
                if (const auto* access = tokens->find("access_token")) token = std::string(access->as_string().value_or(""));
                if (const auto* account = tokens->find("account_id")) account_id = std::string(account->as_string().value_or(""));
            }
        }
    }
    ScopedSecretWiper token_wiper(token);
    ScopedSecretWiper account_id_wiper(account_id);
    if (token.empty()) return fail(L"Codex auth.json contains no usable access token.",
        ProviderConnectionState::NeedsLogin);

    std::vector<std::pair<std::wstring, std::wstring>> headers{
        {L"Authorization", L"Bearer " + Utf8ToWide(token)},
        {L"Accept", L"application/json"},
    };
    if (!account_id.empty()) headers.emplace_back(L"ChatGPT-Account-Id", Utf8ToWide(account_id));
    ScopedSecretWiper authorization_wiper(headers.front().second);
    std::optional<ScopedSecretWiper<std::wstring>> account_header_wiper;
    if (headers.size() > 2) account_header_wiper.emplace(headers.back().second);
    const HttpResponse response = WinHttpClient{}.Get(L"https://chatgpt.com/backend-api/wham/usage", headers, stop);
    if (!response.error.empty()) return fail(L"Could not reach Codex: " + response.error,
        ProviderConnectionState::CredentialsDetected);
    if (response.status == 401 || response.status == 403) return fail(
        L"Codex session expired. Run 'codex login' and refresh.", ProviderConnectionState::NeedsLogin);
    if (!response.ok()) return fail(L"Codex returned HTTP " + std::to_wstring(response.status) + L".",
        ProviderConnectionState::CredentialsDetected);

    const auto parsed = ParseJson(response.body);
    if (!parsed.ok()) return fail(L"Codex returned an unexpected response.",
        ProviderConnectionState::CredentialsDetected);
    UsageSnapshot snapshot;
    snapshot.loading = false;
    snapshot.connection = ProviderConnectionState::CredentialsDetected;
    snapshot.updated_at = std::chrono::system_clock::now();
    if (const auto* plan = parsed.value.find("plan_type")) snapshot.plan = PlanLabel(plan->as_string().value_or(""));
    if (const JsonValue* limits = parsed.value.find("rate_limit")) {
        auto primary = ParseWindow(limits->find("primary_window"), L"Session");
        auto secondary = ParseWindow(limits->find("secondary_window"), L"Weekly");
        if (primary && primary->window_minutes >= 6 * 24 * 60) {
            primary->title = L"Weekly";
            snapshot.weekly = std::move(primary);
        } else {
            snapshot.session = std::move(primary);
        }
        if (secondary) {
            if (secondary->window_minutes > 0 && secondary->window_minutes < 24 * 60 && !snapshot.session) {
                secondary->title = L"Session";
                snapshot.session = std::move(secondary);
            } else {
                secondary->title = L"Weekly";
                snapshot.weekly = std::move(secondary);
            }
        }
    }
    if (!snapshot.session && !snapshot.weekly) {
        if (const auto direct = FlexibleNumber(parsed.value.find("used_percent"))) snapshot.session = RateWindow{L"Session", std::clamp(*direct, 0.0, 100.0), 0, std::nullopt};
    }
    if (const JsonValue* credits = parsed.value.find("credits")) {
        const bool has_credits = credits->find("has_credits") && credits->find("has_credits")->as_bool().value_or(false);
        const bool unlimited = credits->find("unlimited") && credits->find("unlimited")->as_bool().value_or(false);
        if (has_credits && !unlimited) snapshot.credits = FlexibleNumber(credits->find("balance"));
    }
    if (!snapshot.session && !snapshot.weekly) snapshot.error = L"Codex returned no rate-limit windows.";
    return snapshot;
}

std::optional<SpendSummary> CodexProvider::FetchSpend(std::stop_token stop) const {
    if (stop.stop_requested()) return std::nullopt;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    const auto now = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock(spend_cache_mutex_);
        if (spend_cache_ && now - spend_cache_time_ < std::chrono::minutes{1}) return spend_cache_;
    }
    SpendSummary spend = ScanCodexSpend(CodexHome(), std::chrono::system_clock::now(), stop);
    if (stop.stop_requested()) return std::nullopt;
    {
        std::scoped_lock lock(spend_cache_mutex_);
        spend_cache_ = spend;
        spend_cache_time_ = std::chrono::steady_clock::now();
    }
    return spend;
}

}  // namespace codex_partner
