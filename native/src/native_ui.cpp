#include "native_ui.h"
#include "notification_snooze.h"
#include "usage_freshness.h"
#include "usage_summary.h"
#include "version.h"
#include "resource.h"

#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>

namespace codex_partner::ui {
namespace {

using namespace Gdiplus;

struct Palette {
    Color background;
    Color surface;
    Color elevated;
    Color border;
    Color text;
    Color secondary;
    Color muted;
    Color accent;
    Color accent_soft;
    Color green;
    Color yellow;
    Color red;
};

Image* PartnerMark() noexcept {
    struct ResourceImage {
        IStream* stream = nullptr;
        Image* image = nullptr;
        ResourceImage() {
            HMODULE module = GetModuleHandleW(nullptr);
            HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_CODEX_PARTNER_MARK), RT_RCDATA);
            if (!resource) return;
            HGLOBAL loaded = LoadResource(module, resource);
            const DWORD size = SizeofResource(module, resource);
            const void* bytes = loaded ? LockResource(loaded) : nullptr;
            if (!bytes || size == 0) return;
            stream = SHCreateMemStream(static_cast<const BYTE*>(bytes), size);
            if (!stream) return;
            image = Image::FromStream(stream, FALSE);
            if (!image || image->GetLastStatus() != Ok) {
                delete image;
                image = nullptr;
            }
        }
        ~ResourceImage() {
            delete image;
            if (stream) stream->Release();
        }
    };
    static ResourceImage resource;
    return resource.image;
}

void DrawPartnerMark(Graphics& graphics, const RectF& rect) {
    if (Image* mark = PartnerMark()) {
        graphics.DrawImage(mark, rect);
    }
}

Palette Colors(bool light, BackdropStyle backdrop) {
    if (light && backdrop == BackdropStyle::AcrylicGlass) return {
        Color(44, 255, 248, 251), Color(172, 255, 253, 254), Color(190, 255, 246, 250),
        Color(132, 239, 218, 228), Color(255, 52, 42, 50), Color(255, 112, 91, 103),
        Color(255, 130, 106, 117), Color(255, 196, 63, 115), Color(166, 253, 229, 239),
        Color(255, 39, 122, 93), Color(255, 143, 90, 0), Color(255, 191, 52, 77)};
    if (light) return {
        Color(255, 255, 248, 251), Color(255, 255, 253, 254), Color(255, 255, 246, 250),
        Color(255, 239, 218, 228), Color(255, 52, 42, 50), Color(255, 112, 91, 103),
        Color(255, 130, 106, 117), Color(255, 196, 63, 115), Color(255, 253, 229, 239),
        Color(255, 39, 122, 93), Color(255, 143, 90, 0), Color(255, 191, 52, 77)};
    return {
        Color(255, 24, 25, 28), Color(255, 34, 36, 41), Color(255, 41, 43, 49),
        Color(255, 60, 63, 71), Color(255, 244, 245, 247), Color(255, 174, 179, 189),
        Color(255, 122, 128, 140), Color(255, 76, 166, 255), Color(255, 30, 57, 79),
        Color(255, 65, 201, 132), Color(255, 246, 184, 71), Color(255, 255, 105, 117)};
}

Color Blend(const Color& from, const Color& to, float amount) {
    const float t = std::clamp(amount, 0.0F, 1.0F);
    const auto channel = [t](BYTE a, BYTE b) { return static_cast<BYTE>(std::lround(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t)); };
    return Color(channel(from.GetA(), to.GetA()), channel(from.GetR(), to.GetR()), channel(from.GetG(), to.GetG()), channel(from.GetB(), to.GetB()));
}

void DrawCherryPetal(Graphics& graphics, float x, float y, float size, float angle, BYTE alpha) {
    const GraphicsState state = graphics.Save();
    graphics.TranslateTransform(x, y);
    graphics.RotateTransform(angle);
    GraphicsPath petal;
    petal.StartFigure();
    petal.AddBezier(0.0F, -size, size * 0.78F, -size * 0.62F,
        size * 0.62F, size * 0.48F, 0.0F, size);
    petal.AddBezier(0.0F, size, -size * 0.62F, size * 0.48F,
        -size * 0.78F, -size * 0.62F, 0.0F, -size);
    petal.CloseFigure();
    SolidBrush fill(Color(alpha, 244, 136, 177));
    Pen edge(Color(static_cast<BYTE>(std::min(255, static_cast<int>(alpha) + 22)), 222, 91, 145), 0.65F);
    graphics.FillPath(&fill, &petal);
    graphics.DrawPath(&edge, &petal);
    SolidBrush highlight(Color(static_cast<BYTE>(alpha / 2), 255, 240, 247));
    graphics.FillEllipse(&highlight, -size * 0.18F, -size * 0.68F, size * 0.36F, size * 0.56F);
    graphics.Restore(state);
}

enum class BlossomSurface {
    Popup,
    Settings,
    FloatBar,
};

void DrawCherryForeground(Graphics& graphics, float height, bool light, double ambient_phase,
    BlossomSurface surface) {
    if (!light || ambient_phase < 0.0) return;

    struct EdgePetal {
        float y;
        float speed;
        float size;
        float phase;
        float spin;
    };
    static constexpr std::array<EdgePetal, 6> seeds{{
        {0.08F, 8.2F, 8.6F, 0.4F, 14.0F},
        {0.28F, 6.7F, 7.3F, 2.2F, -11.0F},
        {0.47F, 9.4F, 9.2F, 4.7F, 16.0F},
        {0.66F, 7.5F, 7.8F, 1.3F, -15.0F},
        {0.83F, 8.8F, 8.2F, 3.5F, 12.0F},
        {0.94F, 6.2F, 7.0F, 5.4F, -17.0F},
    }};

    const std::size_t count = surface == BlossomSurface::Settings ? 6 :
        surface == BlossomSurface::Popup ? 4 : 2;
    for (std::size_t index = 0; index < count; ++index) {
        const EdgePetal& seed = seeds[index];
        float lane_x = 0.0F;
        float size = seed.size;
        BYTE alpha = 76;
        if (surface == BlossomSurface::Settings) {
            constexpr std::array<float, 6> lanes{8.0F, 202.0F, 692.0F, 202.0F, 8.0F, 692.0F};
            lane_x = lanes[index];
        } else if (surface == BlossomSurface::Popup) {
            constexpr std::array<float, 4> lanes{7.0F, 393.0F, 8.0F, 392.0F};
            lane_x = lanes[index];
            size *= 0.86F;
            alpha = 72;
        } else {
            constexpr std::array<float, 2> lanes{138.0F, 378.0F};
            lane_x = lanes[index];
            size *= 0.64F;
            alpha = 64;
        }
        const float travel = height + size * 3.0F;
        const float y = static_cast<float>(std::fmod(seed.y * height + ambient_phase * seed.speed,
            static_cast<double>(travel))) - size * 1.5F;
        const float x = lane_x + static_cast<float>(std::sin(ambient_phase * 0.54 + seed.phase)) * 2.5F;
        const float angle = static_cast<float>(std::fmod(ambient_phase * seed.spin + seed.phase * 27.0F, 360.0));
        DrawCherryPetal(graphics, x, y, size, angle, alpha);
    }
}

void DrawWindowCanvas(Graphics& graphics, float width, float height, const Palette& palette,
    bool light, double ambient_phase, float variant, BackdropStyle backdrop) {
    if (!light || ambient_phase < 0.0) {
        SolidBrush background(palette.background);
        graphics.FillRectangle(&background, 0.0F, 0.0F, width, height);
        return;
    }

    const bool glass = backdrop != BackdropStyle::Solid;
    if (glass) {
        const CompositingMode previous = graphics.GetCompositingMode();
        graphics.SetCompositingMode(CompositingModeSourceCopy);
        SolidBrush clear(Color(0, 0, 0, 0));
        graphics.FillRectangle(&clear, 0.0F, 0.0F, width, height);
        graphics.SetCompositingMode(previous);
        LinearGradientBrush glass_tint(RectF(0.0F, 0.0F, width, height),
            Color(32, 255, 248, 251), Color(18, 247, 220, 235), 112.0F);
        graphics.FillRectangle(&glass_tint, 0.0F, 0.0F, width, height);
    } else {
        LinearGradientBrush background(RectF(0.0F, 0.0F, width, height),
            Color(255, 255, 252, 253), palette.background, 112.0F);
        graphics.FillRectangle(&background, 0.0F, 0.0F, width, height);
    }

    // Broad, barely-visible blooms add depth without tinting cards or text.
    SolidBrush upper_glow(Color(28, 255, 205, 224));
    SolidBrush lower_glow(Color(18, 224, 190, 239));
    graphics.FillEllipse(&upper_glow, width * 0.62F, -height * 0.18F, width * 0.58F, height * 0.48F);
    graphics.FillEllipse(&lower_glow, -width * 0.22F, height * 0.68F, width * 0.56F, height * 0.42F);

    struct PetalSeed {
        float x;
        float y;
        float speed;
        float sway;
        float size;
        float phase;
        float spin;
        BYTE alpha;
    };
    static constexpr std::array<PetalSeed, 12> seeds{{
        {0.05F, 0.08F, 9.0F, 13.0F, 5.7F, 0.2F, 17.0F, 64},
        {0.16F, 0.58F, 6.5F, 9.0F, 4.8F, 2.1F, -13.0F, 54},
        {0.28F, 0.22F, 8.0F, 17.0F, 7.2F, 4.3F, 11.0F, 62},
        {0.39F, 0.78F, 5.8F, 11.0F, 5.0F, 1.4F, -19.0F, 50},
        {0.49F, 0.42F, 7.2F, 15.0F, 6.3F, 5.2F, 15.0F, 60},
        {0.61F, 0.12F, 8.8F, 10.0F, 4.6F, 3.2F, -16.0F, 52},
        {0.71F, 0.69F, 6.0F, 18.0F, 6.9F, 0.9F, 12.0F, 58},
        {0.82F, 0.31F, 7.8F, 12.0F, 5.4F, 2.8F, -14.0F, 64},
        {0.93F, 0.83F, 5.5F, 10.0F, 4.9F, 4.7F, 18.0F, 50},
        {0.12F, 0.91F, 6.8F, 14.0F, 6.1F, 3.7F, -11.0F, 57},
        {0.55F, 0.94F, 7.4F, 9.0F, 4.5F, 1.8F, 20.0F, 48},
        {0.88F, 0.06F, 8.3F, 16.0F, 6.6F, 5.8F, -17.0F, 60}
    }};
    const std::size_t count = height < 120.0F ? 4 : width < 500.0F ? 8 : seeds.size();
    for (std::size_t index = 0; index < count; ++index) {
        const PetalSeed& seed = seeds[(index + static_cast<std::size_t>(variant)) % seeds.size()];
        const float travel = height + 36.0F;
        const float y = static_cast<float>(std::fmod(seed.y * height + ambient_phase * seed.speed + variant * 19.0F,
            static_cast<double>(travel))) - 18.0F;
        const float base_x = seed.x * width;
        const float x = base_x + static_cast<float>(std::sin(ambient_phase * 0.72 + seed.phase + variant)) * seed.sway;
        const float angle = static_cast<float>(std::fmod(ambient_phase * seed.spin + seed.phase * 31.0F, 360.0));
        DrawCherryPetal(graphics, x, y, seed.size, angle, seed.alpha);
    }
}

float Scale(HWND window) {
    return static_cast<float>(GetDpiForWindow(window)) / 96.0F;
}

RectF R(float x, float y, float width, float height) { return RectF(x, y, width, height); }

std::unique_ptr<GraphicsPath> RoundedPath(const RectF& rect, float radius) {
    auto path = std::make_unique<GraphicsPath>(FillModeAlternate);
    const float diameter = radius * 2.0F;
    path->AddArc(rect.X, rect.Y, diameter, diameter, 180.0F, 90.0F);
    path->AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0F, 90.0F);
    path->AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0F, 90.0F);
    path->AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0F, 90.0F);
    path->CloseFigure();
    return path;
}

void FillRounded(Graphics& graphics, const RectF& rect, float radius, const Color& color) {
    SolidBrush brush(color);
    auto path = RoundedPath(rect, radius);
    graphics.FillPath(&brush, path.get());
}

void StrokeRounded(Graphics& graphics, const RectF& rect, float radius, const Color& color) {
    Pen pen(color, 1.0F);
    auto path = RoundedPath(rect, radius);
    graphics.DrawPath(&pen, path.get());
}

