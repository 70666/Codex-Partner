#include "app.h"

#include <Windows.h>
#include <Ole2.h>

#include <exception>
#include <iterator>

namespace {

class OleScope {
public:
    OleScope() noexcept : initialized_(SUCCEEDED(OleInitialize(nullptr))) {}
    ~OleScope() { if (initialized_) OleUninitialize(); }
    OleScope(const OleScope&) = delete;
    OleScope& operator=(const OleScope&) = delete;

private:
    bool initialized_ = false;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    try {
        OutputDebugStringW(L"[CodexPartner] process entry\n");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        OleScope ole;
        codex_partner::App app(instance);
        OutputDebugStringW(L"[CodexPartner] app constructed\n");
        if (!app.Initialize(show_command)) return 1;
        return app.Run();
    } catch (const std::exception& error) {
        wchar_t message[512]{};
        MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, message, static_cast<int>(std::size(message)));
        MessageBoxW(nullptr, message, L"Codex Partner startup error", MB_OK | MB_ICONERROR);
        return 2;
    } catch (...) {
        MessageBoxW(nullptr, L"An unexpected error occurred while starting Codex Partner.", L"Codex Partner startup error", MB_OK | MB_ICONERROR);
        return 3;
    }
}
