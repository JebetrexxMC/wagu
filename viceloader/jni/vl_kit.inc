// VICE Loader: the San Andreas MP launcher look, copied from src/mp_launcher.inc and src/mp_social.inc (MP 4.13.63).
// Keep it in sync with those files so the overlay looks exactly like the launcher inside San Andreas.
#pragma once
static void MpUiClick() {}
enum { IC_PLAY, IC_PLUS, IC_TRASH, IC_REFRESH, IC_GEAR, IC_SLIDERS, IC_INFO, IC_CLOSE, IC_GLOBE, IC_HEART,
       IC_BOX, IC_USERS, IC_BAN, IC_LIST, IC_SEARCH, IC_CHAT, IC_USER, IC_STOP, IC_REGION,
       IC_BELL, IC_USERPLUS, IC_PARTY, IC_FLAG, IC_SEND, IC_CHECK, IC_CLOUD };   // 3.63, 4.02 cloud

// ---- 4.03: CS:GO community browser style (was Valorant 3.63) - same layout, square corners everywhere ----
// slate glass panels, thin light lines, white selection, blue accent, green CONNECT
static const ImU32 kVRed    = IM_COL32(222, 74, 60, 255);
static const ImU32 kVRedDim = IM_COL32(222, 74, 60, 60);
static const ImU32 kVTeal   = IM_COL32(126, 178, 226, 255);   // CS:GO blue (accent / selected)
static const ImU32 kVTealDim= IM_COL32(126, 178, 226, 40);
static const ImU32 kVText   = IM_COL32(226, 229, 233, 255);
static const ImU32 kVDim    = IM_COL32(150, 157, 166, 255);
static const ImU32 kVLine   = IM_COL32(255, 255, 255, 30);
static const ImU32 kVPanel  = IM_COL32(52, 58, 66, 150);
static const ImU32 kVDark   = IM_COL32(26, 29, 34, 255);
static const ImU32 kVGreen  = IM_COL32(76, 160, 48, 255);      // CONNECT
static inline ImU32 VAlpha(ImU32 c, float a) { int al = (int)(((c >> IM_COL32_A_SHIFT) & 0xFF) * a); return (c & ~IM_COL32_A_MASK) | ((ImU32)ImClamp(al, 0, 255) << IM_COL32_A_SHIFT); }
// 4.03: plain square box (the old cut corners are gone, the cut size is ignored)
static void VChamfer(ImDrawList* dl, ImVec2 a, ImVec2 b, float /*cut*/, ImU32 fill, ImU32 border = 0, float thick = 0)
{
    if(fill) dl->AddRectFilled(a, b, fill, 0.0f);
    if(border) dl->AddRect(a, b, border, 0.0f, 0, thick > 0 ? thick : UI::S(1.0f));
}
// 4.03: square tab / chip (was a pill)
static void VPill(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 fill, ImU32 border = 0)
{
    if(fill) dl->AddRectFilled(a, b, fill, 0.0f);
    if(border) dl->AddRect(a, b, border, 0.0f, 0, UI::S(1.0f));
}
static void VTicks(ImDrawList*, ImVec2, ImVec2, ImU32, float) {}   // 4.03: no corner brackets in the CS:GO look
// a soft vertical slate gradient (CS:GO panel)
static void VSlate(ImDrawList* dl, ImVec2 a, ImVec2 b, float alpha = 1.0f)
{
    dl->AddRectFilledMultiColor(a, b, IM_COL32(64, 72, 82, (int)(235 * alpha)), IM_COL32(64, 72, 82, (int)(235 * alpha)),
                                      IM_COL32(38, 43, 50, (int)(245 * alpha)), IM_COL32(38, 43, 50, (int)(245 * alpha)));
}
// 4.05.1: the CS:GO backdrop = ui_bg.jpg (blurred night city, built into the .so) covering the screen + a
// dark tint; pages are VGlass (see-through slate) on top, so the picture shows through every page
static void LImageCover(ImDrawList* dl, void* tex, ImVec2 uvMax, ImVec2 a, ImVec2 b, ImU32 tint);
static void VBackdrop(ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    ImVec2 uv(1, 1);
    if(void* t = HUD::Texture("ui_bg.jpg", &uv))
    {
        LImageCover(dl, t, uv, a, b, IM_COL32(255, 255, 255, 255));
        dl->AddRectFilled(a, b, IM_COL32(8, 10, 14, 95));
    }
    else dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 150));
}
static void VGlass(ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    dl->AddRectFilledMultiColor(a, b, IM_COL32(58, 66, 76, 95), IM_COL32(58, 66, 76, 95), IM_COL32(18, 21, 26, 150), IM_COL32(18, 21, 26, 150));
}
// ImGui widgets inside the launcher pages (settings, add server, ...) get the same look
static void VPushWidgetStyle()
{
    ImGui::PushStyleColor(ImGuiCol_Button,         ImVec4(0.30f, 0.34f, 0.39f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.42f, 0.47f, 0.53f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.49f, 0.70f, 0.89f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.13f, 0.15f, 0.18f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.20f, 0.24f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.22f, 0.25f, 0.29f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,      ImVec4(0.49f, 0.70f, 0.89f, 1));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,     ImVec4(0.49f, 0.70f, 0.89f, 1));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.65f, 0.80f, 0.95f, 1));
    ImGui::PushStyleColor(ImGuiCol_Header,         ImVec4(0.49f, 0.70f, 0.89f, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.49f, 0.70f, 0.89f, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(0.49f, 0.70f, 0.89f, 0.42f));
    ImGui::PushStyleColor(ImGuiCol_Separator,      ImVec4(1, 1, 1, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(1, 1, 1, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0.89f, 0.90f, 0.91f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);   // 4.03: square boxes
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, UI::S(1));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
}
static void VPopWidgetStyle() { ImGui::PopStyleVar(6); ImGui::PopStyleColor(15); }

static void LIcon(ImDrawList* dl, int k, ImVec2 c, float r, ImU32 col)
{
    float t = ImMax(1.5f, r * 0.14f);
    switch(k)
    {
    case IC_PLAY:  dl->AddTriangle(ImVec2(c.x - r * 0.45f, c.y - r * 0.6f), ImVec2(c.x + r * 0.6f, c.y), ImVec2(c.x - r * 0.45f, c.y + r * 0.6f), col, t); break;
    case IC_STOP:  dl->AddRect(ImVec2(c.x - r * 0.5f, c.y - r * 0.5f), ImVec2(c.x + r * 0.5f, c.y + r * 0.5f), col, r * 0.12f, 0, t); break;
    case IC_PLUS:  dl->AddLine(ImVec2(c.x - r * 0.65f, c.y), ImVec2(c.x + r * 0.65f, c.y), col, t); dl->AddLine(ImVec2(c.x, c.y - r * 0.65f), ImVec2(c.x, c.y + r * 0.65f), col, t); break;
    case IC_TRASH:
        dl->AddRect(ImVec2(c.x - r * 0.42f, c.y - r * 0.38f), ImVec2(c.x + r * 0.42f, c.y + r * 0.7f), col, r * 0.1f, 0, t);
        dl->AddLine(ImVec2(c.x - r * 0.62f, c.y - r * 0.4f), ImVec2(c.x + r * 0.62f, c.y - r * 0.4f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.18f, c.y - r * 0.62f), ImVec2(c.x + r * 0.18f, c.y - r * 0.62f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.14f, c.y - r * 0.15f), ImVec2(c.x - r * 0.14f, c.y + r * 0.45f), col, t * 0.8f);
        dl->AddLine(ImVec2(c.x + r * 0.14f, c.y - r * 0.15f), ImVec2(c.x + r * 0.14f, c.y + r * 0.45f), col, t * 0.8f);
        break;
    case IC_REFRESH:
        dl->PathArcTo(c, r * 0.6f, -IM_PI * 0.35f, IM_PI * 1.45f, 24); dl->PathStroke(col, 0, t);
        dl->AddTriangleFilled(ImVec2(c.x + r * 0.30f, c.y - r * 0.92f), ImVec2(c.x + r * 0.70f, c.y - r * 0.40f), ImVec2(c.x + r * 0.12f, c.y - r * 0.30f), col);
        break;
    case IC_GEAR:
        for(int i = 0; i < 8; ++i) { float a = i * IM_PI / 4; dl->AddLine(ImVec2(c.x + cosf(a) * r * 0.45f, c.y + sinf(a) * r * 0.45f), ImVec2(c.x + cosf(a) * r * 0.72f, c.y + sinf(a) * r * 0.72f), col, t * 1.6f); }
        dl->AddCircle(c, r * 0.48f, col, 20, t); dl->AddCircle(c, r * 0.18f, col, 12, t); break;
    case IC_SLIDERS:
        for(int i = 0; i < 2; ++i)
        {
            float y = c.y + (i ? r * 0.3f : -r * 0.3f), kx = c.x + (i ? -r * 0.25f : r * 0.25f);
            dl->AddLine(ImVec2(c.x - r * 0.7f, y), ImVec2(c.x + r * 0.7f, y), col, t);
            dl->AddCircleFilled(ImVec2(kx, y), r * 0.2f, IM_COL32(20, 20, 22, 255)); dl->AddCircle(ImVec2(kx, y), r * 0.2f, col, 12, t);
        }
        break;
    case IC_INFO:
        dl->AddCircle(c, r * 0.7f, col, 24, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.32f), t * 0.75f, col);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.08f), ImVec2(c.x, c.y + r * 0.38f), col, t); break;
    case IC_CLOSE: dl->AddLine(ImVec2(c.x - r * 0.5f, c.y - r * 0.5f), ImVec2(c.x + r * 0.5f, c.y + r * 0.5f), col, t); dl->AddLine(ImVec2(c.x + r * 0.5f, c.y - r * 0.5f), ImVec2(c.x - r * 0.5f, c.y + r * 0.5f), col, t); break;
    case IC_GLOBE:
        dl->AddCircle(c, r * 0.7f, col, 24, t);
        for(int i = 0; i <= 20; ++i) { float a = i * IM_PI * 2 / 20; dl->PathLineTo(ImVec2(c.x + cosf(a) * r * 0.3f, c.y + sinf(a) * r * 0.7f)); }
        dl->PathStroke(col, 0, t * 0.8f);
        dl->AddLine(ImVec2(c.x - r * 0.7f, c.y), ImVec2(c.x + r * 0.7f, c.y), col, t * 0.8f); break;
    case IC_HEART:
    {
        ImVec2 p0(c.x, c.y + r * 0.62f);
        dl->PathLineTo(p0);
        dl->PathBezierCubicCurveTo(ImVec2(c.x - r * 0.95f, c.y), ImVec2(c.x - r * 0.6f, c.y - r * 0.85f), ImVec2(c.x, c.y - r * 0.3f), 12);
        dl->PathBezierCubicCurveTo(ImVec2(c.x + r * 0.6f, c.y - r * 0.85f), ImVec2(c.x + r * 0.95f, c.y), p0, 12);
        dl->PathStroke(col, ImDrawFlags_Closed, t); break;
    }
    case IC_CLOUD:   // 4.02: Cloud Storage
        dl->PathArcTo(ImVec2(c.x - r * 0.38f, c.y + r * 0.12f), r * 0.32f, IM_PI * 0.5f, IM_PI * 1.5f, 10);
        dl->PathArcTo(ImVec2(c.x - r * 0.05f, c.y - r * 0.18f), r * 0.40f, IM_PI * 1.05f, IM_PI * 1.95f, 12);
        dl->PathArcTo(ImVec2(c.x + r * 0.40f, c.y + r * 0.10f), r * 0.34f, IM_PI * 1.5f, IM_PI * 2.5f, 10);
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.02f), ImVec2(c.x, c.y + r * 0.62f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.2f, c.y + r * 0.40f), ImVec2(c.x, c.y + r * 0.62f), col, t);
        dl->AddLine(ImVec2(c.x + r * 0.2f, c.y + r * 0.40f), ImVec2(c.x, c.y + r * 0.62f), col, t); break;
    case IC_BOX:
        dl->AddQuad(ImVec2(c.x, c.y - r * 0.7f), ImVec2(c.x + r * 0.65f, c.y - r * 0.35f), ImVec2(c.x, c.y), ImVec2(c.x - r * 0.65f, c.y - r * 0.35f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.65f, c.y - r * 0.35f), ImVec2(c.x - r * 0.65f, c.y + r * 0.35f), col, t);
        dl->AddLine(ImVec2(c.x + r * 0.65f, c.y - r * 0.35f), ImVec2(c.x + r * 0.65f, c.y + r * 0.35f), col, t);
        dl->AddLine(ImVec2(c.x, c.y), ImVec2(c.x, c.y + r * 0.7f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.65f, c.y + r * 0.35f), ImVec2(c.x, c.y + r * 0.7f), col, t);
        dl->AddLine(ImVec2(c.x + r * 0.65f, c.y + r * 0.35f), ImVec2(c.x, c.y + r * 0.7f), col, t); break;
    case IC_USERS:
        dl->AddCircle(ImVec2(c.x - r * 0.22f, c.y - r * 0.3f), r * 0.24f, col, 14, t);
        dl->PathArcTo(ImVec2(c.x - r * 0.22f, c.y + r * 0.55f), r * 0.45f, IM_PI, IM_PI * 2, 12); dl->PathStroke(col, 0, t);
        dl->AddCircle(ImVec2(c.x + r * 0.35f, c.y - r * 0.22f), r * 0.18f, col, 12, t * 0.8f);
        dl->PathArcTo(ImVec2(c.x + r * 0.38f, c.y + r * 0.5f), r * 0.32f, IM_PI * 1.15f, IM_PI * 2, 10); dl->PathStroke(col, 0, t * 0.8f); break;
    case IC_USER:
        dl->AddCircle(ImVec2(c.x, c.y - r * 0.28f), r * 0.28f, col, 16, t);
        dl->PathArcTo(ImVec2(c.x, c.y + r * 0.62f), r * 0.55f, IM_PI, IM_PI * 2, 14); dl->PathStroke(col, 0, t); break;
    case IC_BAN:
        dl->AddCircle(c, r * 0.65f, col, 24, t);
        dl->AddLine(ImVec2(c.x - r * 0.46f, c.y + r * 0.46f), ImVec2(c.x + r * 0.46f, c.y - r * 0.46f), col, t); break;
    case IC_LIST:
        for(int i = -1; i <= 1; ++i) { float y = c.y + i * r * 0.4f; dl->AddCircleFilled(ImVec2(c.x - r * 0.55f, y), t * 0.7f, col); dl->AddLine(ImVec2(c.x - r * 0.3f, y), ImVec2(c.x + r * 0.65f, y), col, t); }
        break;
    case IC_SEARCH:
        dl->AddCircle(ImVec2(c.x - r * 0.12f, c.y - r * 0.12f), r * 0.42f, col, 18, t);
        dl->AddLine(ImVec2(c.x + r * 0.2f, c.y + r * 0.2f), ImVec2(c.x + r * 0.62f, c.y + r * 0.62f), col, t * 1.2f); break;
    case IC_CHAT:
        dl->AddRect(ImVec2(c.x - r * 0.65f, c.y - r * 0.5f), ImVec2(c.x + r * 0.65f, c.y + r * 0.3f), col, r * 0.2f, 0, t);
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.3f, c.y + r * 0.28f), ImVec2(c.x - r * 0.05f, c.y + r * 0.28f), ImVec2(c.x - r * 0.38f, c.y + r * 0.62f), col); break;
    case IC_BELL:
    {   // 3.63: notifications
        dl->PathArcTo(ImVec2(c.x, c.y - r * 0.1f), r * 0.45f, IM_PI, IM_PI * 2, 16);
        dl->PathLineTo(ImVec2(c.x + r * 0.45f, c.y + r * 0.3f)); dl->PathLineTo(ImVec2(c.x + r * 0.62f, c.y + r * 0.45f));
        dl->PathLineTo(ImVec2(c.x - r * 0.62f, c.y + r * 0.45f)); dl->PathLineTo(ImVec2(c.x - r * 0.45f, c.y + r * 0.3f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.66f), t * 1.1f, col);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.72f), ImVec2(c.x, c.y - r * 0.55f), col, t);
        break;
    }
    case IC_USERPLUS:
        dl->AddCircle(ImVec2(c.x - r * 0.18f, c.y - r * 0.28f), r * 0.26f, col, 16, t);
        dl->PathArcTo(ImVec2(c.x - r * 0.18f, c.y + r * 0.6f), r * 0.5f, IM_PI, IM_PI * 2, 14); dl->PathStroke(col, 0, t);
        dl->AddLine(ImVec2(c.x + r * 0.42f, c.y - r * 0.2f), ImVec2(c.x + r * 0.82f, c.y - r * 0.2f), col, t);
        dl->AddLine(ImVec2(c.x + r * 0.62f, c.y - r * 0.4f), ImVec2(c.x + r * 0.62f, c.y), col, t); break;
    case IC_PARTY:   // a crown (the party leader) over 3 heads
        dl->PathLineTo(ImVec2(c.x - r * 0.6f, c.y + r * 0.45f)); dl->PathLineTo(ImVec2(c.x - r * 0.7f, c.y - r * 0.35f)); dl->PathLineTo(ImVec2(c.x - r * 0.3f, c.y));
        dl->PathLineTo(ImVec2(c.x, c.y - r * 0.6f)); dl->PathLineTo(ImVec2(c.x + r * 0.3f, c.y)); dl->PathLineTo(ImVec2(c.x + r * 0.7f, c.y - r * 0.35f));
        dl->PathLineTo(ImVec2(c.x + r * 0.6f, c.y + r * 0.45f)); dl->PathStroke(col, ImDrawFlags_Closed, t); break;
    case IC_FLAG:    // community
        dl->AddLine(ImVec2(c.x - r * 0.5f, c.y - r * 0.7f), ImVec2(c.x - r * 0.5f, c.y + r * 0.7f), col, t);
        dl->PathLineTo(ImVec2(c.x - r * 0.5f, c.y - r * 0.65f)); dl->PathLineTo(ImVec2(c.x + r * 0.6f, c.y - r * 0.45f));
        dl->PathLineTo(ImVec2(c.x + r * 0.25f, c.y - r * 0.15f)); dl->PathLineTo(ImVec2(c.x + r * 0.6f, c.y + r * 0.15f)); dl->PathLineTo(ImVec2(c.x - r * 0.5f, c.y + r * 0.1f));
        dl->PathStroke(col, 0, t); break;
    case IC_SEND:
        dl->PathLineTo(ImVec2(c.x - r * 0.65f, c.y - r * 0.6f)); dl->PathLineTo(ImVec2(c.x + r * 0.7f, c.y)); dl->PathLineTo(ImVec2(c.x - r * 0.65f, c.y + r * 0.6f)); dl->PathLineTo(ImVec2(c.x - r * 0.35f, c.y));
        dl->PathStroke(col, ImDrawFlags_Closed, t); dl->AddLine(ImVec2(c.x - r * 0.35f, c.y), ImVec2(c.x + r * 0.2f, c.y), col, t); break;
    case IC_CHECK:
        dl->AddLine(ImVec2(c.x - r * 0.55f, c.y), ImVec2(c.x - r * 0.15f, c.y + r * 0.42f), col, t * 1.3f);
        dl->AddLine(ImVec2(c.x - r * 0.15f, c.y + r * 0.42f), ImVec2(c.x + r * 0.6f, c.y - r * 0.45f), col, t * 1.3f); break;
    case IC_REGION:
    {   // 3.25.1: map pin = the region switch (round head, point at the bottom, a hole in the middle)
        const ImVec2 hc(c.x, c.y - r * 0.2f); const float pr = r * 0.46f, tipY = c.y + r * 0.75f;
        const float ta = acosf(pr / (tipY - hc.y));   // where the two straight sides leave the head
        dl->PathArcTo(hc, pr, IM_PI * 0.5f + ta, IM_PI * 2.5f - ta, 24);
        dl->PathLineTo(ImVec2(c.x, tipY));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddCircle(hc, pr * 0.38f, col, 12, t);
        break;
    }
    }
}