void Text(Graphics& graphics, const std::wstring& value, const RectF& rect, float size, FontStyle style, const Color& color, StringAlignment alignment = StringAlignmentNear) {
    FontFamily family(L"Segoe UI");
    Font font(&family, size, style, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    format.SetAlignment(alignment);
    format.SetLineAlignment(StringAlignmentCenter);
    graphics.DrawString(value.c_str(), static_cast<INT>(value.size()), &font, rect, &format, &brush);
}

void TextWrapped(Graphics& graphics, const std::wstring& value, const RectF& rect, float size, FontStyle style, const Color& color) {
    FontFamily family(L"Segoe UI");
    Font font(&family, size, style, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetTrimming(StringTrimmingEllipsisWord);
    format.SetAlignment(StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentNear);
    graphics.DrawString(value.c_str(), static_cast<INT>(value.size()), &font, rect, &format, &brush);
}

Color HealthColor(UsageHealth health, const Palette& palette) {
    switch (health) {
    case UsageHealth::Healthy: return palette.green;
    case UsageHealth::Watch: return palette.yellow;
    case UsageHealth::Critical:
    case UsageHealth::Exhausted: return palette.red;
    case UsageHealth::Loading: return palette.accent;
    case UsageHealth::Unavailable: return palette.muted;
    }
    return palette.muted;
}

std::wstring Percent(double value) {
    return std::to_wstring(static_cast<int>(std::round(std::clamp(value, 0.0, 100.0)))) + L"%";
}

const wchar_t* T(bool chinese, const wchar_t* english, const wchar_t* simplified_chinese);
std::wstring LocalReset(const RateWindow& window, bool chinese);
std::wstring LocalWindowTitle(const RateWindow& window, bool chinese);

void DrawIconButton(Graphics& graphics, RectF rect, const Palette& palette, bool gear, bool hovered, bool pressed, float progress, float refresh_angle) {
    if (pressed) rect = R(rect.X + 1.0F, rect.Y + 1.0F, rect.Width - 2.0F, rect.Height - 2.0F);
    const float emphasis = hovered ? progress : 0.0F;
    FillRounded(graphics, rect, 8.0F, Blend(palette.elevated, palette.accent_soft, emphasis));
    StrokeRounded(graphics, rect, 8.0F, pressed ? palette.accent : Blend(palette.border, palette.accent, emphasis * 0.65F));
    Pen pen(pressed || hovered ? palette.accent : palette.secondary, pressed ? 2.0F : 1.7F);
    const float cx = rect.X + rect.Width / 2.0F;
    const float cy = rect.Y + rect.Height / 2.0F;
    if (gear) {
        graphics.DrawEllipse(&pen, cx - 5.0F, cy - 5.0F, 10.0F, 10.0F);
        graphics.DrawEllipse(&pen, cx - 1.5F, cy - 1.5F, 3.0F, 3.0F);
    } else {
        graphics.DrawArc(&pen, cx - 6.0F, cy - 6.0F, 12.0F, 12.0F, 35.0F + refresh_angle, 285.0F);
        const float radians = (refresh_angle - 55.0F) * 3.14159265F / 180.0F;
        const float ax = cx + std::cos(radians) * 6.0F;
        const float ay = cy + std::sin(radians) * 6.0F;
        PointF arrow[3]{{ax - 2.5F, ay - 1.5F}, {ax + 1.5F, ay - 2.5F}, {ax + 1.0F, ay + 2.0F}};
        SolidBrush arrow_brush(palette.secondary);
        graphics.FillPolygon(&arrow_brush, arrow, 3);
    }
}

void DrawCopyButton(Graphics& graphics, RectF rect, const Palette& palette, CopySummaryState copy_state,
    bool hovered, bool pressed, float progress) {
    const bool copied = copy_state == CopySummaryState::Copied;
    const bool failed = copy_state == CopySummaryState::Failed;
    const Color state_color = copied ? palette.green : failed ? palette.red : palette.accent;
    if (pressed) rect = R(rect.X + 1.0F, rect.Y + 1.0F, rect.Width - 2.0F, rect.Height - 2.0F);
    const float emphasis = hovered ? progress : 0.0F;
    FillRounded(graphics, rect, 8.0F, Blend(palette.elevated, copied || failed ? state_color : palette.accent_soft,
        copied || failed ? 0.18F : emphasis));
    StrokeRounded(graphics, rect, 8.0F, copied || failed ? state_color :
        pressed ? palette.accent : Blend(palette.border, palette.accent, emphasis * 0.65F));
    Pen pen(copied || failed ? state_color : pressed || hovered ? palette.accent : palette.secondary,
        pressed ? 2.0F : 1.7F);
    const float cx = rect.X + rect.Width / 2.0F;
    const float cy = rect.Y + rect.Height / 2.0F;
    if (copied) {
        graphics.DrawLine(&pen, cx - 6.0F, cy, cx - 1.5F, cy + 4.5F);
        graphics.DrawLine(&pen, cx - 1.5F, cy + 4.5F, cx + 7.0F, cy - 5.0F);
    } else if (failed) {
        graphics.DrawLine(&pen, cx - 5.0F, cy - 5.0F, cx + 5.0F, cy + 5.0F);
        graphics.DrawLine(&pen, cx + 5.0F, cy - 5.0F, cx - 5.0F, cy + 5.0F);
    } else {
        graphics.DrawRectangle(&pen, cx - 6.0F, cy - 4.0F, 9.0F, 10.0F);
        graphics.DrawRectangle(&pen, cx - 2.0F, cy - 7.0F, 9.0F, 10.0F);
    }
}

void DrawCloseButton(Graphics& graphics, RectF rect, const Palette& palette, bool hovered, bool pressed,
    float progress) {
    if (pressed) rect = R(rect.X + 1.0F, rect.Y + 1.0F, rect.Width - 2.0F, rect.Height - 2.0F);
    const float emphasis = hovered ? progress : 0.0F;
    FillRounded(graphics, rect, 8.0F, Blend(palette.elevated, palette.red, emphasis * 0.16F));
    StrokeRounded(graphics, rect, 8.0F, Blend(palette.border, palette.red, emphasis * 0.72F));
    Pen pen(hovered || pressed ? palette.red : palette.secondary, pressed ? 2.0F : 1.7F);
    const float cx = rect.X + rect.Width / 2.0F;
    const float cy = rect.Y + rect.Height / 2.0F;
    graphics.DrawLine(&pen, cx - 5.0F, cy - 5.0F, cx + 5.0F, cy + 5.0F);
    graphics.DrawLine(&pen, cx + 5.0F, cy - 5.0F, cx - 5.0F, cy + 5.0F);
}

void DrawProgress(Graphics& graphics, const RectF& rect, double percent, const Palette& palette) {
    FillRounded(graphics, rect, rect.Height / 2.0F, palette.border);
    const float width = std::max(rect.Height, rect.Width * static_cast<float>(std::clamp(percent, 0.0, 100.0) / 100.0));
    Color color = percent >= 90.0 ? palette.red : percent >= 70.0 ? palette.yellow : palette.accent;
    FillRounded(graphics, R(rect.X, rect.Y, width, rect.Height), rect.Height / 2.0F, color);
}

void DrawRateCard(Graphics& graphics, const RateWindow& window, float y, const Palette& palette, bool chinese) {
    const RectF card = R(16.0F, y, 368.0F, 112.0F);
    FillRounded(graphics, card, 12.0F, palette.surface);
    StrokeRounded(graphics, card, 12.0F, palette.border);
    Text(graphics, LocalWindowTitle(window, chinese), R(30.0F, y + 14.0F, 210.0F, 24.0F), 13.0F, FontStyleBold, palette.text);
    Text(graphics, Percent(window.used_percent), R(282.0F, y + 11.0F, 72.0F, 28.0F), 21.0F, FontStyleBold, palette.text, StringAlignmentFar);
    DrawProgress(graphics, R(30.0F, y + 52.0F, 324.0F, 8.0F), window.used_percent, palette);
    Text(graphics, LocalReset(window, chinese), R(30.0F, y + 70.0F, 324.0F, 22.0F),
        11.0F, FontStyleRegular, palette.muted, StringAlignmentFar);
}

void DrawSettingRow(Graphics& graphics, float y, const std::wstring& title, const std::wstring& description, const std::wstring& value, const Palette& palette, bool toggle = false, bool checked = false, bool hovered = false, bool pressed = false, float progress = 0.0F, bool attention = false) {
    RectF row = R(214.0F, y, 454.0F, 64.0F);
    if (pressed) row = R(215.0F, y + 1.0F, 452.0F, 62.0F);
    FillRounded(graphics, row, 10.0F, Blend(palette.surface, palette.accent_soft, hovered ? progress * 0.55F : 0.0F));
    StrokeRounded(graphics, row, 10.0F, attention ? palette.red : pressed ? palette.accent : Blend(palette.border, palette.accent, hovered ? progress * 0.55F : 0.0F));
    const bool has_description = !description.empty();
    Text(graphics, title, R(230.0F, y + (has_description ? 8.0F : 20.0F), 250.0F, 22.0F), 13.0F, FontStyleBold, palette.text);
    if (has_description) {
        Text(graphics, description, R(230.0F, y + 31.0F, 310.0F, 18.0F), 10.0F, FontStyleRegular, attention ? palette.red : palette.muted);
    }
    if (toggle) {
        const Color track = checked ? palette.accent : palette.border;
        FillRounded(graphics, R(602.0F, y + 20.0F, 46.0F, 24.0F), 12.0F, track);
        SolidBrush knob(palette.surface);
        graphics.FillEllipse(&knob, checked ? 627.0F : 605.0F, y + 23.0F, 18.0F, 18.0F);
    } else {
        FillRounded(graphics, R(552.0F, y + 17.0F, 96.0F, 30.0F), 7.0F, palette.elevated);
        StrokeRounded(graphics, R(552.0F, y + 17.0F, 96.0F, 30.0F), 7.0F, palette.border);
        const float value_size = value.size() > 12 ? 8.5F : value.size() > 9 ? 9.5F : 11.0F;
        Text(graphics, value, R(556.0F, y + 17.0F, 76.0F, 30.0F), value_size, FontStyleRegular, attention ? palette.red : palette.text, StringAlignmentCenter);
        Text(graphics, L"\u25be", R(628.0F, y + 17.0F, 14.0F, 30.0F), 10.0F, FontStyleRegular, palette.muted, StringAlignmentCenter);
    }
}

void DrawWideAction(Graphics& graphics, float y, const std::wstring& label, const Palette& palette, bool hovered, bool pressed, float progress) {
    RectF button = pressed ? R(215.0F, y + 1.0F, 452.0F, 40.0F) : R(214.0F, y, 454.0F, 42.0F);
    FillRounded(graphics, button, 9.0F, Blend(palette.accent_soft, palette.accent, hovered ? progress * 0.18F : 0.0F));
    StrokeRounded(graphics, button, 9.0F, Blend(palette.accent_soft, palette.accent, hovered ? progress * 0.6F : 0.0F));
    Text(graphics, label, R(230.0F, y, 422.0F, 42.0F), 12.0F, FontStyleBold, palette.accent, StringAlignmentCenter);
}

void DrawPrimaryWideAction(Graphics& graphics, float y, const std::wstring& label, const Palette& palette,
    bool hovered, bool pressed, float progress) {
    RectF button = pressed ? R(215.0F, y + 1.0F, 452.0F, 40.0F) : R(214.0F, y, 454.0F, 42.0F);
    const Color fill = Blend(palette.accent, palette.text, hovered ? progress * 0.08F : 0.0F);
    FillRounded(graphics, button, 9.0F, fill);
    StrokeRounded(graphics, button, 9.0F, Blend(palette.accent, palette.text, hovered ? progress * 0.18F : 0.0F));
    Text(graphics, label, R(230.0F, y, 422.0F, 42.0F), 12.0F, FontStyleBold,
        Color(255, 255, 255, 255), StringAlignmentCenter);
}

std::wstring FormatUsd(const std::optional<double>& value) {
    if (!value) return L"\u2014";
    std::wostringstream output;
    output << L'$' << std::fixed << std::setprecision(*value < 0.01 ? 3 : 2) << std::max(0.0, *value);
    std::wstring formatted = output.str();
    const std::size_t integer_end = formatted.find(L'.');
    if (integer_end != std::wstring::npos) {
        for (std::size_t position = integer_end; position > 4; position -= 3) formatted.insert(position - 3, 1, L',');
    }
    return formatted;
}

std::wstring FormatSpendUsd(const std::optional<double>& value, bool partial) {
    const std::wstring formatted = FormatUsd(value);
    return value && partial ? L"≥" + formatted : formatted;
}

std::wstring CompactCount(std::uint64_t value) {
    std::wostringstream output;
    if (value >= 1'000'000'000ULL) output << std::fixed << std::setprecision(1) << static_cast<double>(value) / 1'000'000'000.0 << L'B';
    else if (value >= 1'000'000ULL) output << std::fixed << std::setprecision(1) << static_cast<double>(value) / 1'000'000.0 << L'M';
    else if (value >= 1'000ULL) output << std::fixed << std::setprecision(1) << static_cast<double>(value) / 1'000.0 << L'K';
    else output << value;
    return output.str();
}

std::wstring WideAscii(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring WideUtf8(std::string_view value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return WideAscii(value);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), length);
    return result;
}

struct ChartSeries {
    std::string model;
    std::array<std::size_t, 30> usage{};
    std::array<double, 30> cost{};
    std::array<bool, 30> partial{};
    std::size_t total_usage = 0;
    double total_cost = 0.0;
};

std::vector<ChartSeries> BuildChartSeries(const SpendSummary& spend) {
    const auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    const auto first_day = today - std::chrono::days{29};
    std::vector<ChartSeries> all;
    for (const DailyModelUsage& day : spend.daily_model_usage) {
        const auto offset = (day.day - first_day).count();
        if (offset < 0 || offset >= 30) continue;
        const std::size_t index = static_cast<std::size_t>(offset);
        for (const ModelUsageAmount& amount : day.models) {
            auto found = std::find_if(all.begin(), all.end(), [&](const ChartSeries& value) {
                return value.model == amount.model;
            });
            if (found == all.end()) {
                all.push_back(ChartSeries{});
                found = std::prev(all.end());
                found->model = amount.model;
            }
            found->usage[index] += amount.usage_count;
            found->cost[index] += amount.cost_usd;
            found->partial[index] = found->partial[index] || amount.partial;
            found->total_usage += amount.usage_count;
            found->total_cost += amount.cost_usd;
        }
    }
    std::sort(all.begin(), all.end(), [](const ChartSeries& left, const ChartSeries& right) {
        if (left.total_cost != right.total_cost) return left.total_cost > right.total_cost;
        return left.total_usage > right.total_usage;
    });
    if (all.size() <= 4) return all;
    ChartSeries other;
    other.model = "Other";
    for (std::size_t series = 3; series < all.size(); ++series) {
        other.total_usage += all[series].total_usage;
        other.total_cost += all[series].total_cost;
        for (std::size_t day = 0; day < 30; ++day) {
            other.usage[day] += all[series].usage[day];
            other.cost[day] += all[series].cost[day];
            other.partial[day] = other.partial[day] || all[series].partial[day];
        }
    }
    all.resize(3);
    all.push_back(std::move(other));
    return all;
}

std::wstring ChartDate(std::size_t index, bool chinese) {
    const auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    const std::chrono::year_month_day date{today - std::chrono::days{29 - static_cast<int>(index)}};
    std::wostringstream output;
    if (chinese) output << static_cast<int>(date.year()) << L'年' << static_cast<unsigned>(date.month()) << L'月'
                        << static_cast<unsigned>(date.day()) << L'日';
    else output << std::setfill(L'0') << static_cast<unsigned>(date.month()) << L'/'
                << std::setw(2) << static_cast<unsigned>(date.day());
    return output.str();
}

void DrawUsageWaveChart(Graphics& graphics, const SpendSummary& spend, const Palette& palette, bool light, bool chinese,
    std::optional<std::size_t> hovered_day, float progress) {
    const RectF card{214.0F, 258.0F, 454.0F, 216.0F};
    const RectF plot{232.0F, 322.0F, 418.0F, 116.0F};
    FillRounded(graphics, card, 12.0F, palette.surface);
    StrokeRounded(graphics, card, 12.0F, palette.border);
    Text(graphics, T(chinese, L"CODEX LOG ATTRIBUTION · 30 DAYS", L"CODEX 日志归因 · 近 30 天"),
        R(232.0F, 270.0F, 250.0F, 20.0F), 10.0F, FontStyleBold, palette.muted);
    Text(graphics, T(chinese, L"Includes automatic routing and background tasks", L"包含自动路由和后台任务，并非手动选择记录"),
        R(232.0F, 291.0F, 400.0F, 18.0F), 9.0F, FontStyleRegular, palette.secondary);

    const auto series = BuildChartSeries(spend);
    const std::array<Color, 4> fills = light ?
        std::array<Color, 4>{Color(166, 214, 54, 118), Color(158, 232, 91, 148),
            Color(150, 191, 116, 205), Color(142, 240, 170, 198)} :
        std::array<Color, 4>{Color(174, 27, 92, 214), Color(168, 47, 128, 237),
            Color(160, 91, 174, 255), Color(150, 147, 197, 253)};
    const std::array<Color, 4> strokes = light ?
        std::array<Color, 4>{Color(255, 180, 35, 91), Color(255, 204, 61, 118),
            Color(255, 143, 78, 174), Color(255, 178, 75, 124)} :
        std::array<Color, 4>{Color(255, 21, 74, 180), Color(255, 37, 99, 220),
            Color(255, 76, 150, 244), Color(255, 125, 176, 238)};

    Pen grid_pen(Color(palette.border.GetA(), palette.border.GetR(), palette.border.GetG(), palette.border.GetB()), 1.0F);
    grid_pen.SetDashStyle(DashStyleDash);
    for (int line = 0; line < 3; ++line) {
        const float y = plot.Y + static_cast<float>(line) * plot.Height / 2.0F;
        graphics.DrawLine(&grid_pen, plot.X, y, plot.GetRight(), y);
    }

    // The log scanner naturally produces sparse daily buckets. Plot a small
    // five-day weighted window so missing single days do not turn the wave into
    // misleading saw teeth; tooltips continue to report the exact raw counts.
    std::vector<std::array<double, 30>> smoothed(series.size());
    std::array<double, 30> totals{};
    double maximum = 0.0;
    constexpr std::array<double, 5> weights{1.0, 2.0, 3.0, 2.0, 1.0};
    for (std::size_t series_index = 0; series_index < series.size(); ++series_index) {
        for (std::size_t day = 0; day < 30; ++day) {
            double weighted = 0.0;
            double weight_total = 0.0;
            for (int offset = -2; offset <= 2; ++offset) {
                const int source = static_cast<int>(day) + offset;
                if (source < 0 || source >= 30) continue;
                const double weight = weights[static_cast<std::size_t>(offset + 2)];
                weighted += static_cast<double>(series[series_index].usage[static_cast<std::size_t>(source)]) * weight;
                weight_total += weight;
            }
            smoothed[series_index][day] = weight_total > 0.0 ? weighted / weight_total : 0.0;
            totals[day] += smoothed[series_index][day];
            maximum = std::max(maximum, totals[day]);
        }
    }
    std::array<double, 30> lower{};
    const float eased = 1.0F - std::pow(1.0F - std::clamp(progress, 0.0F, 1.0F), 3.0F);
    const GraphicsState wave_state = graphics.Save();
    graphics.SetClip(plot);
    for (std::size_t series_index = 0; series_index < series.size(); ++series_index) {
        std::vector<PointF> top;
        std::vector<PointF> bottom;
        top.reserve(30);
        bottom.reserve(30);
        for (std::size_t day = 0; day < 30; ++day) {
            const float x = plot.X + plot.Width * static_cast<float>(day) / 29.0F;
            const double upper_value = lower[day] + smoothed[series_index][day];
            const float upper = maximum == 0 ? plot.GetBottom() : plot.GetBottom() -
                plot.Height * static_cast<float>(upper_value / static_cast<double>(maximum)) * eased;
            const float base = maximum == 0 ? plot.GetBottom() : plot.GetBottom() -
                plot.Height * static_cast<float>(lower[day] / static_cast<double>(maximum)) * eased;
            top.emplace_back(x, upper);
            bottom.emplace_back(x, base);
            lower[day] = upper_value;
        }
        if (series[series_index].total_usage == 0) continue;
        GraphicsPath area;
        area.AddCurve(top.data(), static_cast<INT>(top.size()), 0.22F);
        area.AddLine(top.back(), bottom.back());
        std::reverse(bottom.begin(), bottom.end());
        area.AddCurve(bottom.data(), static_cast<INT>(bottom.size()), 0.22F);
        area.CloseFigure();
        SolidBrush fill(fills[series_index]);
        Pen stroke(strokes[series_index], 1.4F);
        graphics.FillPath(&fill, &area);
        graphics.DrawCurve(&stroke, top.data(), static_cast<INT>(top.size()), 0.22F);
    }
    graphics.Restore(wave_state);

    Text(graphics, ChartDate(0, chinese), R(plot.X, 441.0F, 70.0F, 18.0F), 8.5F, FontStyleRegular, palette.muted);
    Text(graphics, ChartDate(29, chinese), R(plot.GetRight() - 70.0F, 441.0F, 70.0F, 18.0F),
        8.5F, FontStyleRegular, palette.muted, StringAlignmentFar);
    if (series.empty()) {
        Text(graphics, T(chinese, L"No local model activity yet", L"暂无本地模型活动"), plot,
            11.0F, FontStyleRegular, palette.muted, StringAlignmentCenter);
        return;
    }
    if (!hovered_day || *hovered_day >= 30) return;
    const std::size_t day = *hovered_day;
    const float marker_x = plot.X + plot.Width * static_cast<float>(day) / 29.0F;
    Pen marker(palette.secondary, 1.0F);
    marker.SetDashStyle(DashStyleDash);
    graphics.DrawLine(&marker, marker_x, plot.Y, marker_x, plot.GetBottom());

    const float tooltip_width = 300.0F;
    const float tooltip_height = 42.0F + 22.0F * static_cast<float>(series.size());
    const float desired_tooltip_x = marker_x > plot.X + plot.Width * 0.56F ?
        marker_x - tooltip_width - 8.0F : marker_x + 8.0F;
    const float tooltip_x = std::clamp(desired_tooltip_x, card.X + 8.0F,
        card.GetRight() - tooltip_width - 8.0F);
    const float tooltip_y = 312.0F;
    FillRounded(graphics, R(tooltip_x, tooltip_y, tooltip_width, tooltip_height), 9.0F, palette.elevated);
    StrokeRounded(graphics, R(tooltip_x, tooltip_y, tooltip_width, tooltip_height), 9.0F, palette.border);
    Text(graphics, ChartDate(day, chinese), R(tooltip_x + 12.0F, tooltip_y + 7.0F, tooltip_width - 24.0F, 22.0F),
        10.5F, FontStyleBold, palette.text);
    for (std::size_t index = 0; index < series.size(); ++index) {
        const float y = tooltip_y + 34.0F + static_cast<float>(index) * 22.0F;
        SolidBrush dot(strokes[index]);
        graphics.FillEllipse(&dot, tooltip_x + 12.0F, y + 7.0F, 7.0F, 7.0F);
        Text(graphics, WideUtf8(series[index].model), R(tooltip_x + 25.0F, y, 112.0F, 20.0F),
            9.0F, FontStyleRegular, palette.text);
        std::wstring detail = std::to_wstring(series[index].usage[day]) + T(chinese, L" log events · ", L" 个日志事件 · ");
        const std::optional<double> cost = series[index].cost[day];
        detail += FormatSpendUsd(cost, series[index].partial[day]);
        Text(graphics, detail, R(tooltip_x + 136.0F, y, tooltip_width - 148.0F, 20.0F),
            8.5F, FontStyleRegular, series[index].partial[day] ? palette.yellow : palette.secondary, StringAlignmentFar);
    }
}

void DrawTopProjects(Graphics& graphics, const SpendSummary& spend, const Palette& palette, bool chinese,
    bool hide_identity) {
    const RectF card{214.0F, 482.0F, 454.0F, 174.0F};
    FillRounded(graphics, card, 12.0F, palette.surface);
    StrokeRounded(graphics, card, 12.0F, palette.border);
    Text(graphics, T(chinese, L"TOP PROJECTS · 30 DAYS", L"项目消耗 TOP 5 · 近 30 天"),
        R(232.0F, 492.0F, 260.0F, 22.0F), 10.0F, FontStyleBold, palette.muted);
    Text(graphics, T(chinese, L"Share of locally priced value", L"本地已计价金额占比"),
        R(486.0F, 492.0F, 164.0F, 22.0F), 8.5F, FontStyleRegular, palette.secondary, StringAlignmentFar);
    if (spend.top_projects.empty()) {
        Text(graphics, T(chinese, L"Projects appear after local session logs are scanned", L"扫描本地会话日志后显示项目排行"),
            R(232.0F, 526.0F, 418.0F, 100.0F), 10.0F, FontStyleRegular, palette.muted, StringAlignmentCenter);
        return;
    }
    for (std::size_t index = 0; index < spend.top_projects.size() && index < 5; ++index) {
        const ProjectUsageAmount& project = spend.top_projects[index];
        const float y = 518.0F + static_cast<float>(index) * 25.0F;
        std::wstring name = hide_identity ? T(chinese, L"Private project ", L"隐私项目 ") + std::to_wstring(index + 1) :
            WideUtf8(project.project);
        Text(graphics, std::to_wstring(index + 1) + L"  " + name, R(232.0F, y, 240.0F, 18.0F),
            9.0F, index == 0 ? FontStyleBold : FontStyleRegular, palette.text);
        const std::wstring value = FormatSpendUsd(std::optional<double>{project.cost_usd}, project.partial) + L"  ·  " +
            std::to_wstring(static_cast<int>(std::round(project.share_percent))) + L"%";
        Text(graphics, value, R(478.0F, y, 172.0F, 18.0F), 9.0F, FontStyleBold,
            project.partial ? palette.yellow : palette.secondary, StringAlignmentFar);
        FillRounded(graphics, R(232.0F, y + 18.0F, 418.0F, 3.0F), 1.5F, palette.border);
        const float width = 418.0F * static_cast<float>(std::clamp(project.share_percent, 0.0, 100.0) / 100.0);
        if (width > 0.0F) FillRounded(graphics, R(232.0F, y + 18.0F, std::max(4.0F, width), 3.0F), 1.5F,
            index == 0 ? palette.accent : Blend(palette.accent, palette.muted, static_cast<float>(index) * 0.16F));
    }
}

const wchar_t* T(bool chinese, const wchar_t* english, const wchar_t* simplified_chinese) {
    return chinese ? simplified_chinese : english;
}

std::wstring LocalHealthHeadline(UsageHealth health, bool chinese) {
    if (!chinese) return HealthHeadline(health);
    switch (health) {
    case UsageHealth::Loading: return L"正在检查 Codex 额度";
    case UsageHealth::Healthy: return L"额度充足，可以继续工作";
    case UsageHealth::Watch: return L"额度需要留意";
    case UsageHealth::Critical: return L"即将达到额度上限";
    case UsageHealth::Exhausted: return L"额度已经用尽";
    case UsageHealth::Unavailable: return L"暂时无法读取使用情况";
    }
    return L"Codex 使用情况";
}

std::wstring LocalReset(const RateWindow& window, bool chinese) {
    if (!chinese) return FormatReset(window);
    if (!window.resets_at) return L"重置时间不可用";
    const auto remaining = *window.resets_at - std::chrono::system_clock::now();
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(remaining).count();
    if (minutes <= 0) return L"正在重置";
    if (minutes < 60) return std::to_wstring(minutes) + L" 分钟后重置";
    const auto hours = minutes / 60;
    if (hours < 48) return std::to_wstring(hours) + L" 小时后重置";
    return std::to_wstring(hours / 24) + L" 天后重置";
}

std::wstring FloatBarReset(const std::optional<RateWindow>& window, bool chinese) {
    if (!window || !window->resets_at) return T(chinese, L"Reset time unknown", L"重置时间未知");
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(*window->resets_at);
    const std::time_t now_timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm reset_local{};
    std::tm now_local{};
    if (localtime_s(&reset_local, &timestamp) != 0 || localtime_s(&now_local, &now_timestamp) != 0) {
        return T(chinese, L"Reset time unknown", L"重置时间未知");
    }
    const bool today = reset_local.tm_year == now_local.tm_year && reset_local.tm_yday == now_local.tm_yday;
    const bool tomorrow = reset_local.tm_year == now_local.tm_year && reset_local.tm_yday == now_local.tm_yday + 1;
    std::wostringstream label;
    if (chinese) {
        if (today) label << L"今天 ";
        else if (tomorrow) label << L"明天 ";
        else label << reset_local.tm_mon + 1 << L"月" << reset_local.tm_mday << L"日 ";
        label << std::setfill(L'0') << std::setw(2) << reset_local.tm_hour << L":"
              << std::setw(2) << reset_local.tm_min << L" 重置";
    } else {
        label << (today ? L"Today" : tomorrow ? L"Tomorrow" :
            std::to_wstring(reset_local.tm_mon + 1) + L"/" + std::to_wstring(reset_local.tm_mday));
        label << L" " << std::setfill(L'0') << std::setw(2) << reset_local.tm_hour << L":"
              << std::setw(2) << reset_local.tm_min << L" reset";
    }
    return label.str();
}

std::wstring LocalPaceDetail(const PaceForecast& forecast, bool chinese) {
    const bool weekly = forecast.window_minutes >= 6 * 24 * 60 || forecast.window_title == L"Weekly";
    const std::wstring window = weekly ? T(chinese, L"Weekly capacity", L"每周额度") :
        T(chinese, L"Current session", L"当前周期");
    const auto minutes = std::max<std::int64_t>(1, forecast.until_exhaustion.count());
    std::wstring remaining;
    if (minutes < 60) remaining = std::to_wstring(minutes) + T(chinese, L" min", L" 分钟");
    else if (minutes < 48 * 60) remaining = std::to_wstring(std::max<std::int64_t>(1, minutes / 60)) + T(chinese, L" hr", L" 小时");
    else remaining = std::to_wstring(std::max<std::int64_t>(1, minutes / (24 * 60))) + T(chinese, L" days", L" 天");
    return chinese ? L"按当前速度，" + window + L"可能在约 " + remaining + L"后提前用尽。" :
        L"At this pace, " + window + L" may run out in about " + remaining + L", before reset.";
}

std::wstring LocalWindowTitle(const RateWindow& window, bool chinese) {
    if (!chinese) return window.title;
    return window.window_minutes >= 6 * 24 * 60 || window.title == L"Weekly" ? L"每周额度" : L"当前周期";
}

std::wstring LocalError(const std::wstring& error, bool chinese) {
    if (!chinese || error.empty()) return error;
    if (error.find(L"not signed in") != std::wstring::npos || error.find(L"No Codex token") != std::wstring::npos) {
        return L"Codex 尚未登录，请先运行 codex login。";
    }
    if (error.find(L"expired") != std::wstring::npos || error.find(L"unauthorized") != std::wstring::npos ||
        error.find(L"Unauthorized") != std::wstring::npos) {
        return L"Codex 登录状态已失效，请重新登录后刷新。";
    }
    if (error.find(L"no rate-limit") != std::wstring::npos) return L"Codex 没有返回可用的额度周期。";
    if (error.find(L"reach") != std::wstring::npos || error.find(L"network") != std::wstring::npos ||
        error.find(L"Network") != std::wstring::npos) {
        return L"暂时无法连接 Codex，请检查网络后重试。";
    }
    return L"暂时无法读取 Codex 使用情况，请稍后重试。";
}

SettingsAction TabAction(SettingsTab tab) {
    switch (tab) {
    case SettingsTab::General: return SettingsAction::SelectGeneral;
    case SettingsTab::Providers: return SettingsAction::SelectProviders;
    case SettingsTab::Notifications: return SettingsAction::SelectNotifications;
    case SettingsTab::FloatBar: return SettingsAction::SelectFloatBar;
    case SettingsTab::UsageSpend: return SettingsAction::SelectUsageSpend;
    case SettingsTab::About: return SettingsAction::SelectAbout;
    }
    return SettingsAction::None;
}

void DrawSidebarTab(Graphics& graphics, float y, const wchar_t* label, SettingsTab tab, SettingsTab active, const Palette& palette, SettingsAction hovered, SettingsAction pressed, float progress) {
    const SettingsAction action = TabAction(tab);
    const bool selected = tab == active;
    const bool hot = hovered == action;
    RectF rect = pressed == action ? R(33.0F, y + 1.0F, 146.0F, 40.0F) : R(32.0F, y, 148.0F, 42.0F);
    if (selected || hot) FillRounded(graphics, rect, 8.0F, selected ? palette.accent_soft : Blend(palette.surface, palette.accent_soft, progress * 0.7F));
    if (hot) StrokeRounded(graphics, rect, 8.0F, Blend(palette.border, palette.accent, progress * 0.65F));
    Text(graphics, label, R(48.0F, y, 120.0F, 42.0F), 12.0F, selected ? FontStyleBold : FontStyleRegular, selected ? palette.accent : palette.secondary);
}

void DrawSpendMetric(Graphics& graphics, float x, float y, float width, const wchar_t* label, const std::optional<double>& value, bool partial, const Palette& palette, bool chinese) {
    FillRounded(graphics, R(x, y, width, 98.0F), 11.0F, palette.surface);
    StrokeRounded(graphics, R(x, y, width, 98.0F), 11.0F, palette.border);
    Text(graphics, label, R(x + 14.0F, y + 12.0F, width - 28.0F, 22.0F), 10.0F, FontStyleBold, palette.muted);
    const std::wstring formatted = FormatSpendUsd(value, partial);
    float amount_size = 22.0F;
    FontFamily family(L"Segoe UI");
    RectF measured;
    while (amount_size > 13.0F) {
        Font font(&family, amount_size, FontStyleBold, UnitPixel);
        graphics.MeasureString(formatted.c_str(), static_cast<INT>(formatted.size()), &font, PointF{}, &measured);
        if (measured.Width <= width - 28.0F) break;
        amount_size -= 1.0F;
    }
    Text(graphics, formatted, R(x + 14.0F, y + 36.0F, width - 28.0F, 34.0F), amount_size, FontStyleBold, value ? palette.text : palette.muted);
    const wchar_t* caption = !value ? T(chinese, L"No priced logs", L"暂无可计价日志") :
        partial ? T(chinese, L"Known priced part", L"已知计价部分") : T(chinese, L"API equivalent", L"API 等价费用");
    Text(graphics, caption, R(x + 14.0F, y + 73.0F, width - 28.0F, 18.0F), 9.0F, FontStyleRegular, partial ? palette.yellow : palette.secondary);
}

void DrawSetupStep(Graphics& graphics, float y, const wchar_t* number, const wchar_t* title,
    const wchar_t* detail, const Palette& palette) {
    FillRounded(graphics, R(16.0F, y, 368.0F, 112.0F), 11.0F, palette.surface);
    StrokeRounded(graphics, R(16.0F, y, 368.0F, 112.0F), 11.0F, palette.border);
    FillRounded(graphics, R(30.0F, y + 18.0F, 34.0F, 34.0F), 9.0F, palette.accent_soft);
    Text(graphics, number, R(30.0F, y + 18.0F, 34.0F, 34.0F), 13.0F, FontStyleBold,
        palette.accent, StringAlignmentCenter);
    Text(graphics, title, R(78.0F, y + 14.0F, 278.0F, 28.0F), 14.0F, FontStyleBold, palette.text);
    TextWrapped(graphics, detail, R(78.0F, y + 46.0F, 278.0F, 50.0F), 10.0F,
        FontStyleRegular, palette.secondary);
}

}  // namespace

void PaintPopup(HWND window, HDC dc, const UsageSnapshot& snapshot, bool light, bool chinese, bool identity_hidden, RefreshPhase refresh_phase,
    CopySummaryState copy_state, ExternalActionFeedback external_feedback, PopupAction hovered, PopupAction pressed,
    float hover_progress, float refresh_angle, bool refresh_queued, GlobalShortcut global_shortcut, double ambient_phase,
    BackdropStyle backdrop) {
    const bool refreshing = RefreshIsActive(refresh_phase);
    const bool scanning_spend = refresh_phase == RefreshPhase::ScanningSpend;
    const PopupLayout layout = ResolvePopupLayout(snapshot);
    const float scale = Scale(window);
    Graphics graphics(dc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(backdrop == BackdropStyle::Solid ?
        TextRenderingHintClearTypeGridFit : TextRenderingHintAntiAliasGridFit);
    graphics.ScaleTransform(scale * kContentScale, scale * kContentScale);
    const Palette palette = Colors(light, backdrop);
    DrawWindowCanvas(graphics, static_cast<float>(kPopupWidth), static_cast<float>(kPopupHeight),
        palette, light, ambient_phase, 1.0F, backdrop);

    DrawPartnerMark(graphics, R(14.0F, 12.0F, 38.0F, 38.0F));
    Text(graphics, L"Codex Partner", R(60.0F, 10.0F, 180.0F, 26.0F), 15.0F, FontStyleBold, palette.text);
    const std::wstring header_context = identity_hidden ? T(chinese, L"Identity hidden", L"身份信息已隐藏") :
        snapshot.plan.empty() ? T(chinese, L"Native for Windows", L"Windows 原生版") : snapshot.plan;
    Text(graphics, header_context, R(60.0F, 31.0F, 190.0F, 18.0F), 10.0F, FontStyleRegular, palette.muted);
    DrawCopyButton(graphics, R(246.0F, 15.0F, 32.0F, 34.0F), palette, copy_state,
        hovered == PopupAction::CopySummary, pressed == PopupAction::CopySummary, hover_progress);
    DrawIconButton(graphics, R(284.0F, 15.0F, 32.0F, 34.0F), palette, false, hovered == PopupAction::Refresh, pressed == PopupAction::Refresh, hover_progress, refreshing ? refresh_angle : 0.0F);
    DrawIconButton(graphics, R(322.0F, 15.0F, 32.0F, 34.0F), palette, true, hovered == PopupAction::Settings, pressed == PopupAction::Settings, hover_progress, 0.0F);
    DrawCloseButton(graphics, R(360.0F, 15.0F, 32.0F, 34.0F), palette,
        hovered == PopupAction::Close, pressed == PopupAction::Close, hover_progress);

    const bool needs_setup = NeedsProviderSetup(snapshot);
    const UsagePrimaryTarget primary_target = ResolveUsagePrimaryTarget(snapshot);
    const auto now = std::chrono::system_clock::now();
    const auto pace = MostUrgentPaceForecast(snapshot, now);
    const UsageHealth health = DeriveHealth(snapshot, 70.0, 90.0, now);
    const Color health_color = needs_setup ? palette.accent :
        primary_target == UsagePrimaryTarget::RefreshUsage ? palette.yellow : HealthColor(health, palette);
    FillRounded(graphics, R(16.0F, 64.0F, 368.0F, 58.0F), 12.0F, palette.surface);
    SolidBrush health_brush(health_color);
    graphics.FillEllipse(&health_brush, 28.0F, 80.0F, 8.0F, 8.0F);
    const std::wstring headline = refresh_queued ?
        T(chinese, L"One more refresh is queued", L"已排队，将再刷新一次") :
        refresh_phase == RefreshPhase::FetchingUsage ? T(chinese, L"Refreshing live limits...", L"正在刷新实时额度…") :
        scanning_spend ? T(chinese, L"Limits ready · analyzing spend", L"额度已更新 · 正在分析费用") :
        needs_setup ? T(chinese, L"Connect Codex to see your headroom", L"连接 Codex，一眼掌握剩余额度") :
        snapshot.stale ? T(chinese, L"Showing last known usage", L"正在显示上次可用数据") : LocalHealthHeadline(health, chinese);
    Text(graphics, headline, R(44.0F, 67.0F, 320.0F, 25.0F), 13.0F, FontStyleBold, palette.text);
    std::wstring detail;
    if (refresh_queued) detail = T(chinese,
        L"Current work finishes first; one refresh follows.",
        L"当前读取完成后，会自动再刷新一次。");
    else if (refresh_phase == RefreshPhase::FetchingUsage) detail = T(chinese,
        L"Reading Codex limits and local activity in parallel.",
        L"正在并行读取 Codex 额度与本地活动。");
    else if (scanning_spend) detail = T(chinese,
        L"Quota is ready; local session logs are still being analyzed.",
        L"额度已经可用；本地会话日志仍在后台分析。");
    else if (needs_setup) detail = T(chinese, L"Sign in once; Codex Partner keeps the connection read-only and local.",
        L"只需登录一次；Codex Partner 始终只读访问并保留在本机。");
    else if (snapshot.stale) detail = T(chinese, L"Last successful values stay visible while Codex reconnects.", L"重新连接 Codex 时，仍保留上次成功读取的数值。");
    else if (!snapshot.error.empty()) detail = LocalError(snapshot.error, chinese);
    else if (snapshot.loading) detail = T(chinese, L"Reading your Codex limits securely.", L"正在安全读取 Codex 额度。");
    else if (pace) detail = LocalPaceDetail(*pace, chinese);
    else detail = chinese ? L"当前最高使用率为 " + Percent(MostConstrainedPercent(snapshot)) + L"。" : L"Your most constrained window is " + Percent(MostConstrainedPercent(snapshot)) + L" used.";
    Text(graphics, detail, R(44.0F, 91.0F, 320.0F, 20.0F), 9.5F, FontStyleRegular, palette.secondary);

    if (needs_setup) {
        DrawSetupStep(graphics, layout.first_card_y, L"1", T(chinese, L"Sign in with the Codex CLI", L"使用 Codex CLI 登录"),
            T(chinese, L"Open Providers below, then start the guided login in a terminal.",
                L"点击下方进入“提供商”，再在终端中启动登录引导。"), palette);
        DrawSetupStep(graphics, layout.second_card_y, L"2", T(chinese, L"Return and refresh", L"返回并刷新"),
            T(chinese, L"Your current and weekly limits will appear here immediately.",
                L"当前周期和每周额度随后会立即显示在这里。"), palette);
    } else {
        float card_y = layout.first_card_y;
        if (snapshot.session) {
            DrawRateCard(graphics, *snapshot.session, card_y, palette, chinese);
            card_y = layout.second_card_y;
        }
        if (snapshot.weekly) DrawRateCard(graphics, *snapshot.weekly, card_y, palette, chinese);
    }

    FillRounded(graphics, R(16.0F, layout.spend_y, 368.0F, 72.0F), 11.0F, palette.surface);
    StrokeRounded(graphics, R(16.0F, layout.spend_y, 368.0F, 72.0F), 11.0F, palette.border);
    const SpendSummary empty_spend;
    const SpendSummary& spend = snapshot.spend.value_or(empty_spend);
    std::wstring spend_title = scanning_spend ? T(chinese, L"UPDATING ESTIMATE…", L"正在更新费用估算…") :
        spend.stale ? T(chinese, L"API-EQUIVALENT ESTIMATE · CACHED", L"API 等价费用估算 · 上次数据") :
        T(chinese, L"API-EQUIVALENT ESTIMATE", L"API 等价费用估算");
    if (const auto coverage = SpendTokenCoveragePercent(spend); coverage && spend.partial) {
        spend_title += L" · " + std::to_wstring(*coverage) + T(chinese, L"% priced", L"% 已计价");
    }
    Text(graphics, spend_title, R(30.0F, layout.spend_y + 7.0F, 330.0F, 17.0F), 9.0F, FontStyleBold,
        scanning_spend || spend.stale || spend.partial ? palette.yellow : palette.muted);
    const std::array labels{T(chinese, L"1 day", L"近 1 天"), T(chinese, L"7 days", L"近 7 天"), T(chinese, L"30 days", L"近 30 天")};
    const std::array values{spend.one_day_usd, spend.seven_day_usd, spend.thirty_day_usd};
    const std::array partial_windows{spend.one_day_partial, spend.seven_day_partial, spend.thirty_day_partial};
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const float x = 30.0F + static_cast<float>(index) * 112.0F;
        Text(graphics, labels[index], R(x, layout.spend_y + 27.0F, 96.0F, 16.0F), 9.0F, FontStyleRegular, palette.secondary);
        Text(graphics, FormatSpendUsd(values[index], partial_windows[index]), R(x, layout.spend_y + 43.0F, 96.0F, 23.0F), 13.0F, FontStyleBold, values[index] ? palette.text : palette.muted);
    }

    RectF primary = R(16.0F, layout.primary_y, 368.0F, 42.0F);
    if (pressed == PopupAction::Primary) primary = R(17.0F, layout.primary_y + 1.0F, 366.0F, 40.0F);
    const bool primary_hovered = hovered == PopupAction::Primary;
    FillRounded(graphics, primary, 9.0F,
        Blend(palette.surface, palette.accent_soft, primary_hovered ? hover_progress * 0.55F : 0.28F));
    StrokeRounded(graphics, primary, 9.0F,
        Blend(palette.border, palette.accent, primary_hovered ? hover_progress * 0.65F : 0.18F));
    const wchar_t* primary_label = primary_target == UsagePrimaryTarget::ProviderSetup ?
        T(chinese, L"Set up Codex in Providers  >", L"前往“提供商”连接 Codex  >") :
        primary_target == UsagePrimaryTarget::RefreshUsage ?
            (refresh_queued ? T(chinese, L"Codex refresh queued…", L"Codex 刷新已排队…") :
            refreshing ? T(chinese, L"Refreshing Codex usage…", L"正在重新连接 Codex…") :
                T(chinese, L"Retry Codex refresh now  >", L"立即重试 Codex 刷新  >")) :
            T(chinese, L"Detailed analytics", L"详细分析");
    Text(graphics, primary_label,
        R(30.0F, layout.primary_y, 324.0F, 42.0F), 12.0F, FontStyleBold, palette.accent, StringAlignmentCenter);
    DrawCherryForeground(graphics, static_cast<float>(kPopupHeight),
        light, ambient_phase, BlossomSurface::Popup);
    if (backdrop != BackdropStyle::Solid) {
        StrokeRounded(graphics, R(0.5F, 0.5F, 399.0F,
            static_cast<float>(layout.content_height) - 1.0F), 14.0F, palette.border);
    }
    static_cast<void>(external_feedback);
    static_cast<void>(global_shortcut);
}

void PaintFloatBar(HWND window, HDC dc, const UsageSnapshot& snapshot, bool light, bool chinese,
    bool identity_hidden, RefreshPhase refresh_phase, FloatBarAction hovered, FloatBarAction pressed, float hover_progress,
    double ambient_phase, BackdropStyle backdrop) {
    const float scale = Scale(window);
    Graphics graphics(dc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(backdrop == BackdropStyle::Solid ?
        TextRenderingHintClearTypeGridFit : TextRenderingHintAntiAliasGridFit);
    graphics.ScaleTransform(scale * kContentScale, scale * kContentScale);
    const Palette palette = Colors(light, backdrop);
    const auto pace = MostUrgentPaceForecast(snapshot);
    DrawWindowCanvas(graphics, static_cast<float>(kFloatBarWidth), static_cast<float>(kFloatBarHeight),
        palette, light, ambient_phase, 5.0F, backdrop);

    if (hovered == FloatBarAction::OpenPopup) {
        FillRounded(graphics, R(3.0F, 3.0F, 378.0F, 70.0F), 12.0F,
            Blend(palette.background, palette.accent_soft, hover_progress * 0.45F));
    }
    if (pressed == FloatBarAction::OpenPopup) {
        StrokeRounded(graphics, R(4.0F, 4.0F, 376.0F, 68.0F), 11.0F, palette.accent);
    }

    DrawPartnerMark(graphics, R(10.0F, 16.0F, 42.0F, 42.0F));
    Text(graphics, L"Codex", R(59.0F, 13.0F, 70.0F, 24.0F), 13.0F, FontStyleBold, palette.text);
    const std::wstring state = refresh_phase == RefreshPhase::FetchingUsage ? T(chinese, L"Refreshing", L"正在刷新") :
        refresh_phase == RefreshPhase::ScanningSpend ? T(chinese, L"Spend scan", L"费用扫描") :
        NeedsProviderSetup(snapshot) ? T(chinese, L"Sign in", L"请登录") :
        snapshot.stale ? T(chinese, L"Last known", L"上次数据") :
        !snapshot.error.empty() ? T(chinese, L"Needs attention", L"需要处理") :
        pace ? T(chinese, L"Pace running hot", L"节奏偏快") :
        identity_hidden ? T(chinese, L"Privacy mode", L"隐私模式") :
        snapshot.plan.empty() ? T(chinese, L"Ready", L"已就绪") : snapshot.plan;
    Text(graphics, state, R(59.0F, 35.0F, 72.0F, 20.0F), 9.0F, FontStyleRegular,
        snapshot.error.empty() ? palette.muted : palette.yellow);

    Pen divider(palette.border, 1.0F);
    const bool has_session = snapshot.session.has_value();
    const bool has_weekly = snapshot.weekly.has_value();
    const bool two_metrics = has_session && has_weekly;
    if (has_session || has_weekly) {
        const float first_divider = two_metrics ? 138.0F : 172.0F;
        graphics.DrawLine(&divider, first_divider, 15.0F, first_divider, 61.0F);
    }
    if (two_metrics) graphics.DrawLine(&divider, 258.0F, 15.0F, 258.0F, 61.0F);

    const auto draw_metric = [&](float x, float width, const RateWindow& value) {
        const std::wstring title = LocalWindowTitle(value, chinese);
        const bool pace_risk = pace && value.window_minutes == pace->window_minutes &&
            value.title == pace->window_title;
        const Color value_color = value.used_percent >= 90.0 ? palette.red :
            value.used_percent >= 70.0 || pace_risk ? palette.yellow : palette.green;
        Text(graphics, title, R(x, 6.0F, width, 20.0F), 9.0F, FontStyleBold, palette.muted);
        Text(graphics, Percent(value.used_percent), R(x, 21.0F, width, 23.0F), 16.0F, FontStyleBold,
            value_color);
        Text(graphics, FloatBarReset(value, chinese), R(x, 43.0F, width, 15.0F), 7.5F,
            FontStyleRegular, value.resets_at ? palette.secondary : palette.muted);
        DrawProgress(graphics, R(x, 62.0F, width - 8.0F, 4.0F), value.used_percent, palette);
    };
    if (two_metrics) {
        draw_metric(152.0F, 100.0F, *snapshot.session);
        draw_metric(272.0F, 100.0F, *snapshot.weekly);
    } else if (snapshot.session) {
        draw_metric(194.0F, 174.0F, *snapshot.session);
    } else if (snapshot.weekly) {
        draw_metric(194.0F, 174.0F, *snapshot.weekly);
    }

    const float close_emphasis = hovered == FloatBarAction::Hide ? hover_progress : 0.0F;
    const RectF close_rect = pressed == FloatBarAction::Hide ? R(388.0F, 23.0F, 27.0F, 27.0F) : R(387.0F, 22.0F, 29.0F, 29.0F);
    FillRounded(graphics, close_rect, 8.0F, Blend(palette.elevated, palette.accent_soft, close_emphasis));
    StrokeRounded(graphics, close_rect, 8.0F, Blend(palette.border, palette.accent, close_emphasis * 0.6F));
    Text(graphics, L"×", close_rect, 15.0F, FontStyleRegular,
        hovered == FloatBarAction::Hide ? palette.accent : palette.secondary, StringAlignmentCenter);

    DrawCherryForeground(graphics, static_cast<float>(kFloatBarHeight),
        light, ambient_phase, BlossomSurface::FloatBar);
    StrokeRounded(graphics, R(0.5F, 0.5F, 419.0F, 75.0F), 13.0F, palette.border);
}

void PaintSettings(HWND window, HDC dc, const AppSettings& settings, const UsageSnapshot& snapshot,
    const UpdateCheckState& update, SettingsTab tab, bool light, bool chinese, SettingsPersistenceState persistence,
    bool diagnostics_copied, ExternalActionFeedback external_feedback, RefreshPhase refresh_phase, SettingsAction hovered, SettingsAction pressed,
    float hover_progress, GlobalShortcutStatus global_shortcut_status, std::optional<std::size_t> usage_chart_hover,
    float usage_chart_progress, double ambient_phase, BackdropStyle backdrop) {
    const float scale = Scale(window);
    Graphics graphics(dc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(backdrop == BackdropStyle::Solid ?
        TextRenderingHintClearTypeGridFit : TextRenderingHintAntiAliasGridFit);
    graphics.ScaleTransform(scale * kContentScale, scale * kContentScale);
    const Palette palette = Colors(light, backdrop);
    DrawWindowCanvas(graphics, static_cast<float>(kSettingsWidth), static_cast<float>(kSettingsHeight),
        palette, light, ambient_phase, 8.0F, backdrop);

    Text(graphics, T(chinese, L"Codex Partner Settings", L"Codex Partner 设置"), R(28.0F, 20.0F, 360.0F, 36.0F), 24.0F, FontStyleBold, palette.text);
    const bool external_active = external_feedback.outcome != ExternalActionOutcome::Idle;
    const bool shortcut_attention = global_shortcut_status == GlobalShortcutStatus::Unavailable ||
        global_shortcut_status == GlobalShortcutStatus::CandidateUnavailable;
    const std::wstring feedback = external_active ? ExternalActionStatus(external_feedback, chinese) :
        global_shortcut_status == GlobalShortcutStatus::CandidateUnavailable ?
            T(chinese, L"Shortcut in use · kept current", L"快捷键已占用 · 已保留当前设置") :
        global_shortcut_status == GlobalShortcutStatus::Unavailable ?
            T(chinese, L"Shortcut unavailable · choose another", L"快捷键不可用 · 请选择其他组合") :
        persistence == SettingsPersistenceState::Failed ?
        T(chinese, L"Save failed · changes restored", L"保存失败 · 已恢复原设置") :
        diagnostics_copied ? T(chinese, L"Diagnostics copied", L"诊断摘要已复制") :
        persistence == SettingsPersistenceState::Saved ? T(chinese, L"Saved", L"已保存") : L"";
    const Color feedback_color = external_active ?
        (ExternalActionFullySucceeded(external_feedback) ? palette.green :
            ExternalActionPartiallySucceeded(external_feedback) ? palette.yellow : palette.red) :
        shortcut_attention ? palette.red :
        persistence == SettingsPersistenceState::Failed ? palette.red :
        diagnostics_copied || persistence == SettingsPersistenceState::Saved ? palette.green : palette.muted;
    Text(graphics, feedback, R(408.0F, 25.0F, 230.0F, 28.0F), 11.0F, FontStyleRegular,
        feedback_color, StringAlignmentFar);
    DrawCloseButton(graphics, R(656.0F, 16.0F, 30.0F, 30.0F), palette,
        hovered == SettingsAction::Close, pressed == SettingsAction::Close, hover_progress);

    FillRounded(graphics, R(20.0F, 78.0F, 172.0F, 584.0F), 12.0F, palette.surface);
    StrokeRounded(graphics, R(20.0F, 78.0F, 172.0F, 584.0F), 12.0F, palette.border);
    DrawSidebarTab(graphics, 94.0F, T(chinese, L"General", L"通用"), SettingsTab::General, tab, palette, hovered, pressed, hover_progress);
    DrawSidebarTab(graphics, 140.0F, T(chinese, L"Providers", L"提供商"), SettingsTab::Providers, tab, palette, hovered, pressed, hover_progress);
    DrawSidebarTab(graphics, 186.0F, T(chinese, L"Notifications", L"通知"), SettingsTab::Notifications, tab, palette, hovered, pressed, hover_progress);
    DrawSidebarTab(graphics, 232.0F, T(chinese, L"Floating bar", L"浮动用量条"), SettingsTab::FloatBar, tab, palette, hovered, pressed, hover_progress);
    DrawSidebarTab(graphics, 278.0F, T(chinese, L"Usage & spend", L"使用与费用"), SettingsTab::UsageSpend, tab, palette, hovered, pressed, hover_progress);
    DrawSidebarTab(graphics, 324.0F, T(chinese, L"About", L"关于"), SettingsTab::About, tab, palette, hovered, pressed, hover_progress);
    Text(graphics, L"Codex Partner " CODEX_PARTNER_VERSION_WIDE, R(36.0F, 606.0F, 140.0F, 22.0F), 10.0F, FontStyleBold, palette.muted);

    if (tab == SettingsTab::General) {
        Text(graphics, T(chinese, L"GENERAL", L"通用"), R(218.0F, 76.0F, 220.0F, 26.0F), 10.0F, FontStyleBold, palette.muted);
        const wchar_t* language = settings.language == LanguageMode::SimplifiedChinese ? L"简体中文" : settings.language == LanguageMode::English ? L"English" : T(chinese, L"System", L"跟随系统");
        DrawSettingRow(graphics, 104.0F, T(chinese, L"Language", L"语言"), L"", language, palette, false, false, hovered == SettingsAction::CycleLanguage, pressed == SettingsAction::CycleLanguage, hover_progress);
        const wchar_t* theme = settings.theme == ThemeMode::Light ? L"Light" : settings.theme == ThemeMode::Dark ? L"Dark" : L"System";
        const wchar_t* localized_theme = chinese ? (settings.theme == ThemeMode::Light ? L"浅色" : settings.theme == ThemeMode::Dark ? L"深色" : L"跟随系统") : theme;
        DrawSettingRow(graphics, 172.0F, T(chinese, L"Theme", L"主题"), L"", localized_theme, palette, false, false, hovered == SettingsAction::CycleTheme, pressed == SettingsAction::CycleTheme, hover_progress);
        const std::wstring shortcut_description = global_shortcut_status == GlobalShortcutStatus::CandidateUnavailable ?
            T(chinese, L"That combination is in use; current shortcut kept", L"该组合已被占用；已保留当前快捷键") :
            global_shortcut_status == GlobalShortcutStatus::Unavailable ?
                T(chinese, L"Used by another app; choose a different shortcut", L"已被其他应用占用；请选择其他组合") :
            settings.global_shortcut == GlobalShortcut::Disabled ?
                T(chinese, L"Global quick peek is turned off", L"全局快速查看已关闭") :
                L"";
        const std::wstring shortcut_value = settings.global_shortcut == GlobalShortcut::Disabled && chinese ?
            L"关闭" : GlobalShortcutLabel(settings.global_shortcut);
        DrawSettingRow(graphics, 240.0F, T(chinese, L"Quick peek shortcut", L"快速查看快捷键"), shortcut_description,
            shortcut_value, palette, false, false, hovered == SettingsAction::ChooseGlobalShortcut,
            pressed == SettingsAction::ChooseGlobalShortcut, hover_progress, shortcut_attention);
        DrawSettingRow(graphics, 308.0F, T(chinese, L"Refresh interval", L"刷新间隔"), L"", std::to_wstring(settings.refresh_minutes) + T(chinese, L" min", L" 分钟"), palette, false, false, hovered == SettingsAction::CycleRefresh, pressed == SettingsAction::CycleRefresh, hover_progress);
        DrawSettingRow(graphics, 376.0F, T(chinese, L"Start at login", L"登录时启动"), L"", {}, palette, true, settings.start_at_login, hovered == SettingsAction::ToggleStartAtLogin, pressed == SettingsAction::ToggleStartAtLogin, hover_progress);
        DrawSettingRow(graphics, 444.0F, T(chinese, L"Start minimized", L"启动时最小化"), L"", {}, palette, true, settings.start_minimized, hovered == SettingsAction::ToggleStartMinimized, pressed == SettingsAction::ToggleStartMinimized, hover_progress);
        DrawSettingRow(graphics, 512.0F, T(chinese, L"Hide identity", L"隐藏身份信息"), L"", {}, palette, true, settings.hide_identity, hovered == SettingsAction::TogglePrivacy, pressed == SettingsAction::TogglePrivacy, hover_progress);
    } else if (tab == SettingsTab::Providers) {
        Text(graphics, T(chinese, L"PROVIDERS", L"提供商"), R(218.0F, 76.0F, 180.0F, 26.0F), 10.0F, FontStyleBold, palette.muted);
        FillRounded(graphics, R(214.0F, 104.0F, 454.0F, 128.0F), 12.0F, palette.surface);
        StrokeRounded(graphics, R(214.0F, 104.0F, 454.0F, 128.0F), 12.0F, palette.border);
        DrawPartnerMark(graphics, R(228.0F, 120.0F, 46.0F, 46.0F));
        Text(graphics, L"Codex", R(286.0F, 116.0F, 190.0F, 28.0F), 17.0F, FontStyleBold, palette.text);
        const bool needs_login = NeedsProviderSetup(snapshot);
        const wchar_t* connection = needs_login ? T(chinese, L"Sign-in required", L"需要登录") :
            snapshot.connection == ProviderConnectionState::CredentialsDetected ?
                (snapshot.error.empty() ? T(chinese, L"Credentials detected and connected", L"已检测凭据并连接") :
                    T(chinese, L"Credentials detected · refresh needs attention", L"已检测凭据 · 刷新需要处理")) :
                T(chinese, L"Checking local Codex credentials", L"正在检查本地 Codex 凭据");
        Text(graphics, connection, R(286.0F, 144.0F, 348.0F, 20.0F), 10.0F, FontStyleRegular,
            needs_login || !snapshot.error.empty() ? palette.yellow : palette.green);
        Text(graphics, snapshot.plan.empty() ? T(chinese, L"Plan unavailable", L"套餐信息不可用") : snapshot.plan, R(230.0F, 182.0F, 220.0F, 22.0F), 11.0F, FontStyleRegular, palette.secondary);
        if (snapshot.credits) Text(graphics, FormatUsd(snapshot.credits) + T(chinese, L" credits", L" 额度余额"), R(480.0F, 182.0F, 164.0F, 22.0F), 11.0F, FontStyleBold, palette.secondary, StringAlignmentFar);
        Text(graphics, T(chinese, L"CREDENTIAL PATH", L"凭据位置"), R(218.0F, 252.0F, 180.0F, 26.0F), 10.0F, FontStyleBold, palette.muted);
        FillRounded(graphics, R(214.0F, 280.0F, 454.0F, 108.0F), 11.0F, palette.surface);
        StrokeRounded(graphics, R(214.0F, 280.0F, 454.0F, 108.0F), 11.0F, palette.border);
        Text(graphics, T(chinese, L"Local Codex credentials", L"本地 Codex 凭据"), R(232.0F, 296.0F, 320.0F, 25.0F), 14.0F, FontStyleBold, palette.text);
        TextWrapped(graphics, T(chinese, L"CODEX_HOME\\auth.json, or %USERPROFILE%\\.codex\\auth.json. Access is read-only.",
            L"CODEX_HOME\\auth.json 或 %USERPROFILE%\\.codex\\auth.json"),
            R(232.0F, 326.0F, 410.0F, 42.0F), 10.0F, FontStyleRegular, palette.secondary);
    } else if (tab == SettingsTab::Notifications) {
        Text(graphics, T(chinese, L"NOTIFICATIONS", L"通知"), R(218.0F, 76.0F, 180.0F, 26.0F), 10.0F, FontStyleBold, palette.muted);
        DrawSettingRow(graphics, 104.0F, T(chinese, L"Usage alerts", L"额度提醒"), L"", {}, palette, true, settings.usage_notifications, hovered == SettingsAction::ToggleUsageNotifications, pressed == SettingsAction::ToggleUsageNotifications, hover_progress);
        DrawSettingRow(graphics, 176.0F, T(chinese, L"Warning threshold", L"预警阈值"), L"", std::to_wstring(settings.usage_warning_percent) + L"%", palette, false, false, hovered == SettingsAction::ChooseUsageWarning, pressed == SettingsAction::ChooseUsageWarning, hover_progress);
        DrawSettingRow(graphics, 248.0F, T(chinese, L"Pause alerts", L"暂停提醒"),
            L"",
            settings.usage_notifications ?
                FormatNotificationSnoozeCompact(settings.notification_snoozed_until, chinese) :
                T(chinese, L"Off", L"关闭"), palette, false, false,
            hovered == SettingsAction::ChooseNotificationSnooze,
            pressed == SettingsAction::ChooseNotificationSnooze, hover_progress);
        DrawWideAction(graphics, 340.0F, T(chinese, L"Send a test notification", L"发送测试通知"), palette, hovered == SettingsAction::TestNotification, pressed == SettingsAction::TestNotification, hover_progress);
    } else if (tab == SettingsTab::FloatBar) {
        Text(graphics, T(chinese, L"FLOATING USAGE BAR", L"浮动用量条"), R(218.0F, 76.0F, 240.0F, 26.0F), 10.0F, FontStyleBold, palette.muted);
        DrawSettingRow(graphics, 104.0F, T(chinese, L"Show floating bar", L"显示浮动用量条"),
            L"", {}, palette, true,
            settings.show_float_bar, hovered == SettingsAction::ToggleFloatBar, pressed == SettingsAction::ToggleFloatBar, hover_progress);
        DrawWideAction(graphics, 196.0F, T(chinese, L"Reset position to the top right", L"将位置重置到右上角"), palette,
            hovered == SettingsAction::ResetFloatBarPosition, pressed == SettingsAction::ResetFloatBarPosition, hover_progress);
    } else if (tab == SettingsTab::UsageSpend) {
        Text(graphics, T(chinese, L"USAGE & SPEND", L"使用与费用"), R(218.0F, 76.0F, 220.0F, 26.0F), 10.0F, FontStyleBold, palette.muted);
        Text(graphics, T(chinese, L"Known API-equivalent value — not a subscription bill", L"已知 API 等价价值，并非订阅账单"), R(218.0F, 98.0F, 440.0F, 28.0F), 15.0F, FontStyleBold, palette.text);
        const std::wstring plan_context = refresh_phase == RefreshPhase::FetchingUsage ?
            T(chinese, L"Refreshing limits and local spend...", L"正在刷新额度与本地费用…") :
            refresh_phase == RefreshPhase::ScanningSpend ?
                T(chinese, L"Updating from local logs · current totals stay visible", L"正在读取本地日志 · 当前估算继续显示") :
            snapshot.plan.empty() ? T(chinese, L"Subscription plan unknown \u00b7 estimate only", L"订阅套餐未知 \u00b7 仅为估算") :
                snapshot.plan + T(chinese, L" \u00b7 included subscription usage", L" \u00b7 订阅内用量");
        Text(graphics, plan_context, R(218.0F, 120.0F, 440.0F, 18.0F), 9.0F, FontStyleRegular,
            RefreshIsActive(refresh_phase) ? palette.yellow : palette.muted);
        const SpendSummary empty;
        const SpendSummary& spend = snapshot.spend.value_or(empty);
        DrawSpendMetric(graphics, 214.0F, 142.0F, 142.0F, T(chinese, L"LAST 1 DAY", L"近 1 天"), spend.one_day_usd, spend.one_day_partial, palette, chinese);
        DrawSpendMetric(graphics, 370.0F, 142.0F, 142.0F, T(chinese, L"LAST 7 DAYS", L"近 7 天"), spend.seven_day_usd, spend.seven_day_partial, palette, chinese);
        DrawSpendMetric(graphics, 526.0F, 142.0F, 142.0F, T(chinese, L"LAST 30 DAYS", L"近 30 天"), spend.thirty_day_usd, spend.thirty_day_partial, palette, chinese);
        DrawUsageWaveChart(graphics, spend, palette, light, chinese, usage_chart_hover, usage_chart_progress);
        DrawTopProjects(graphics, spend, palette, chinese, settings.hide_identity);
        std::wstring coverage;
        if (const auto event_percent = SpendPricingCoveragePercent(spend)) {
            coverage = std::to_wstring(*event_percent) + T(chinese, L"% of records priced", L"% 记录已计价");
        } else {
            coverage = T(chinese, L"Pricing coverage unavailable", L"计价覆盖率不可用");
        }
        if (spend.unpriced_events > 0) coverage += L"  \u00b7  " + CompactCount(spend.unpriced_events) + T(chinese, L" unpriced", L" 个未计价");
        Text(graphics, coverage, R(220.0F, 660.0F, 438.0F, 18.0F), 8.5F, FontStyleRegular,
            spend.partial ? palette.yellow : palette.muted, StringAlignmentCenter);
    } else {
        Text(graphics, T(chinese, L"ABOUT", L"关于"), R(218.0F, 76.0F, 180.0F, 26.0F), 10.0F, FontStyleBold, palette.muted);
        FillRounded(graphics, R(214.0F, 104.0F, 454.0F, 188.0F), 12.0F, palette.surface);
        StrokeRounded(graphics, R(214.0F, 104.0F, 454.0F, 188.0F), 12.0F, palette.border);
        Text(graphics, L"Codex Partner", R(236.0F, 126.0F, 300.0F, 34.0F), 22.0F, FontStyleBold, palette.text);
        Text(graphics, T(chinese, L"Version " CODEX_PARTNER_VERSION_WIDE, L"版本 " CODEX_PARTNER_VERSION_WIDE), R(236.0F, 168.0F, 330.0F, 24.0F), 11.0F, FontStyleRegular, palette.secondary);

        FillRounded(graphics, R(214.0F, 310.0F, 454.0F, 146.0F), 12.0F, palette.surface);
        StrokeRounded(graphics, R(214.0F, 310.0F, 454.0F, 146.0F), 12.0F, palette.border);
        const wchar_t* update_title = T(chinese, L"Updates", L"软件更新");
        std::wstring update_detail = T(chinese,
            L"Check GitHub Releases for a newer stable version.",
            L"从 GitHub Releases 检查新的稳定版本。");
        std::wstring update_button = T(chinese, L"Check for updates", L"检查更新");
        Color update_color = palette.accent;
        switch (update.status) {
        case UpdateCheckStatus::Idle: break;
        case UpdateCheckStatus::Checking:
            update_title = T(chinese, L"Checking GitHub Releases…", L"正在检查 GitHub Releases…");
            update_detail = T(chinese, L"This read-only check runs in the background.", L"只读检查正在后台进行。");
            update_button = T(chinese, L"Checking…", L"检查中…");
            update_color = palette.yellow;
            break;
        case UpdateCheckStatus::UpToDate:
            update_title = T(chinese, L"You’re up to date", L"已是最新版");
            update_detail = update.latest_version.empty() ? CODEX_PARTNER_VERSION_WIDE : update.latest_version;
            update_button = T(chinese, L"Check again", L"再次检查");
            update_color = palette.green;
            break;
        case UpdateCheckStatus::Available:
            update_title = T(chinese, L"A new version is available", L"发现新版本");
            if (ResolveUpdateNavigation(update).kind == UpdateNavigationKind::NativeDownload) {
                update_detail = update.latest_version + T(chinese,
                    L" · One-click Native EXE ready on GitHub.",
                    L" · GitHub 已提供一键 Native EXE。");
                update_button = T(chinese, L"Download Native EXE", L"下载 Native EXE");
            } else {
                update_detail = update.latest_version + T(chinese,
                    L" · Review the release and choose a build.",
                    L" · 查看版本详情并选择构建。");
                update_button = T(chinese, L"View release", L"查看版本");
            }
            update_color = palette.green;
            break;
        case UpdateCheckStatus::Failed:
            update_title = T(chinese, L"Couldn’t check for updates", L"无法检查更新");
            update_detail = T(chinese, L"No data was changed. Try again when online.", L"没有更改任何数据，请联网后重试。");
            update_button = T(chinese, L"Try again", L"重试");
            update_color = palette.red;
            break;
        }
        SolidBrush update_dot(update_color);
        graphics.FillEllipse(&update_dot, 236.0F, 330.0F, 8.0F, 8.0F);
        Text(graphics, update_title, R(254.0F, 320.0F, 386.0F, 28.0F), 14.0F, FontStyleBold, palette.text);
        Text(graphics, update_detail, R(236.0F, 350.0F, 404.0F, 26.0F), 10.0F, FontStyleRegular, palette.secondary);
        DrawWideAction(graphics, 398.0F, update_button, palette,
            hovered == SettingsAction::CheckForUpdates && update.status != UpdateCheckStatus::Checking,
            pressed == SettingsAction::CheckForUpdates && update.status != UpdateCheckStatus::Checking,
            hover_progress);
    }

    if (tab == SettingsTab::About) {
        DrawPrimaryWideAction(graphics, 454.0F, T(chinese, L"Report a problem", L"报告问题"), palette,
            hovered == SettingsAction::ReportIssue, pressed == SettingsAction::ReportIssue, hover_progress);
        const std::array actions{SettingsAction::OpenProjectSite, SettingsAction::CopyDiagnostics};
        const std::array labels{T(chinese, L"View on GitHub", L"查看 GitHub"), T(chinese, L"Copy diagnostics", L"复制诊断摘要")};
        for (std::size_t index = 0; index < actions.size(); ++index) {
            const float x = 214.0F + static_cast<float>(index) * 234.0F;
            const bool action_hovered = hovered == actions[index];
            RectF button = pressed == actions[index] ? R(x + 1.0F, 511.0F, 219.0F, 36.0F) : R(x, 510.0F, 221.0F, 38.0F);
            FillRounded(graphics, button, 9.0F, Blend(palette.accent_soft, palette.accent, action_hovered ? hover_progress * 0.18F : 0.0F));
            StrokeRounded(graphics, button, 9.0F, Blend(palette.accent_soft, palette.accent, action_hovered ? hover_progress * 0.6F : 0.0F));
            Text(graphics, labels[index], R(x + 8.0F, 510.0F, 205.0F, 38.0F), 10.0F, FontStyleBold, palette.accent, StringAlignmentCenter);
        }
    } else if (tab == SettingsTab::Providers) {
        const std::array actions{SettingsAction::LaunchCodexLogin, SettingsAction::OpenCodexFolder};
        const std::array labels{
            NeedsProviderSetup(snapshot) ? T(chinese, L"Open Codex login", L"打开 Codex 登录") :
                T(chinese, L"Re-authenticate", L"重新登录"),
            T(chinese, L"Open configuration", L"打开配置目录")};
        for (std::size_t index = 0; index < actions.size(); ++index) {
            const float x = 214.0F + static_cast<float>(index) * 234.0F;
            const bool action_hovered = hovered == actions[index];
            RectF button = pressed == actions[index] ? R(x + 1.0F, 487.0F, 219.0F, 40.0F) : R(x, 486.0F, 221.0F, 42.0F);
            FillRounded(graphics, button, 9.0F, Blend(palette.accent_soft, palette.accent, action_hovered ? hover_progress * 0.18F : 0.0F));
            StrokeRounded(graphics, button, 9.0F, Blend(palette.accent_soft, palette.accent, action_hovered ? hover_progress * 0.6F : 0.0F));
            Text(graphics, labels[index], R(x + 8.0F, 486.0F, 205.0F, 42.0F), 11.0F, FontStyleBold, palette.accent, StringAlignmentCenter);
        }
    }
    if (hovered != SettingsAction::None) {
        Text(graphics, SettingsActionHint(hovered, chinese), R(220.0F, 700.0F, 440.0F, 18.0F), 9.0F,
            FontStyleRegular, palette.accent, StringAlignmentCenter);
    }
    DrawCherryForeground(graphics, static_cast<float>(kSettingsHeight),
        light, ambient_phase, BlossomSurface::Settings);
    if (backdrop != BackdropStyle::Solid) {
        StrokeRounded(graphics, R(0.5F, 0.5F, 699.0F, 719.0F), 14.0F, palette.border);
    }
}

PopupAction HitTestPopup(POINT point, float primary_y) noexcept {
    if (point.y >= 12 && point.y <= 52 && point.x >= 242 && point.x < 282) return PopupAction::CopySummary;
    if (point.y >= 12 && point.y <= 52 && point.x >= 282 && point.x < 320) return PopupAction::Refresh;
    if (point.y >= 12 && point.y <= 52 && point.x >= 320 && point.x < 358) return PopupAction::Settings;
    if (point.y >= 12 && point.y <= 52 && point.x >= 358) return PopupAction::Close;
    if (static_cast<float>(point.y) >= primary_y - 6.0F && static_cast<float>(point.y) <= primary_y + 48.0F) return PopupAction::Primary;
    return PopupAction::None;
}

SettingsAction HitTestSettings(POINT point, SettingsTab tab) noexcept {
    if (point.x >= 648 && point.y >= 8 && point.y <= 54) return SettingsAction::Close;
    if (point.x >= 28 && point.x <= 184) {
        if (point.y >= 90 && point.y < 138) return SettingsAction::SelectGeneral;
        if (point.y >= 138 && point.y < 184) return SettingsAction::SelectProviders;
        if (point.y >= 184 && point.y < 230) return SettingsAction::SelectNotifications;
        if (point.y >= 230 && point.y < 276) return SettingsAction::SelectFloatBar;
        if (point.y >= 276 && point.y < 322) return SettingsAction::SelectUsageSpend;
        if (point.y >= 322 && point.y < 370) return SettingsAction::SelectAbout;
    }
    if (point.x < 208 || point.x > 680) return SettingsAction::None;
    if (tab == SettingsTab::General) {
        if (point.y >= 104 && point.y < 168) return SettingsAction::CycleLanguage;
        if (point.y >= 172 && point.y < 236) return SettingsAction::CycleTheme;
        if (point.y >= 240 && point.y < 304) return SettingsAction::ChooseGlobalShortcut;
        if (point.y >= 308 && point.y < 372) return SettingsAction::CycleRefresh;
        if (point.y >= 376 && point.y < 440) return SettingsAction::ToggleStartAtLogin;
        if (point.y >= 444 && point.y < 508) return SettingsAction::ToggleStartMinimized;
        if (point.y >= 512 && point.y < 576) return SettingsAction::TogglePrivacy;
    } else if (tab == SettingsTab::Notifications) {
        if (point.y >= 96 && point.y < 176) return SettingsAction::ToggleUsageNotifications;
        if (point.y >= 176 && point.y < 248) return SettingsAction::ChooseUsageWarning;
        if (point.y >= 248 && point.y < 320) return SettingsAction::ChooseNotificationSnooze;
        if (point.y >= 332 && point.y < 392) return SettingsAction::TestNotification;
    } else if (tab == SettingsTab::FloatBar) {
        if (point.y >= 96 && point.y < 176) return SettingsAction::ToggleFloatBar;
        if (point.y >= 188 && point.y < 248) return SettingsAction::ResetFloatBarPosition;
    } else if (tab == SettingsTab::About) {
        if (point.y >= 390 && point.y < 448) return SettingsAction::CheckForUpdates;
        if (point.y >= 448 && point.y < 506) return SettingsAction::ReportIssue;
        if (point.y >= 506 && point.y < 558) {
            if (point.x >= 214 && point.x <= 435) return SettingsAction::OpenProjectSite;
            if (point.x >= 448 && point.x <= 669) return SettingsAction::CopyDiagnostics;
        }
        return SettingsAction::None;
    } else if (tab == SettingsTab::Providers && point.y >= 478 && point.y < 538) {
        if (point.x >= 214 && point.x <= 435) return SettingsAction::LaunchCodexLogin;
        if (point.x >= 448 && point.x <= 669) return SettingsAction::OpenCodexFolder;
        return SettingsAction::None;
    }
    return SettingsAction::None;
}

std::optional<std::size_t> HitTestUsageChart(POINT point) noexcept {
    constexpr float left = 232.0F;
    constexpr float right = 650.0F;
    if (point.x < static_cast<LONG>(left) || point.x > static_cast<LONG>(right) || point.y < 312 || point.y > 454) {
        return std::nullopt;
    }
    const float ratio = std::clamp((static_cast<float>(point.x) - left) / (right - left), 0.0F, 1.0F);
    return static_cast<std::size_t>(std::lround(ratio * 29.0F));
}

FloatBarAction HitTestFloatBar(POINT point) noexcept {
    if (point.x >= 382 && point.x < kFloatBarWidth && point.y >= 0 && point.y < kFloatBarHeight) return FloatBarAction::Hide;
    if (point.x >= 0 && point.x < 382 && point.y >= 0 && point.y < kFloatBarHeight) return FloatBarAction::OpenPopup;
    return FloatBarAction::None;
}

const wchar_t* PopupActionHint(PopupAction action, bool chinese, UsagePrimaryTarget primary_target,
    bool refreshing, bool refresh_queued) noexcept {
    switch (action) {
    case PopupAction::CopySummary: return T(chinese, L"Copy a private usage summary (Ctrl+C)", L"复制隐私安全的使用摘要（Ctrl+C）");
    case PopupAction::Refresh:
        if (refresh_queued) return T(chinese, L"One more refresh is already queued", L"已排队等待再刷新一次");
        if (refreshing) return T(chinese, L"Queue one more refresh after this one", L"当前完成后再刷新一次");
        return T(chinese, L"Refresh Codex usage now", L"立即刷新 Codex 使用情况");
    case PopupAction::Settings: return T(chinese, L"Open Codex Partner settings", L"打开 Codex Partner 设置");
    case PopupAction::Close: return T(chinese, L"Close this window", L"关闭此窗口");
    case PopupAction::Primary:
        if (primary_target == UsagePrimaryTarget::ProviderSetup) {
            return T(chinese, L"Open Providers and finish Codex sign-in", L"打开“提供商”并完成 Codex 登录");
        }
        if (primary_target == UsagePrimaryTarget::RefreshUsage) {
            return refresh_queued ? T(chinese, L"A Codex refresh is queued", L"Codex 刷新已排队") :
                refreshing ? T(chinese, L"Codex refresh is already in progress", L"Codex 刷新正在进行中") :
                T(chinese, L"Retry the Codex connection without leaving this panel", L"不离开面板，立即重试 Codex 连接");
        }
        return T(chinese, L"Open model trends and project spend inside Codex Partner", L"在 Codex Partner 内查看模型趋势与项目费用");
    case PopupAction::None: return L"";
    }
    return L"";
}

const wchar_t* FloatBarActionHint(FloatBarAction action, bool chinese) noexcept {
    switch (action) {
    case FloatBarAction::OpenPopup: return T(chinese, L"Open the full Codex Partner usage panel", L"打开完整 Codex Partner 使用面板");
    case FloatBarAction::Hide: return T(chinese, L"Hide the floating usage bar", L"隐藏浮动用量条");
    case FloatBarAction::None: return L"";
    }
    return L"";
}

const wchar_t* SettingsActionHint(SettingsAction action, bool chinese) noexcept {
    switch (action) {
    case SettingsAction::SelectGeneral: return T(chinese, L"Open general preferences", L"打开通用偏好设置");
    case SettingsAction::SelectProviders: return T(chinese, L"Inspect connected providers and data sources", L"查看已连接的提供商和数据来源");
    case SettingsAction::SelectNotifications: return T(chinese, L"Review notification behaviour", L"查看通知行为");
    case SettingsAction::SelectFloatBar: return T(chinese, L"Configure the glanceable floating usage bar", L"配置一眼可见的浮动用量条");
    case SettingsAction::SelectUsageSpend: return T(chinese, L"Show 1, 7, and 30 day USD estimates", L"查看近 1、7、30 天的 USD 估算");
    case SettingsAction::SelectAbout: return T(chinese, L"About Codex Partner", L"关于 Codex Partner");
    case SettingsAction::CycleLanguage: return T(chinese, L"Choose System, Simplified Chinese, or English", L"选择跟随系统、简体中文或 English");
    case SettingsAction::CycleTheme: return T(chinese, L"Choose System, Light, or Dark", L"选择跟随系统、浅色或深色主题");
    case SettingsAction::ChooseGlobalShortcut: return T(chinese, L"Choose a system-wide quick peek shortcut or turn it off", L"选择全局快速查看快捷键或将其关闭");
    case SettingsAction::CycleRefresh: return T(chinese, L"Choose a 5, 15, or 30 minute refresh interval", L"选择 5、15 或 30 分钟刷新间隔");
    case SettingsAction::ToggleStartAtLogin: return T(chinese, L"Toggle launch with your Windows account", L"切换是否随 Windows 用户登录启动");
    case SettingsAction::ToggleStartMinimized: return T(chinese, L"Choose whether startup stays quietly in the tray", L"选择启动后是否安静地留在托盘");
    case SettingsAction::TogglePrivacy: return T(chinese, L"Hide plan details across quick surfaces and copied summaries", L"在快捷界面和复制摘要中隐藏套餐信息");
    case SettingsAction::ToggleFloatBar: return T(chinese, L"Show or hide the floating usage bar", L"显示或隐藏浮动用量条");
    case SettingsAction::ResetFloatBarPosition: return T(chinese, L"Move the floating bar back to the top right", L"将浮动用量条移回右上角");
    case SettingsAction::ToggleUsageNotifications: return T(chinese, L"Enable or disable threshold-crossing alerts", L"开启或关闭跨阈值额度提醒");
    case SettingsAction::ChooseUsageWarning: return T(chinese, L"Choose a 70%, 80%, or 90% warning threshold", L"选择 70%、80% 或 90% 的预警阈值");
    case SettingsAction::ChooseNotificationSnooze: return T(chinese, L"Pause alerts for 1, 4, or 24 hours, or resume now", L"暂停提醒 1、4 或 24 小时，或立即恢复");
    case SettingsAction::TestNotification: return T(chinese, L"Send one harmless test through the notification area", L"通过通知区域发送一次无害的测试提醒");
    case SettingsAction::LaunchCodexLogin: return T(chinese, L"Open a terminal and run the official Codex login flow", L"打开终端并运行官方 Codex 登录流程");
    case SettingsAction::OpenCodexFolder: return T(chinese, L"Open the local Codex configuration folder", L"打开本地 Codex 配置文件夹");
    case SettingsAction::CheckForUpdates: return T(chinese, L"Check for updates, then open the exact Native download when available", L"检查更新，并在可用时打开精确的 Native 下载");
    case SettingsAction::ReportIssue: return T(chinese, L"Copy private diagnostics and open a new GitHub issue", L"复制脱敏诊断并打开新的 GitHub 问题表单");
    case SettingsAction::OpenProjectSite: return T(chinese, L"Open Codex Partner on GitHub", L"在 GitHub 打开 Codex Partner 项目");
    case SettingsAction::CopyDiagnostics: return T(chinese, L"Copy a credential-free issue report summary", L"复制不含凭据的问题诊断摘要");
    case SettingsAction::Close: return T(chinese, L"Close settings", L"关闭设置");
    case SettingsAction::None: return L"";
    }
    return L"";
}

}  // namespace codex_partner::ui
