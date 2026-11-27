#include "ui_common.h"

#include <algorithm>

namespace UI {
float S(float pixels) {
    const float height = ImGui::GetIO().DisplaySize.y;
    const float scale = height > 0.0f ? height / 1080.0f : 1.0f;
    return std::max(0.65f, std::min(1.8f, scale)) * pixels;
}

ImFont* Font() { return ImGui::GetFont(); }

void OutlinedText(ImDrawList* drawList, float fontSize,
                  const ImVec2& position, ImU32 color, const char* text) {
    if (!drawList || !text || !*text) return;
    const float outline = S(1.2f);
    const ImU32 shadow = IM_COL32(0, 0, 0, 230);
    ImFont* font = ImGui::GetFont();
    drawList->AddText(font, fontSize, ImVec2(position.x - outline, position.y), shadow, text);
    drawList->AddText(font, fontSize, ImVec2(position.x + outline, position.y), shadow, text);
    drawList->AddText(font, fontSize, ImVec2(position.x, position.y - outline), shadow, text);
    drawList->AddText(font, fontSize, ImVec2(position.x, position.y + outline), shadow, text);
    drawList->AddText(font, fontSize, position, color, text);
}
}
