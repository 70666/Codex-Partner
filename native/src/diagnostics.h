#pragma once

#include "settings_store.h"
#include "usage_model.h"

#include <string>

namespace codex_partner {

// Produces a report suitable for issue templates. It intentionally contains
// derived state only: no paths, provider responses, credentials, or account IDs.
[[nodiscard]] std::wstring BuildDiagnosticSummary(
    const AppSettings& settings,
    const UsageSnapshot& snapshot);

}  // namespace codex_partner
