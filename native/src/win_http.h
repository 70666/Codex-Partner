#pragma once

#include <string>
#include <stop_token>
#include <utility>
#include <vector>

namespace codex_partner {

struct HttpResponse {
    unsigned status = 0;
    std::string body;
    std::wstring error;
    [[nodiscard]] bool ok() const noexcept { return error.empty() && status >= 200 && status < 300; }
};

class WinHttpClient {
public:
    [[nodiscard]] HttpResponse Get(
        const std::wstring& url,
        const std::vector<std::pair<std::wstring, std::wstring>>& headers,
        std::stop_token stop = {}) const;
};

}  // namespace codex_partner