static void LPadlock(ImDrawList* dl, ImVec2 c, float r, bool locked, ImU32 col)
{
    float t = ImMax(1.5f, r * 0.14f);
    dl->AddRect(ImVec2(c.x - r * 0.5f, c.y - r * 0.05f), ImVec2(c.x + r * 0.5f, c.y + r * 0.62f), col, r * 0.12f, 0, t);
    if(locked) { dl->PathArcTo(ImVec2(c.x, c.y - r * 0.12f), r * 0.32f, IM_PI, IM_PI * 2, 12); dl->PathStroke(col, 0, t);
                 dl->AddLine(ImVec2(c.x - r * 0.32f, c.y - r * 0.12f), ImVec2(c.x - r * 0.32f, c.y - r * 0.05f), col, t); dl->AddLine(ImVec2(c.x + r * 0.32f, c.y - r * 0.12f), ImVec2(c.x + r * 0.32f, c.y - r * 0.05f), col, t); }
    else       { dl->PathArcTo(ImVec2(c.x + r * 0.3f, c.y - r * 0.2f), r * 0.32f, IM_PI, IM_PI * 2, 12); dl->PathStroke(col, 0, t);
                 dl->AddLine(ImVec2(c.x - r * 0.02f, c.y - r * 0.2f), ImVec2(c.x - r * 0.02f, c.y - r * 0.05f), col, t); }
}

