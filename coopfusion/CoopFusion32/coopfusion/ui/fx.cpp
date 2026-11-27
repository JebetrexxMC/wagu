#include "fx.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

#include "imgui_internal.h"
#include "ui_common.h"

namespace UI {
namespace Fx {
namespace {

constexpr float kPi = 3.14159265f;

ImU32 Col(float r, float g, float b, float a) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}
ImU32 Col(const ImVec4& c, float aMul = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * aMul));
}
ImVec4 Mul(const ImVec4& c, float k, float a) {
    return ImVec4(std::min(1.0f, c.x * k), std::min(1.0f, c.y * k), std::min(1.0f, c.z * k), a);
}
ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
ImU32 LerpCol(ImU32 a, ImU32 b, float t) {
    return ImGui::ColorConvertFloat4ToU32(
        Mix(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), t));
}

float Hash01(int n) {
    unsigned int x = static_cast<unsigned int>(n) * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return static_cast<float>(x & 0xFFFFFFu) / 16777216.0f;
}

struct Palette {
    ThemeInfo info;
    ImVec4 textDim, border, separator;
    ImVec4 btn, btnHover, btnActive;
    ImVec4 frame, frameHover, frameActive;
    ImVec4 wave[3];
    ImVec4 launchL, launchR;
    ImVec4 title, good, warn, idle;
    ImVec4 panelBorder, hilite;
    float shadowA;
    float snowA;
};

Palette MakeDark(const char* name, ImVec4 accent) {
    Palette p{};
    const ImVec4 black(0.03f, 0.03f, 0.035f, 1.0f);
    p.info.name = name;
    p.info.accent = accent;
    p.info.light = false;
    p.info.bgTop = ImVec4(0.035f, 0.035f, 0.04f, 0.97f);
    p.info.bgBottom = ImVec4(0.03f + accent.x * 0.16f, 0.03f + accent.y * 0.16f,
                             0.03f + accent.z * 0.16f, 0.97f);
    p.info.text = ImVec4(0.96f, 0.96f, 0.97f, 1.0f);
    p.textDim = ImVec4(0.64f, 0.65f, 0.70f, 1.0f);
    p.border = Mul(accent, 1.0f, 0.75f);
    p.separator = Mul(accent, 1.0f, 0.35f);
    p.btn = Mul(accent, 0.38f, 0.90f);
    p.btnHover = Mul(accent, 0.55f, 0.95f);
    p.btnActive = Mul(accent, 0.85f, 1.0f);
    p.frame = ImVec4(accent.x * 0.12f, accent.y * 0.12f, accent.z * 0.12f, 0.85f);
    p.frameHover = ImVec4(accent.x * 0.20f, accent.y * 0.20f, accent.z * 0.20f, 0.90f);
    p.frameActive = ImVec4(accent.x * 0.30f, accent.y * 0.30f, accent.z * 0.30f, 1.0f);
    p.wave[0] = Mul(accent, 1.00f, 0.13f);
    p.wave[1] = Mul(accent, 0.80f, 0.11f);
    p.wave[2] = Mul(accent, 1.00f, 0.14f);
    p.launchL = Mix(black, accent, 0.22f);
    p.launchR = accent;
    p.title = Mix(accent, ImVec4(1, 1, 1, 1), 0.25f);
    p.good = ImVec4(0.35f, 0.95f, 0.55f, 1.0f);
    p.warn = ImVec4(1.0f, 0.80f, 0.30f, 1.0f);
    p.idle = ImVec4(0.76f, 0.78f, 0.85f, 1.0f);
    p.panelBorder = Mul(accent, 1.0f, 0.85f);
    p.hilite = ImVec4(1, 1, 1, 0.07f);
    p.shadowA = 0.25f;
    p.snowA = 0.75f;
    return p;
}

