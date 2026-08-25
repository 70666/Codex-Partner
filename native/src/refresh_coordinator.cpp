#include "refresh_coordinator.h"

namespace codex_partner {

RefreshRequestDisposition RefreshCoordinator::Request(bool retain_if_active) noexcept {
    if (!active_) {
        active_ = true;
        return RefreshRequestDisposition::Started;
    }
    if (retain_if_active && !queued_) {
        queued_ = true;
        return RefreshRequestDisposition::Queued;
    }
    return RefreshRequestDisposition::Coalesced;
}

bool RefreshCoordinator::FinishCycle() noexcept {
    if (queued_) {
        queued_ = false;
        return true;
    }
    active_ = false;
    return false;
}

}  // namespace codex_partner
