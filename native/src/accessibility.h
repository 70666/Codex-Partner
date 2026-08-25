#pragma once

#include "accessibility_model.h"

#include <Windows.h>

#include <vector>

namespace codex_partner::accessibility {

constexpr UINT kQueryElementsMessage = WM_APP + 60;
constexpr UINT kActivateElementMessage = WM_APP + 61;
constexpr UINT kFocusElementMessage = WM_APP + 62;

[[nodiscard]] LRESULT HandleGetObject(HWND window, WPARAM wparam, LPARAM lparam);
[[nodiscard]] bool QueryElements(HWND window, std::vector<Element>& elements) noexcept;

}  // namespace codex_partner::accessibility
