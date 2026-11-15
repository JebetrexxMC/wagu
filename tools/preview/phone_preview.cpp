// Offscreen preview of src/mp_phone.inc (4.13.59): the MP phone on every server - contacts, calling, incoming, on a call.
// The real drawing + call code with game stubs, software rasterized.
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <vector>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include "ui/ui_common.h"
#include "net/protocol.h"
#include "arial.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
static int W = 3200, H = 1440;
static ImFont* g_Font;
float UI::S(float px) { return px * H / 1080.0f; }
ImFont* UI::Font() { return g_Font; }
void UI::OutlinedText(ImDrawList* dl, float size, ImVec2 p, ImU32 col, const char* text, float alphaMul)
{
    ImFont* f = Font(); const float o = fmaxf(1.0f, size * 0.06f);
    ImU32 black = IM_COL32(0, 0, 0, (int)(255 * alphaMul));
    ImU32 c = (col & 0x00FFFFFF) | ((ImU32)(((col >> 24) & 255) * alphaMul) << 24);
    dl->AddText(f, size, ImVec2(p.x - o, p.y), black, text); dl->AddText(f, size, ImVec2(p.x + o, p.y), black, text);
    dl->AddText(f, size, ImVec2(p.x, p.y - o), black, text); dl->AddText(f, size, ImVec2(p.x, p.y + o), black, text);
    dl->AddText(f, size, p, c, text);
}
// ---- stubs for the game side (the phone code below is the real src/mp_phone.inc) ----
static uint64_t g_Now = 100000;
static uint64_t MP_NowMs() { return g_Now; }
#define COL_SYSTEM  0xA9C4E4FF
#define COL_GREEN   0x33CC55FF
#define COL_RED     0xFF4444FF
#define COL_YELLOW  0xFFDD33FF
static void SysMsg(uint32_t, const char*, ...) {}
namespace Game { static bool IsCharInAnyCar(int) { return false; } static bool IsPaused() { return false; } static void TaskUsePhone(int, bool) {} static void* PedPtr(int) { return nullptr; } }
namespace FloatMenu { static bool IsModal() { return false; } }
namespace HUD { static void* Texture(const char*, ImVec2* uv) { *uv = ImVec2(1, 1); return nullptr; } }   // the drawn phone icon
static int g_ItemsTex = 2; static int g_ItemsW = 1, g_ItemsH = 1; static unsigned char g_ItemsPx[4] = { 255, 255, 255, 255 }; static unsigned char* g_Items = g_ItemsPx;
struct { bool active; char name[MP_NAME_LEN]; uint32_t color; } players[MP_MAX_PLAYERS];
static uint8_t myId = 0;
static struct { int sent = 0; void Send(const void*, int) { ++sent; } int GetServerBuild() const { return MP_BUILD; } } client;
static bool IsConnected() { return true; }
static bool RpOnServer() { return false; }
static bool SampDialogOpen() { return false; }
enum { RPS_CLICK = 0, RPS_PHONE, RPS_CARD };
static void RpClick(int) {}
static ImU32 RpCol(uint32_t rgba, float a) { return UI::FromRGBA(rgba, a < 0 ? 0 : (a > 1 ? 1 : a)); }
static float RpTextW(float fs, const char* t) { return UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t).x; }
static void BcwBeginOverlay(const char* id, ImVec2 pos, ImVec2 size, float bgAlpha)
{
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, bgAlpha));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(18));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(18), UI::S(14)));
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}
static void BcwEndOverlay() { ImGui::End(); ImGui::PopStyleVar(2); ImGui::PopStyleColor(); }
#define RP_SKY 0x0EA5E9FF
// the phone widgets of src/mp_rp_phone.inc (lines between "small widgets" and "games"), pulled in as they are
#include "phone_widgets.inc"
#include "mp_phone.inc"

