#pragma once

#include "tray_icon_model.h"

#include <Windows.h>

namespace codex_partner {

// The caller owns the returned icon and must release it with DestroyIcon.
[[nodiscard]] HICON CreateTrayIconHandle(const TrayIconPixels& rgba) noexcept;
[[nodiscard]] HICON CreateUsageTrayIconHandle(const UsageSnapshot& snapshot) noexcept;

}  // namespace codex_partner
