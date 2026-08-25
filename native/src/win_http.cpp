#include "win_http.h"
#include "version.h"

#include <Windows.h>
#include <winhttp.h>

#include <array>
#include <atomic>
#include <sstream>

namespace codex_partner {
namespace {

constexpr std::size_t kMaxResponseBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxHeaderNameCharacters = 256;
constexpr std::size_t kMaxHeaderValueCharacters = 16 * 1024;

class InternetHandle {
public:
    explicit InternetHandle(void* handle) noexcept : handle_(handle) {}
    ~InternetHandle() { Close(); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;

    [[nodiscard]] void* Get() const noexcept { return handle_.load(std::memory_order_acquire); }
    [[nodiscard]] explicit operator bool() const noexcept { return Get() != nullptr; }
    void Close() noexcept {
        if (void* handle = handle_.exchange(nullptr, std::memory_order_acq_rel)) WinHttpCloseHandle(handle);
    }

private:
    std::atomic<void*> handle_;
};

std::wstring SystemError(DWORD code) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = length && message ? std::wstring(message, length) : L"Windows error " + std::to_wstring(code);
    if (message) LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result;
}

bool IsSafeHeader(std::wstring_view name, std::wstring_view value) {
    return !name.empty() && name.size() <= kMaxHeaderNameCharacters &&
        value.size() <= kMaxHeaderValueCharacters &&
        name.find_first_of(L":\r\n") == std::wstring_view::npos &&
        value.find_first_of(L"\r\n") == std::wstring_view::npos;
}

}  // namespace

HttpResponse WinHttpClient::Get(const std::wstring& url, const std::vector<std::pair<std::wstring, std::wstring>>& headers, std::stop_token stop) const {
    if (stop.stop_requested()) return {0, {}, L"Usage request cancelled"};
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components)) return {0, {}, L"Invalid usage URL"};
    if (components.nScheme != INTERNET_SCHEME_HTTPS) return {0, {}, L"Usage requests require HTTPS"};

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength) path.append(components.lpszExtraInfo, components.dwExtraInfoLength);

    InternetHandle session(WinHttpOpen(L"Codex-Partner/" CODEX_PARTNER_VERSION_WIDE, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) return {0, {}, SystemError(GetLastError())};
    if (!WinHttpSetTimeouts(session.Get(), 8000, 8000, 15000, 15000)) return {0, {}, SystemError(GetLastError())};
    if (stop.stop_requested()) return {0, {}, L"Usage request cancelled"};
    InternetHandle connection(WinHttpConnect(session.Get(), host.c_str(), components.nPort, 0));
    if (!connection) return {0, {}, SystemError(GetLastError())};
    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request(WinHttpOpenRequest(connection.Get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) return {0, {}, SystemError(GetLastError())};
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy))) {
        return {0, {}, SystemError(GetLastError())};
    }
    std::stop_callback cancel(stop, [&request] { request.Close(); });
    if (stop.stop_requested()) return {0, {}, L"Usage request cancelled"};

    for (const auto& [name, value] : headers) {
        if (!IsSafeHeader(name, value)) return {0, {}, L"Invalid request header"};
        const std::wstring header = name + L": " + value;
        if (!WinHttpAddRequestHeaders(request.Get(), header.c_str(), static_cast<DWORD>(header.size()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) return {0, {}, SystemError(GetLastError())};
    }
    if (!WinHttpSendRequest(request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.Get(), nullptr)) return {0, {}, SystemError(GetLastError())};

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        return {0, {}, SystemError(GetLastError())};
    }

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.Get(), &available)) return {status, std::move(body), SystemError(GetLastError())};
        if (available == 0) break;
        if (available > kMaxResponseBytes - body.size()) return {status, std::move(body), L"Usage response exceeded the 4 MB safety limit"};
        const std::size_t old_size = body.size();
        body.resize(old_size + available);
        DWORD received = 0;
        if (!WinHttpReadData(request.Get(), body.data() + old_size, available, &received)) return {status, std::move(body), SystemError(GetLastError())};
        body.resize(old_size + received);
        if (received == 0) break;
    }
    return {status, std::move(body), {}};
}

}  // namespace codex_partner
