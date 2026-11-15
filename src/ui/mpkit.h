// 4.13.54: the MP Launcher look (src/mp_launcher.inc / mp_social.inc) for other layers - the Vice City layer
// (src/vc/vc_gui.inc) draws its launcher, player list and About page with the same pieces as San Andreas.
// Pure ImGui: nothing here touches the San Andreas game.
#pragma once
#include "imgui.h"

namespace MPKit
{
    // icons (same numbers as the launcher's IC_* list; checked with static_assert in mp.cpp)
    enum { IC_PLAY = 0, IC_PLUS, IC_TRASH, IC_REFRESH, IC_GEAR, IC_SLIDERS, IC_INFO, IC_CLOSE, IC_GLOBE, IC_HEART,
           IC_BOX, IC_USERS, IC_BAN, IC_LIST, IC_SEARCH, IC_CHAT, IC_USER, IC_STOP, IC_REGION };
    // colours
    ImU32 Teal(); ImU32 Text(); ImU32 Dim(); ImU32 Line(); ImU32 Green(); ImU32 Red(); ImU32 Dark();
    void  Glass(ImDrawList* dl, ImVec2 a, ImVec2 b);
    void  Pill(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 fill, ImU32 border = 0);
    void  PushWidgetStyle();
    void  PopWidgetStyle();
    void  Icon(ImDrawList* dl, int icon, ImVec2 c, float r, ImU32 col);
    bool  ToolButton(const char* id, int icon, const char* label, float h, bool enabled = true, bool on = false);
    bool  SideItem(const char* id, int icon, const char* label, bool on, float w, float h);
    void  GroupLabel(const char* t);
    // style: 0 outline, 1 blue (main), 2 red, 3 red outline, 4 disabled chip
    bool  Btn(const char* id, const char* label, float w, float h, int style, bool enabled = true, int icon = -1);
    void  AboutPage(float W);   // the launcher's About page (author, links, credit notice)
}