// drag a child window with the finger to scroll it
static void LDragScroll()
{
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseDragging(0, UI::S(14)))
    {
        float dy = ImGui::GetIO().MouseDelta.y;
        if(dy != 0) ImGui::SetScrollY(ImGui::GetScrollY() - dy);
    }
}

// toolbar button: icon + optional label, flat, white highlight while pressed
static void SocText(ImDrawList* dl, ImVec2 p, ImU32 col, const char* t, float scale = 1.0f, float wrap = 0)
{
    dl->AddText(UI::Font(), ImGui::GetFontSize() * scale, p, col, t, nullptr, wrap);
}
static float SocTextW(const char* t, float scale = 1.0f) { return UI::Font()->CalcTextSizeA(ImGui::GetFontSize() * scale, FLT_MAX, 0, t).x; }
static const char* SocAgo(int64_t ts, char* b, size_t n)
{
    int64_t d = (int64_t)time(nullptr) - ts; if(d < 0) d = 0;
    if(d < 60) snprintf(b, n, "now");
    else if(d < 3600) snprintf(b, n, "%dm", (int)(d / 60));
    else if(d < 86400) snprintf(b, n, "%dh", (int)(d / 3600));
    else snprintf(b, n, "%dd", (int)(d / 86400));
    return b;
}
// Valorant button: 0 = outline, 1 = teal, 2 = red, 3 = red outline, 4 = chip (not a button)
static bool VBtn(const char* id, const char* label, float w, float h, int style, bool enabled = true, int icon = -1)
{
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos(), q(p.x + w, p.y + h);
    bool hit = false;
    if(style != 4) { if(!enabled) ImGui::BeginDisabled(); hit = ImGui::InvisibleButton("##vb", ImVec2(w, h)); if(!enabled) ImGui::EndDisabled(); }
    else ImGui::Dummy(ImVec2(w, h));
    bool held = enabled && style != 4 && ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fill = 0, border = 0, tc = kVText;
    switch(style)
    {
    case 1: fill = held ? VAlpha(kVTeal, 0.75f) : kVTeal; tc = kVDark; break;
    case 2: fill = held ? VAlpha(kVRed, 0.75f) : kVRed; tc = IM_COL32_WHITE; break;
    case 3: fill = held ? kVRedDim : IM_COL32(255, 70, 85, 18); border = VAlpha(kVRed, 0.8f); tc = IM_COL32(255, 130, 140, 255); break;
    case 4: fill = IM_COL32(236, 232, 225, 14); border = IM_COL32(236, 232, 225, 40); tc = kVDim; break;
    default: fill = held ? IM_COL32(236, 232, 225, 40) : IM_COL32(236, 232, 225, 10); border = IM_COL32(236, 232, 225, 70); break;
    }
    if(!enabled) { fill = VAlpha(fill, 0.35f); border = VAlpha(border, 0.35f); tc = VAlpha(tc, 0.45f); }
    VChamfer(dl, p, q, ImMin(UI::S(10), h * 0.3f), fill, border, UI::S(1.5f));
    float iw = icon >= 0 ? h * 0.42f : 0, tw = label && *label ? SocTextW(label, 0.9f) : 0;
    float gap = (iw > 0 && tw > 0) ? UI::S(10) : 0, x = p.x + (w - iw - gap - tw) * 0.5f;
    if(icon >= 0) LIcon(dl, icon, ImVec2(x + iw * 0.5f, p.y + h * 0.5f), iw * 0.75f, tc);
    if(tw > 0) SocText(dl, ImVec2(x + iw + gap, p.y + (h - ImGui::GetFontSize() * 0.9f) * 0.5f), tc, label, 0.9f);
    ImGui::PopID();
    if(hit && enabled) MpUiClick();
    return hit && enabled;
}
static bool LSideItem(const char* id, int icon, const char* label, bool on, float w, float h)
{
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hit = ImGui::InvisibleButton("##si", ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // 3.63: selected = teal pill + a teal bar on the left (Valorant tabs)
    const ImVec2 a1(p.x + UI::S(6), p.y + UI::S(6)), b1(p.x + w - UI::S(4), p.y + h - UI::S(6));
    if(on) { VPill(dl, a1, b1, kVTealDim, VAlpha(kVTeal, 0.55f)); dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.28f), ImVec2(p.x + UI::S(4), p.y + h * 0.72f), kVTeal, 0.0f); }
    else if(ImGui::IsItemActive()) VPill(dl, a1, b1, IM_COL32(236, 232, 225, 22));
    ImU32 col = on ? kVTeal : IM_COL32(205, 210, 214, 255);
    LIcon(dl, icon, ImVec2(p.x + UI::S(36), p.y + h * 0.5f), h * 0.24f, col);
    dl->PushClipRect(p, ImVec2(p.x + w, p.y + h), true);
    dl->AddText(ImVec2(p.x + UI::S(74), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), col, label);
    dl->PopClipRect();
    ImGui::PopID();
    return hit;
}