Palette MakeFrost() {
    Palette p{};
    p.info.name = "Frost (light)";
    p.info.accent = ImVec4(0.30f, 0.62f, 1.0f, 1.0f);
    p.info.light = true;
    p.info.bgTop = ImVec4(0.93f, 0.96f, 1.0f, 0.97f);
    p.info.bgBottom = ImVec4(0.84f, 0.91f, 1.0f, 0.97f);
    p.info.text = ImVec4(0.09f, 0.16f, 0.30f, 1.0f);
    p.textDim = ImVec4(0.33f, 0.42f, 0.58f, 1.0f);
    p.border = ImVec4(0.36f, 0.55f, 0.88f, 0.55f);
    p.separator = ImVec4(0.36f, 0.55f, 0.88f, 0.35f);
    p.btn = ImVec4(0.42f, 0.66f, 0.98f, 0.55f);
    p.btnHover = ImVec4(0.36f, 0.62f, 0.98f, 0.75f);
    p.btnActive = ImVec4(0.22f, 0.50f, 0.95f, 0.90f);
    p.frame = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
    p.frameHover = ImVec4(1.0f, 1.0f, 1.0f, 0.85f);
    p.frameActive = ImVec4(0.88f, 0.94f, 1.0f, 1.0f);
    p.wave[0] = ImVec4(0.55f, 0.74f, 1.00f, 0.20f);
    p.wave[1] = ImVec4(0.32f, 0.62f, 1.00f, 0.18f);
    p.wave[2] = ImVec4(0.20f, 0.52f, 0.96f, 0.20f);
    p.launchL = ImVec4(0.14f, 0.40f, 0.95f, 1.0f);
    p.launchR = ImVec4(0.18f, 0.70f, 1.00f, 1.0f);
    p.title = ImVec4(0.10f, 0.42f, 0.92f, 1.0f);
    p.good = ImVec4(0.04f, 0.56f, 0.26f, 1.0f);
    p.warn = ImVec4(0.78f, 0.46f, 0.02f, 1.0f);
    p.idle = ImVec4(0.30f, 0.40f, 0.58f, 1.0f);
    p.panelBorder = ImVec4(0.30f, 0.48f, 0.85f, 0.70f);
    p.hilite = ImVec4(1, 1, 1, 0.55f);
    p.shadowA = 0.06f;
    p.snowA = 1.0f;
    return p;
}

const std::vector<Palette>& Palettes() {
    static const std::vector<Palette> list = {
        MakeDark("Red / Black", ImVec4(0.95f, 0.18f, 0.20f, 1.0f)),
        MakeDark("Green / Black", ImVec4(0.12f, 0.82f, 0.38f, 1.0f)),
        MakeDark("Blue / Black", ImVec4(0.18f, 0.50f, 1.00f, 1.0f)),
        MakeDark("Yellow / Black", ImVec4(1.00f, 0.80f, 0.10f, 1.0f)),
        MakeFrost(),
    };
    return list;
}

int g_theme = 4;

const Palette& P() { return Palettes()[g_theme]; }

void RoundedVGradient(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                      ImU32 top, ImU32 bottom, float rounding) {
    const float split = std::min((max.y - min.y) * 0.5f, rounding * 2.0f);
    dl->AddRectFilled(min, ImVec2(max.x, min.y + split), top, rounding, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(min.x, max.y - split), max, bottom, rounding, ImDrawFlags_RoundCornersBottom);
    dl->AddRectFilledMultiColor(ImVec2(min.x, min.y + split), ImVec2(max.x, max.y - split),
                                top, top, bottom, bottom);
}

struct WaveShape {
    float amp;
    float base;
    float freq;
    float speed;
    float phase;
};

void DrawWaveLayer(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float bottomY,
                   const WaveShape& shape, const ImVec4& color, bool light, float time) {
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    if (w < 8.0f || h < 8.0f) return;

    const int segments = std::max(24, std::min(120, static_cast<int>(w / 5.0f)));
    const float step = w / segments;
    const ImU32 fill = Col(color);
    const ImVec4 lineBase = light ? ImVec4(color.x * 0.8f, color.y * 0.9f, 1.0f, 0.80f)
                                  : Mix(ImVec4(color.x, color.y, color.z, 0.95f), ImVec4(1, 1, 1, 0.95f), 0.25f);
    const ImU32 line = Col(lineBase);

    static thread_local ImVector<ImVec2> top;
    top.resize(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / segments;
        const float a = u * shape.freq * 2.0f * kPi + time * shape.speed + shape.phase;
        const float s = std::sin(a) + 0.35f * std::sin(a * 1.9f + time * 0.6f);
        top[i] = ImVec2(min.x + step * i, min.y + h * shape.base + s * h * shape.amp);
    }

    const ImVec2 white = dl->_Data->TexUvWhitePixel;
    const unsigned int base = dl->_VtxCurrentIdx;
    dl->PrimReserve(segments * 6, (segments + 1) * 2);
    for (int i = 0; i <= segments; ++i) {
        dl->PrimWriteVtx(top[i], white, fill);
        dl->PrimWriteVtx(ImVec2(top[i].x, bottomY), white, fill);
    }
    for (int i = 0; i < segments; ++i) {
        const ImDrawIdx t0 = static_cast<ImDrawIdx>(base + i * 2);
        const ImDrawIdx b0 = t0 + 1, t1 = t0 + 2, b1 = t0 + 3;
        dl->PrimWriteIdx(t0); dl->PrimWriteIdx(t1); dl->PrimWriteIdx(b0);
        dl->PrimWriteIdx(t1); dl->PrimWriteIdx(b1); dl->PrimWriteIdx(b0);
    }
    dl->AddPolyline(top.Data, top.Size, line, ImDrawFlags_None, std::max(1.2f, S(1.6f)));
}

