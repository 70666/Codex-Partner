#pragma once

namespace codex_partner {

enum class RefreshRequestDisposition {
    Started,
    Queued,
    Coalesced,
};

enum class RefreshPhase {
    Idle,
    FetchingUsage,
    ScanningSpend,
};

[[nodiscard]] constexpr RefreshPhase DeriveRefreshPhase(
    bool usage_refreshing, bool spend_refreshing) noexcept {
    if (usage_refreshing) return RefreshPhase::FetchingUsage;
    if (spend_refreshing) return RefreshPhase::ScanningSpend;
    return RefreshPhase::Idle;
}

[[nodiscard]] constexpr bool RefreshIsActive(RefreshPhase phase) noexcept {
    return phase != RefreshPhase::Idle;
}

// UI-thread state machine for provider + local-spend refresh cycles. While a
// cycle is active, any number of requests collapse into one trailing cycle so
// user, timer, and resume intent is never silently lost or amplified.
class RefreshCoordinator final {
public:
    [[nodiscard]] RefreshRequestDisposition Request(bool retain_if_active = true) noexcept;
    [[nodiscard]] bool FinishCycle() noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool queued() const noexcept { return queued_; }

private:
    bool active_ = false;
    bool queued_ = false;
};

}  // namespace codex_partner