static void LGroupLabel(const char* t)
{
    ImGui::Dummy(ImVec2(1, UI::S(6)));
    // 3.63: small dim caps + a thin line to the right (Valorant section headers)
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float fs = ImGui::GetFontSize() * 0.82f, lh = ImGui::GetTextLineHeight();
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t);
    float x = p.x + UI::S(14), y = p.y + (lh - fs) * 0.5f;
    dl->AddText(UI::Font(), fs, ImVec2(x, y), kVDim, t);
    float lx = x + ts.x + UI::S(12), rx = p.x + ImGui::GetContentRegionAvail().x - UI::S(6);
    if(rx > lx) dl->AddLine(ImVec2(lx, p.y + lh * 0.5f), ImVec2(rx, p.y + lh * 0.5f), kVLine, UI::S(1));
    ImGui::Dummy(ImVec2(1, lh));
}


#define L_PRESS_ON_CLICK ((ImGuiButtonFlags)(1 << 4))   // 4.05.2: ImGuiButtonFlags_PressedOnClick (imgui_internal.h)
static bool LToolButton(const char* id, int icon, const char* label, float h, bool enabled = true, bool on = false)
{
    float isz = h * 0.42f;
    float w = label ? (h * 0.55f + isz + ImGui::CalcTextSize(label).x + h * 0.35f) : h;
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if(!enabled) ImGui::BeginDisabled();
    // 4.05.2: fires on the press (not the release) - the toolbar sits over the GTA weapon icon and a
    // release could get lost there, so refresh / region / settings / add server did nothing
    bool hit = ImGui::InvisibleButton("##tb", ImVec2(w, h), L_PRESS_ON_CLICK);
    if(!enabled) ImGui::EndDisabled();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // 3.63: Valorant look - a thin chamfered frame, teal while selected, light fill while pressed
    const ImVec2 q(p.x + w, p.y + h), a1(p.x + UI::S(3), p.y + UI::S(5)), b1(q.x - UI::S(3), q.y - UI::S(5));
    if(on) VChamfer(dl, a1, b1, UI::S(9), kVTealDim, VAlpha(kVTeal, 0.85f), UI::S(1.5f));
    else if(enabled && ImGui::IsItemActive()) VChamfer(dl, a1, b1, UI::S(9), IM_COL32(236, 232, 225, 30), kVLine);
    else if(label && enabled) VChamfer(dl, a1, b1, UI::S(9), 0, IM_COL32(236, 232, 225, 22));
    ImU32 col = enabled ? (on ? kVTeal : kVText) : IM_COL32(110, 118, 126, 255);
    float cx = label ? p.x + h * 0.32f + isz * 0.5f : p.x + w * 0.5f;
    LIcon(dl, icon, ImVec2(cx, p.y + h * 0.5f), isz * 0.75f, col);
    if(label) dl->AddText(ImVec2(cx + isz * 0.5f + h * 0.22f, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), col, label);
    ImGui::PopID();
    return hit && enabled;
}

