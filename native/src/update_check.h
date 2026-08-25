#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace codex_partner {

struct SemanticVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    auto operator<=>(const SemanticVersion&) const = default;
};

enum class UpdateCheckStatus { Idle, Checking, UpToDate, Available, Failed };

struct UpdateCheckState {
    UpdateCheckStatus status = UpdateCheckStatus::Idle;
    std::wstring latest_version;
    std::wstring release_url;
    std::wstring native_download_url;
};

enum class UpdateNavigationKind { None, ReleasePage, NativeDownload };

struct UpdateNavigationTarget {
    UpdateNavigationKind kind = UpdateNavigationKind::None;
    std::wstring url;
};

// Accepts a strict numeric X.Y.Z version, with an optional leading lowercase v.
// Prerelease/build suffixes and ambiguous leading zeroes are intentionally rejected.
[[nodiscard]] std::optional<SemanticVersion> ParseSemanticVersion(std::string_view text) noexcept;

// Parses the public GitHub latest-release response. Returned URLs are built
// locally from a validated canonical tag; remote URL fields are never trusted.
// A direct Native download is exposed only when the exact versioned EXE and
// its SHA-256 sidecar both exist exactly once in the release asset list.
[[nodiscard]] UpdateCheckState EvaluateLatestRelease(
    std::string_view response_json,
    std::string_view current_version);

// Revalidates a stored/completed update state before it crosses the Windows
// shell boundary. A malformed direct target safely falls back to the canonical
// release page; a malformed release page produces no navigation at all.
[[nodiscard]] UpdateNavigationTarget ResolveUpdateNavigation(const UpdateCheckState& state);

// Performs one unauthenticated, read-only request to the canonical public
// repository. It never downloads or executes a release asset; user activation
// may later open a locally constructed GitHub download URL in the browser.
[[nodiscard]] UpdateCheckState FetchLatestRelease(std::stop_token stop = {});

}  // namespace codex_partner