void DrawSnow(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float time, float alphaScale) {
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    constexpr int kFlakes = 46;
    for (int i = 0; i < kFlakes; ++i) {
        const float size = 0.8f + Hash01(i * 7 + 1) * 1.9f;
        const float speed = 14.0f + Hash01(i * 7 + 2) * 30.0f;
        const float sway = 4.0f + Hash01(i * 7 + 3) * 12.0f;
        const float phase = Hash01(i * 7 + 4) * 6.2831853f;
        const float x0 = Hash01(i * 7 + 5) * w;
        const float y0 = Hash01(i * 7 + 6) * h;

        const float y = std::fmod(y0 + time * speed * (h / 300.0f + 0.4f), h + 8.0f) - 4.0f;
        const float x = x0 + std::sin(time * (0.5f + size * 0.25f) + phase) * sway;
        const float edge = std::min(1.0f, std::min(y + 4.0f, h + 4.0f - y) / 14.0f);
        const float alpha = (0.55f + 0.4f * Hash01(i * 7 + 3)) * std::max(0.0f, edge) * alphaScale;
        dl->AddCircleFilled(ImVec2(min.x + x, min.y + y), size * std::max(0.8f, S(1.0f)),
                            Col(1.0f, 1.0f, 1.0f, alpha), 6);
    }
}

}

float Tween::Update(bool target, float seconds) {
    const float dt = ImGui::GetIO().DeltaTime;
    const float step = seconds > 0.0f ? dt / seconds : 1.0f;
    t = target ? std::min(1.0f, t + step) : std::max(0.0f, t - step);
    return t;
}

float EaseOutCubic(float x) {
    x = std::max(0.0f, std::min(1.0f, x));
    const float inv = 1.0f - x;
    return 1.0f - inv * inv * inv;
}

float EaseOutBack(float x) {
    x = std::max(0.0f, std::min(1.0f, x));
    constexpr float c1 = 1.35f;
    constexpr float c3 = c1 + 1.0f;
    const float p = x - 1.0f;
    return 1.0f + c3 * p * p * p + c1 * p * p;
}

int ThemeCount() { return static_cast<int>(Palettes().size()); }
const ThemeInfo& ThemeAt(int index) {
    index = std::max(0, std::min(ThemeCount() - 1, index));
    return Palettes()[index].info;
}
int CurrentTheme() { return g_theme; }
void SetTheme(int index) { g_theme = std::max(0, std::min(ThemeCount() - 1, index)); }

ImVec4 TitleColor() { return P().title; }
ImVec4 GoodColor() { return P().good; }
ImVec4 WarnColor() { return P().warn; }
ImVec4 IdleColor() { return P().idle; }
ImVec4 AccentButtonColor() {
    const Palette& p = P();
    return p.info.light ? ImVec4(0.22f, 0.50f, 0.95f, 0.95f) : Mul(p.info.accent, 0.85f, 1.0f);
}

void PushTheme(float alpha) {
    const Palette& p = P();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, p.info.text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, p.textDim);
    ImGui::PushStyleColor(ImGuiCol_Border, p.border);
    ImGui::PushStyleColor(ImGuiCol_Separator, p.separator);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, p.frame);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, p.frameHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, p.frameActive);
    ImGui::PushStyleColor(ImGuiCol_Button, p.btn);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.btnHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.btnActive);
}

