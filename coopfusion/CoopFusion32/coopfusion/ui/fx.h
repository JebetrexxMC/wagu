#pragma once

#include "imgui.h"

namespace UI {
namespace Fx {

struct Tween {
    float t = 0.0f;
    float Update(bool target, float seconds);
    bool Settled(bool target) const { return target ? t >= 1.0f : t <= 0.0f; }
};

float EaseOutCubic(float x);
float EaseOutBack(float x);

struct ThemeInfo {
    const char* name;
    ImVec4 accent;
    ImVec4 bgTop;
    ImVec4 bgBottom;
    ImVec4 text;
    bool light;
};
int ThemeCount();
const ThemeInfo& ThemeAt(int index);
int CurrentTheme();
void SetTheme(int index);

ImVec4 TitleColor();
ImVec4 GoodColor();
ImVec4 WarnColor();
ImVec4 IdleColor();
ImVec4 AccentButtonColor();

void PushTheme(float alpha);
void PopTheme();

void DrawPanelBackground(float rounding, bool waves, bool snow);

bool LauncherButton(const char* id, const char* label, ImVec2 size, ImU32 statusColor);

bool GearButton(const char* id, ImVec2 size, bool spin);

bool ThemeSwatch(const char* id, int index, ImVec2 size, bool selected);

}
}