// framed "input" box that opens the game keyboard
static bool LInputBox(const char* id, const char* value, const char* placeholder, float w, float h, int icon = -1)
{
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hit = ImGui::InvisibleButton("##ib", ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // 3.63: Valorant field - navy glass, chamfered, a teal underline while touched
    VChamfer(dl, p, ImVec2(p.x + w, p.y + h), UI::S(10), IM_COL32(10, 18, 26, 150), ImGui::IsItemActive() ? VAlpha(kVTeal, 0.9f) : IM_COL32(236, 232, 225, 40), UI::S(1.5f));
    dl->AddLine(ImVec2(p.x + UI::S(12), p.y + h - UI::S(1.5f)), ImVec2(p.x + w - UI::S(14), p.y + h - UI::S(1.5f)), ImGui::IsItemActive() ? kVTeal : IM_COL32(236, 232, 225, 18), UI::S(2));
    float tx = p.x + UI::S(18);
    if(icon >= 0) { LIcon(dl, icon, ImVec2(p.x + UI::S(30), p.y + h * 0.5f), h * 0.26f, kVDim); tx = p.x + UI::S(56); }
    dl->PushClipRect(p, ImVec2(p.x + w - UI::S(8), p.y + h), true);
    bool empty = !value || !value[0];
    dl->AddText(ImVec2(tx, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), empty ? IM_COL32(125, 135, 145, 255) : kVText, empty ? placeholder : value);
    dl->PopClipRect();
    ImGui::PopID();
    return hit;
}


static void LImageCover(ImDrawList* dl, void* tex, ImVec2 uv, ImVec2 a, ImVec2 b, ImU32 tint)
{
    float bw = b.x - a.x, bh = b.y - a.y; if(bw <= 0 || bh <= 0) return;
    float img = 2.0f, box = bw / bh;   // 512x256
    ImVec2 u0(0, 0), u1(uv.x, uv.y);
    if(box > img) { float k = img / box; u0.y = uv.y * (1 - k) * 0.5f; u1.y = uv.y - u0.y; }
    else          { float k = box / img; u0.x = uv.x * (1 - k) * 0.5f; u1.x = uv.x - u0.x; }
    dl->AddImage((ImTextureID)tex, a, b, u0, u1, tint);
}