void PopTheme() {
    ImGui::PopStyleColor(11);
    ImGui::PopStyleVar(4);
}

void DrawPanelBackground(float rounding, bool waves, bool snow) {
    const Palette& pal = P();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const float time = static_cast<float>(ImGui::GetTime());
    const float alpha = ImGui::GetStyle().Alpha;

    for (int i = 4; i >= 1; --i) {
        const float grow = S(3.0f) * i;
        dl->AddRectFilled(ImVec2(min.x - grow, min.y - grow + S(4.0f)),
                          ImVec2(max.x + grow, max.y + grow + S(4.0f)),
                          Col(0.0f, 0.0f, 0.0f, pal.shadowA * alpha), rounding + grow);
    }

    RoundedVGradient(dl, min, max, Col(pal.info.bgTop, alpha), Col(pal.info.bgBottom, alpha), rounding);

    dl->PushClipRect(ImVec2(min.x, min.y + rounding), ImVec2(max.x, max.y - rounding), true);
    if (waves) {
        const float bottomY = max.y - rounding;
        static const WaveShape shapes[3] = {
            {0.050f, 0.58f, 1.00f, 0.90f, 0.0f},
            {0.060f, 0.68f, 1.35f, -1.15f, 1.7f},
            {0.045f, 0.78f, 0.80f, 1.60f, 3.1f},
        };
        for (int i = 0; i < 3; ++i) {
            ImVec4 c = pal.wave[i];
            c.w *= alpha;
            DrawWaveLayer(dl, min, max, bottomY, shapes[i], c, pal.info.light, time);
        }
    }
    if (snow) DrawSnow(dl, ImVec2(min.x, min.y + rounding), ImVec2(max.x, max.y - rounding),
                       time, pal.snowA * alpha);
    dl->PopClipRect();

    dl->AddRect(min, max, Col(pal.panelBorder, alpha), rounding, 0, std::max(1.0f, S(1.5f)));
    dl->AddRect(ImVec2(min.x + 1.5f, min.y + 1.5f), ImVec2(max.x - 1.5f, max.y - 1.5f),
                Col(pal.hilite, alpha), rounding - 1.0f, 0, 1.0f);
}

