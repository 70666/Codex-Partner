#include "tray_icon_win32.h"

namespace codex_partner {

HICON CreateTrayIconHandle(const TrayIconPixels& rgba) noexcept {
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = static_cast<LONG>(kTrayIconSize);
    header.bV5Height = -static_cast<LONG>(kTrayIconSize);
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    void* bitmap_bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header),
        DIB_RGB_COLORS, &bitmap_bits, nullptr, 0);
    if (screen) ReleaseDC(nullptr, screen);
    if (!color || !bitmap_bits) {
        if (color) DeleteObject(color);
        return nullptr;
    }

    auto* bgra = static_cast<std::uint8_t*>(bitmap_bits);
    for (std::size_t offset = 0; offset < rgba.size(); offset += 4) {
        const unsigned alpha = rgba[offset + 3];
        bgra[offset] = static_cast<std::uint8_t>(rgba[offset + 2] * alpha / 255U);
        bgra[offset + 1] = static_cast<std::uint8_t>(rgba[offset + 1] * alpha / 255U);
        bgra[offset + 2] = static_cast<std::uint8_t>(rgba[offset] * alpha / 255U);
        bgra[offset + 3] = static_cast<std::uint8_t>(alpha);
    }

    constexpr std::size_t mask_stride = ((kTrayIconSize + 15U) / 16U) * 2U;
    std::array<std::uint8_t, mask_stride * kTrayIconSize> mask_bits{};
    HBITMAP mask = CreateBitmap(static_cast<int>(kTrayIconSize), static_cast<int>(kTrayIconSize),
        1, 1, mask_bits.data());
    if (!mask) {
        DeleteObject(color);
        return nullptr;
    }
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = color;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

HICON CreateUsageTrayIconHandle(const UsageSnapshot& snapshot) noexcept {
    return CreateTrayIconHandle(RenderTrayIconRgba(BuildTrayIconModel(snapshot)));
}

}  // namespace codex_partner
