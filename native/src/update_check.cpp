#include "update_check.h"

#include "json.h"
#include "version.h"
#include "win_http.h"

#include <array>
#include <limits>
#include <vector>

namespace codex_partner {
namespace {

constexpr wchar_t kLatestReleaseApi[] =
    L"https://api.github.com/repos/70666/Codex-Partner/releases/latest";
constexpr wchar_t kReleasePagePrefix[] =
    L"https://github.com/70666/Codex-Partner/releases/tag/";
constexpr wchar_t kReleaseDownloadPrefix[] =
    L"https://github.com/70666/Codex-Partner/releases/download/";

std::wstring AsciiToWide(std::string_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (const unsigned char character : text) result.push_back(static_cast<wchar_t>(character));
    return result;
}

UpdateCheckState Failed() {
    return {UpdateCheckStatus::Failed, {}, {}, {}};
}

std::optional<std::string> WideAscii(std::wstring_view text) {
    std::string result;
    result.reserve(text.size());
    for (const wchar_t character : text) {
        if (character < 0 || character > 0x7F) return std::nullopt;
        result.push_back(static_cast<char>(character));
    }
    return result;
}

std::string NativeAssetName(std::string_view version) {
    return "Codex-Partner-" + std::string(version) + "-native-windows-x64.exe";
}

std::wstring NativeDownloadUrl(std::string_view tag, std::string_view asset_name) {
    return std::wstring(kReleaseDownloadPrefix) + AsciiToWide(tag) + L"/" + AsciiToWide(asset_name);
}

}  // namespace

std::optional<SemanticVersion> ParseSemanticVersion(std::string_view text) noexcept {
    if (text.starts_with('v')) text.remove_prefix(1);
    if (text.empty()) return std::nullopt;

    std::array<std::uint32_t, 3> components{};
    std::size_t cursor = 0;
    for (std::size_t index = 0; index < components.size(); ++index) {
        const std::size_t separator = text.find('.', cursor);
        const bool final_component = index + 1 == components.size();
        if ((final_component && separator != std::string_view::npos) ||
            (!final_component && separator == std::string_view::npos)) {
            return std::nullopt;
        }
        const std::size_t end = final_component ? text.size() : separator;
        const std::string_view component = text.substr(cursor, end - cursor);
        if (component.empty() || (component.size() > 1 && component.front() == '0')) return std::nullopt;

        std::uint32_t value = 0;
        for (const char character : component) {
            if (character < '0' || character > '9') return std::nullopt;
            const std::uint32_t digit = static_cast<std::uint32_t>(character - '0');
            if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) return std::nullopt;
            value = value * 10U + digit;
        }
        components[index] = value;
        cursor = end + (final_component ? 0 : 1);
    }
    return SemanticVersion{components[0], components[1], components[2]};
}

UpdateCheckState EvaluateLatestRelease(std::string_view response_json, std::string_view current_version) {
    const auto current = ParseSemanticVersion(current_version);
    if (!current) return Failed();

    JsonParseResult parsed = ParseJson(response_json);
    if (!parsed.ok()) return Failed();
    const JsonValue* tag_value = parsed.value.find("tag_name");
    const auto tag = tag_value ? tag_value->as_string() : std::nullopt;
    if (!tag || tag->size() > 32 || !tag->starts_with('v')) return Failed();
    const auto latest = ParseSemanticVersion(*tag);
    if (!latest) return Failed();

    UpdateCheckState result;
    result.status = *latest > *current ? UpdateCheckStatus::Available : UpdateCheckStatus::UpToDate;
    result.latest_version = AsciiToWide(*tag);
    result.release_url = std::wstring(kReleasePagePrefix) + AsciiToWide(*tag);
    if (result.status == UpdateCheckStatus::Available) {
        const JsonValue* assets = parsed.value.find("assets");
        const std::string asset_name = NativeAssetName(tag->substr(1));
        const std::string sidecar_name = asset_name + ".sha256";
        std::size_t asset_matches = 0;
        std::size_t sidecar_matches = 0;
        if (assets && assets->type() == JsonValue::Type::Array && assets->size() <= 512) {
            for (std::size_t index = 0; index < assets->size(); ++index) {
                const JsonValue* asset = assets->at(index);
                const JsonValue* name_value = asset ? asset->find("name") : nullptr;
                const auto name = name_value ? name_value->as_string() : std::nullopt;
                if (!name) continue;
                if (*name == asset_name) ++asset_matches;
                else if (*name == sidecar_name) ++sidecar_matches;
            }
        }
        if (asset_matches == 1 && sidecar_matches == 1) {
            result.native_download_url = NativeDownloadUrl(*tag, asset_name);
        }
    }
    return result;
}

UpdateNavigationTarget ResolveUpdateNavigation(const UpdateCheckState& state) {
    if (state.status != UpdateCheckStatus::Available || state.latest_version.size() > 32 ||
        !state.latest_version.starts_with(L'v')) {
        return {};
    }
    const auto tag = WideAscii(state.latest_version);
    if (!tag || !ParseSemanticVersion(*tag)) return {};
    const std::wstring expected_release = std::wstring(kReleasePagePrefix) + state.latest_version;
    if (state.release_url != expected_release) return {};

    const std::string asset_name = NativeAssetName(std::string_view(*tag).substr(1));
    const std::wstring expected_download = NativeDownloadUrl(*tag, asset_name);
    if (!state.native_download_url.empty() && state.native_download_url == expected_download) {
        return {UpdateNavigationKind::NativeDownload, expected_download};
    }
    return {UpdateNavigationKind::ReleasePage, expected_release};
}

UpdateCheckState FetchLatestRelease(std::stop_token stop) {
    const std::vector<std::pair<std::wstring, std::wstring>> headers{
        {L"Accept", L"application/vnd.github+json"},
        {L"X-GitHub-Api-Version", L"2022-11-28"},
    };
    const HttpResponse response = WinHttpClient{}.Get(kLatestReleaseApi, headers, stop);
    if (stop.stop_requested() || !response.ok()) return Failed();
    return EvaluateLatestRelease(response.body, CODEX_PARTNER_VERSION_STRING);
}

}  // namespace codex_partner