// ---- rasterizer (font alpha atlas = tex 1, rp_items.png RGBA = tex 2), with clip rects ----
static std::vector<float> fb; static unsigned char* ftex; static int ftw, fth;
static void Raster(ImDrawData* dd)
{
    for(int n = 0; n < dd->CmdListsCount; ++n)
    {
        const ImDrawList* cl = dd->CmdLists[n];
        const ImDrawVert* vb = cl->VtxBuffer.Data; const ImDrawIdx* ib = cl->IdxBuffer.Data;
        for(const ImDrawCmd& cmd : cl->CmdBuffer)
        {
            int texId = (int)(intptr_t)cmd.GetTexID();
            for(unsigned e = 0; e < cmd.ElemCount; e += 3)
            {
                const ImDrawVert& a = vb[ib[cmd.IdxOffset + e] + cmd.VtxOffset];
                const ImDrawVert& b = vb[ib[cmd.IdxOffset + e + 1] + cmd.VtxOffset];
                const ImDrawVert& c = vb[ib[cmd.IdxOffset + e + 2] + cmd.VtxOffset];
                float minx = fmaxf(cmd.ClipRect.x, fminf(a.pos.x, fminf(b.pos.x, c.pos.x))), maxx = fminf(cmd.ClipRect.z, fmaxf(a.pos.x, fmaxf(b.pos.x, c.pos.x)));
                float miny = fmaxf(cmd.ClipRect.y, fminf(a.pos.y, fminf(b.pos.y, c.pos.y))), maxy = fminf(cmd.ClipRect.w, fmaxf(a.pos.y, fmaxf(b.pos.y, c.pos.y)));
                int x0 = (int)fmaxf(0, floorf(minx)), x1 = (int)fminf(W - 1, ceilf(maxx) - 1);
                int y0 = (int)fmaxf(0, floorf(miny)), y1 = (int)fminf(H - 1, ceilf(maxy) - 1);
                float area = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) - (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x);
                if(fabsf(area) < 1e-6f) continue;
                for(int y = y0; y <= y1; ++y) for(int x = x0; x <= x1; ++x)
                {
                    float px = x + 0.5f, py = y + 0.5f;
                    float w0 = ((b.pos.x - px) * (c.pos.y - py) - (b.pos.y - py) * (c.pos.x - px)) / area;
                    float w1 = ((c.pos.x - px) * (a.pos.y - py) - (c.pos.y - py) * (a.pos.x - px)) / area;
                    float w2 = 1 - w0 - w1;
                    if(w0 < 0 || w1 < 0 || w2 < 0) continue;
                    float u = w0 * a.uv.x + w1 * b.uv.x + w2 * c.uv.x, v = w0 * a.uv.y + w1 * b.uv.y + w2 * c.uv.y;
                    float t[4] = { 1, 1, 1, 1 };
                    if(texId == 2)
                    {
                        int tx = (int)(u * g_ItemsW), ty = (int)(v * g_ItemsH); tx = tx < 0 ? 0 : tx >= g_ItemsW ? g_ItemsW - 1 : tx; ty = ty < 0 ? 0 : ty >= g_ItemsH ? g_ItemsH - 1 : ty;
                        for(int k = 0; k < 4; ++k) t[k] = g_Items[(ty * g_ItemsW + tx) * 4 + k] / 255.0f;
                    }
                    else
                    {
                        int tx = (int)(u * ftw), ty = (int)(v * fth); tx = tx < 0 ? 0 : tx >= ftw ? ftw - 1 : tx; ty = ty < 0 ? 0 : ty >= fth ? fth - 1 : ty;
                        t[3] = ftex[ty * ftw + tx] / 255.0f;
                    }
                    float col[4];
                    for(int k = 0; k < 4; ++k)
                    {
                        float ca = ((a.col >> (8 * k)) & 255) / 255.0f, cb = ((b.col >> (8 * k)) & 255) / 255.0f, cc = ((c.col >> (8 * k)) & 255) / 255.0f;
                        col[k] = (w0 * ca + w1 * cb + w2 * cc) * t[k];
                    }
                    float al = col[3];
                    float* d = &fb[(y * W + x) * 3];
                    for(int k = 0; k < 3; ++k) d[k] = d[k] * (1 - al) + col[k] * al;
                }
            }
        }
    }
}
int main(int argc, char** argv)
{
    // build: python3 tools/preview/make_gifs.py phone   (or see make_gifs.py: phone())
    const char* bg = argv[1]; const char* out = argv[2]; int mode = atoi(argv[3]);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.DisplaySize = ImVec2(W, H); io.IniFilename = nullptr;
    ImFontConfig fc; fc.FontDataOwnedByAtlas = false; fc.OversampleH = 3; fc.OversampleV = 2;
    g_Font = io.Fonts->AddFontFromMemoryTTF((void*)arialData, sizeof(arialData), 64.0f, &fc);
    io.Fonts->GetTexDataAsAlpha8(&ftex, &ftw, &fth);
    io.Fonts->SetTexID((ImTextureID)1);
    io.FontGlobalScale = 0.5f;
    int n, bw, bh; unsigned char* bgp = stbi_load(bg, &bw, &bh, &n, 3);
    fb.resize(W * H * 3);
    for(int y = 0; y < H; ++y) for(int x = 0; x < W; ++x) for(int k = 0; k < 3; ++k)
        fb[(y * W + x) * 3 + k] = bgp[((y * bh / H) * bw + x * bw / W) * 3 + k] / 255.0f;
    const char* names[] = { "Senji_Macase", "Carl_Johnson", "Lia_Moreno", "Ken_Rosenberg", "Cesar_Vialpando", "Big_Smoke" };
    const uint32_t cols[] = { 0xFFFFFFFF, 0x33CC55FF, 0xF472B6FF, 0xFFDD33FF, 0x38BDF8FF, 0xFB923CFF };
    for(int i = 0; i < 6; ++i) { players[i].active = true; MP_CopyStr(players[i].name, names[i], MP_NAME_LEN); players[i].color = cols[i]; }
    // the same packets the server sends (see src/net/server.cpp HandleCall), fed to the real PhoneOnCall()
    auto pkt = [](uint8_t act, int other) { PktCall c; c.type = PKT_CALL; c.action = act; c.other = (uint8_t)other; PhoneOnCall(c); };
    switch(mode)
    {
        case 0: PhoneOpen(); break;                                                                  // contacts
        case 1: PhoneDial(1); pkt(CALL_RINGING, 1); g_Now += 1700; break;                            // calling Carl
        case 2: myId = 1; pkt(CALL_RING, 0); g_Now += 900; break;                                    // Carl's phone rings
        case 3: PhoneDial(1); pkt(CALL_LINKED, 1); g_Now += 42000; break;                            // on the call 0:42
        case 4: PhoneDial(1); pkt(CALL_LINKED, 1); g_Now += 95000; pkt(CALL_ENDED, 1); g_Now += 300; break;   // hung up
        case 5: myId = 1; pkt(CALL_RING, 0); s_Ph.open = false; g_Now += 350; break;                 // ringing, phone put away
        case 6: PhoneDial(2); pkt(CALL_BUSY, 2); g_Now += 300; break;                                // Lia is busy
    }
    io.MousePos = ImVec2(-1, -1);
    for(int f = 0; f < 2; ++f) { io.DeltaTime = 1 / 30.f; ImGui::NewFrame(); PhoneRender(); ImGui::Render(); }
    Raster(ImGui::GetDrawData());
    FILE* o = fopen(out, "wb"); fprintf(o, "P6 %d %d 255\n", W, H);
    for(int i = 0; i < W * H * 3; ++i) { float v = fb[i]; if(v > 1) v = 1; fputc((int)(v * 255), o); }
    fclose(o);
    return 0;
}