bool LauncherButton(const char* id, const char* label, ImVec2 size, ImU32 statusColor) {
    const Palette& pal = P();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float time = static_cast<float>(ImGui::GetTime());
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(id, size);
    const bool pressed = ImGui::IsItemDeactivated() && ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    static float press = 0.0f;
    const float dt = ImGui::GetIO().DeltaTime;
    press += ((held ? 1.0f : 0.0f) - press) * std::min(1.0f, dt * 18.0f);
    const float scale = 1.0f - 0.06f * press;
    const ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    const ImVec2 half(size.x * 0.5f * scale, size.y * 0.5f * scale);
    const ImVec2 min(center.x - half.x, center.y - half.y);
    const ImVec2 max(center.x + half.x, center.y + half.y);
    const float h = max.y - min.y;
    const float rounding = h * 0.5f;

    const float pulse = 0.5f + 0.5f * std::sin(time * 2.2f);
    for (int i = 3; i >= 1; --i) {
        const float grow = S(2.5f) * i + pulse * S(3.0f);
        dl->AddRectFilled(ImVec2(min.x - grow, min.y - grow), ImVec2(max.x + grow, max.y + grow),
                          Col(pal.info.accent.x, pal.info.accent.y, pal.info.accent.z,
                              (0.10f + 0.06f * pulse) / i), rounding + grow);
    }

    const float drift = 0.5f + 0.5f * std::sin(time * 0.9f);
    const ImU32 leftEnd = Col(Mix(pal.launchL, Mul(pal.launchL, 1.3f, 1.0f), drift));
    const ImU32 rightEnd = Col(Mix(pal.launchR, Mul(pal.launchR, 1.15f, 1.0f), drift));
    dl->AddRectFilled(min, max, rightEnd, rounding);
    dl->AddRectFilled(min, ImVec2(min.x + rounding * 2.0f, max.y), leftEnd, rounding,
                      ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilledMultiColor(ImVec2(min.x + rounding, min.y), ImVec2(max.x - rounding, max.y),
                                leftEnd, rightEnd, rightEnd, leftEnd);

    dl->AddRectFilled(ImVec2(min.x + rounding * 0.55f, min.y + h * 0.07f),
                      ImVec2(max.x - rounding * 0.55f, min.y + h * 0.48f),
                      Col(1, 1, 1, pal.info.light ? 0.24f : 0.14f), h * 0.22f);

    const float cycle = std::fmod(time, 3.2f) / 1.1f;
    if (cycle < 1.0f) {
        dl->PushClipRect(ImVec2(min.x + rounding * 0.6f, min.y + 1.0f),
                         ImVec2(max.x - rounding * 0.6f, max.y - 1.0f), true);
        const float sx = min.x - h + (max.x - min.x + h * 2.0f) * EaseOutCubic(cycle);
        const ImVec2 quad[4] = {
            ImVec2(sx, min.y), ImVec2(sx + h * 0.55f, min.y),
            ImVec2(sx + h * 0.55f - h * 0.45f, max.y), ImVec2(sx - h * 0.45f, max.y)};
        dl->AddConvexPolyFilled(quad, 4, Col(1, 1, 1, 0.34f));
        dl->PopClipRect();
    }
    dl->AddRect(min, max, Col(1, 1, 1, 0.85f), rounding, 0, std::max(1.2f, S(1.6f)));

    const float r = h * 0.20f;
    const ImVec2 logo(min.x + rounding * 0.95f, center.y);
    const float lw = std::max(1.5f, S(2.0f));
    dl->AddCircleFilled(ImVec2(logo.x, logo.y), r * 1.9f, Col(1, 1, 1, 0.20f), 20);
    dl->AddCircle(ImVec2(logo.x - r * 0.55f, logo.y), r, Col(1, 1, 1, 0.95f), 18, lw);
    dl->AddCircle(ImVec2(logo.x + r * 0.55f, logo.y), r, Col(1, 1, 1, 0.95f), 18, lw);

    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();
    const float textLeft = logo.x + r * 2.5f;
    const float textRight = max.x - rounding * 1.15f;
    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
    if (textSize.x > textRight - textLeft && textSize.x > 1.0f) {
        fontSize *= (textRight - textLeft) / textSize.x;
        textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
    }
    const ImVec2 textPos(textLeft + (textRight - textLeft - textSize.x) * 0.5f,
                         center.y - textSize.y * 0.5f);
    dl->AddText(font, fontSize, ImVec2(textPos.x, textPos.y + 1.2f), Col(0.0f, 0.0f, 0.0f, 0.45f), label);
    dl->AddText(font, fontSize, textPos, Col(1, 1, 1, 1), label);

    if ((statusColor >> IM_COL32_A_SHIFT) & 0xFF) {
        const ImVec2 dot(max.x - rounding * 0.62f, center.y);
        const float dr = h * 0.12f;
        dl->AddCircleFilled(dot, dr + pulse * S(2.0f) + 1.0f,
                            (statusColor & 0x00FFFFFF) | (0x55u << IM_COL32_A_SHIFT), 14);
        dl->AddCircleFilled(dot, dr, statusColor, 14);
        dl->AddCircle(dot, dr, Col(1, 1, 1, 0.9f), 14, 1.2f);
    }
    return pressed;
}

bool GearButton(const char* id, ImVec2 size, bool spin) {
    const Palette& pal = P();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    static float angle = 0.0f;
    const float dt = ImGui::GetIO().DeltaTime;
    angle += dt * (spin ? 1.6f : (hovered || held ? 0.9f : 0.0f));
    if (angle > 2.0f * kPi) angle -= 2.0f * kPi;

    const ImVec2 min = pos;
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    const ImVec4& bg = held ? pal.btnActive : (hovered || spin ? pal.btnHover : pal.btn);
    const float alpha = ImGui::GetStyle().Alpha;
    dl->AddRectFilled(min, max, Col(bg, alpha), S(10.0f));
    dl->AddRect(min, max, Col(pal.border, alpha), S(10.0f), 0, 1.0f);

    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float R = std::min(size.x, size.y) * 0.36f;
    const float body = R * 0.74f;
    const float thick = R * 0.36f;
    const ImU32 col = Col(pal.info.text, alpha);
    for (int k = 0; k < 8; ++k) {
        const float a = angle + k * (2.0f * kPi / 8.0f);
        const ImVec2 d(std::cos(a), std::sin(a));
        const ImVec2 n(-d.y, d.x);
        const float inner = body - 0.5f;
        const ImVec2 quad[4] = {
            ImVec2(c.x + d.x * inner + n.x * R * 0.21f, c.y + d.y * inner + n.y * R * 0.21f),
            ImVec2(c.x + d.x * R + n.x * R * 0.14f, c.y + d.y * R + n.y * R * 0.14f),
            ImVec2(c.x + d.x * R - n.x * R * 0.14f, c.y + d.y * R - n.y * R * 0.14f),
            ImVec2(c.x + d.x * inner - n.x * R * 0.21f, c.y + d.y * inner - n.y * R * 0.21f)};
        dl->AddConvexPolyFilled(quad, 4, col);
    }
    dl->AddCircle(c, body - thick * 0.5f, col, 28, thick);
    return clicked;
}

bool ThemeSwatch(const char* id, int index, ImVec2 size, bool selected) {
    const ThemeInfo& info = ThemeAt(index);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool held = ImGui::IsItemActive();
    const float time = static_cast<float>(ImGui::GetTime());
    const float alpha = ImGui::GetStyle().Alpha;

    static float press = 0.0f;
    press += ((held ? 1.0f : 0.0f) - press) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f);
    const float scale = 1.0f - 0.05f * (held ? press : 0.0f);
    const ImVec2 c(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    const ImVec2 min(c.x - size.x * 0.5f * scale, c.y - size.y * 0.5f * scale);
    const ImVec2 max(c.x + size.x * 0.5f * scale, c.y + size.y * 0.5f * scale);
    const float rounding = S(12.0f);
    const float w = max.x - min.x;
    const float h = max.y - min.y;

    RoundedVGradient(dl, min, max, Col(info.bgTop, alpha), Col(info.bgBottom, alpha), rounding);

    dl->PushClipRect(ImVec2(min.x, min.y + rounding), ImVec2(max.x, max.y - rounding), true);
    for (int layer = 0; layer < 2; ++layer) {
        ImVec2 pts[33];
        for (int i = 0; i <= 32; ++i) {
            const float u = static_cast<float>(i) / 32.0f;
            const float a = u * (1.2f + layer * 0.5f) * 2.0f * kPi + time * (layer ? -1.3f : 1.0f) + layer * 2.0f;
            pts[i] = ImVec2(min.x + w * u, min.y + h * (0.62f + layer * 0.12f) + std::sin(a) * h * 0.06f);
        }
        dl->AddPolyline(pts, 33, Col(info.accent.x, info.accent.y, info.accent.z, (layer ? 0.55f : 0.95f) * alpha),
                        ImDrawFlags_None, std::max(1.5f, S(2.0f)));
    }
    dl->PopClipRect();

    const float dotR = h * 0.075f;
    const ImVec2 dot(min.x + rounding + dotR * 0.4f, min.y + h * 0.24f);
    dl->AddCircleFilled(dot, dotR, Col(info.accent, alpha), 16);
    dl->AddCircle(dot, dotR, Col(1, 1, 1, 0.55f * alpha), 16, 1.0f);
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, info.name);
    dl->AddText(font, fontSize, ImVec2(dot.x + dotR * 1.8f, dot.y - ts.y * 0.5f),
                Col(info.text, alpha), info.name);

    dl->AddRect(min, max, Col(info.accent, (selected ? 1.0f : 0.45f) * alpha), rounding, 0,
                selected ? std::max(2.5f, S(3.0f)) : 1.0f);
    if (selected) {
        const float br = h * 0.11f;
        const ImVec2 b(max.x - br - rounding * 0.8f, min.y + h * 0.24f);
        dl->AddCircleFilled(b, br, Col(info.accent, alpha), 18);
        const ImVec2 tick[3] = {ImVec2(b.x - br * 0.48f, b.y + br * 0.02f),
                                ImVec2(b.x - br * 0.12f, b.y + br * 0.38f),
                                ImVec2(b.x + br * 0.52f, b.y - br * 0.34f)};
        dl->AddPolyline(tick, 3, Col(0.02f, 0.02f, 0.02f, alpha), ImDrawFlags_None, std::max(1.8f, S(2.4f)));
    }
    return clicked;
}

}
}
