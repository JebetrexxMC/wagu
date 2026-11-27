#pragma once

#include "imgui.h"

namespace UI {
float S(float pixels);
ImFont* Font();
void OutlinedText(ImDrawList* drawList, float fontSize,
                  const ImVec2& position, ImU32 color, const char* text);
}
