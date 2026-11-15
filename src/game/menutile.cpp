// MP 4.13.52: the [MultiPlayer] tile is a REAL tile of the game's main menu now (next to Resume / Start Game /
// Settings), drawn and touched by the game itself - no ImGui label / overlay button any more.
//
// How: MainMenuScreen::AddAllItems (the game fills the title / pause menu tiles there) is hooked; after the
// game's own tiles one more FlowScreen button is appended: { RwTexture* icon, const char* GXT key, callback }.
// It is only added when the screen is the TITLE menu (FlowScreen::m_bIsPauseScreen == 0 and no game loaded):
// the pause menu in a game never gets it. The label is the GXT key "MP_MPLAY", answered by a CText::Get hook
// ("MultiPlayer"); the icon is MP's two-people glyph (mp_menu_png.h) turned into an RwTexture once.
//
// GTA SA 2.00 armeabi-v7a layout (read from SAUtils 1.4.1's own AddSettingsButton / MainMenuAddItems code):
//   FlowScreen +0x0D  bool m_bIsPauseScreen
//   FlowScreen +0x3C  OSArray<FlowScreenButton> items: u32 alloc, u32 count, FlowScreenButton* data
//   FlowScreenButton (12 bytes): RwTexture* tex, const char* name, MobileMenu* (*onPressed)()
//   RwTexture +0x54   refCount (the screen drops one ref per tile when it is destroyed)
// Other builds (SA 2.10, 64-bit) keep MP's fallback tile (mp_menu.inc), the layout there is not verified.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <new>
#include "game.h"
#include "../../imgui/rw_glue.h"
#include "../../imgui/stb_image.h"
#include "mp_menu_png.h"

void InitRenderWareFunctions();

namespace
{
const char kTileKey[] = "MP_MPLAY";
const char kTileText[] = "MultiPlayer";

#ifdef AML32
struct FsBtn { void* tex; const char* name; void* (*onPressed)(); };
static_assert(sizeof(FsBtn) == 12, "FlowScreenButton (SA 2.00 32-bit)");
const int kOfsPause = 0x0D, kOfsItems = 0x3C, kOfsTexRef = 0x54;
#endif

void      (*AddAllItems_orig)(uint8_t*) = nullptr;
uint16_t* (*TextGet_orig)(void*, const char*) = nullptr;
void*     (*TexCreate)(RwRaster*) = nullptr;
void      (*s_fnTap)(void*) = nullptr;
bool      (*s_fnWanted)() = nullptr;
void*     s_pTex = nullptr;
bool      s_bTexTried = false, s_bOn = false;
uint16_t  s_Text[sizeof(kTileText)];
int       s_nAdds = 0;

void* MakeTileTexture()
{
    if(s_bTexTried) return s_pTex;
    s_bTexTried = true;
    if(!RwImageCreate || !RwRasterCreate) InitRenderWareFunctions();
    if(!TexCreate || !RwImageCreate || !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage || !RwImageDestroy)
    {
        logger->Error("Main menu tile: RenderWare functions missing - no icon");
        return nullptr;
    }
    int w = 0, h = 0, c = 0;
    unsigned char* px = stbi_load_from_memory(kMpMenuPng, (int)sizeof(kMpMenuPng), &w, &h, &c, 4);
    if(!px) return nullptr;
    RwImage* img = RwImageCreate(w, h, 32);
    if(!img) { stbi_image_free(px); return nullptr; }
    RwImageAllocatePixels(img);
    if(!img->cpPixels || img->stride < w * 4) { RwImageDestroy(img); stbi_image_free(px); return nullptr; }
    for(int y = 0; y < h; ++y) memcpy(img->cpPixels + (size_t)y * img->stride, px + (size_t)y * w * 4, (size_t)w * 4);
    stbi_image_free(px);
    RwInt32 rw = 0, rh = 0, rd = 0, rf = 0;
    RwImageFindRasterFormat(img, 4 /* rwRASTERTYPETEXTURE */, &rw, &rh, &rd, &rf);
    RwRaster* r = (rw == w && rh == h) ? RwRasterCreate(rw, rh, rd, rf) : nullptr;
    if(r) r = RwRasterSetFromImage(r, img);
    RwImageDestroy(img);
    if(!r) return nullptr;
    s_pTex = TexCreate(r);
    if(!s_pTex) { if(RwRasterDestroy) RwRasterDestroy(r); return nullptr; }
#ifdef AML32
    char* name = (char*)s_pTex + 4 * sizeof(void*);   // RwTexture::name[32]
    strcpy(name, "mp_menu_mplay"); name[32] = 0;       // mask[0]
    *(uint32_t*)((uint8_t*)s_pTex + 0x50) = 2u;        // filterAddressing: linear
#endif
    return s_pTex;
}

// the game calls this when the tile is touched (menu thread = game thread)
void* OnTilePressed()
{
    if(Game::LocalPed() > 0) return nullptr;   // never from a loaded game
    logger->Info("Main menu: [MultiPlayer] tile (game menu) tapped");
    if(s_fnTap) s_fnTap(nullptr);
    return nullptr;
}

uint16_t* HookTextGet(void* self, const char* key)
{
    if(key && key[0] == 'M' && key[1] == 'P' && key[2] == '_' && !strcmp(key, kTileKey)) return s_Text;
    return TextGet_orig(self, key);
}

void HookAddAllItems(uint8_t* self)
{
    AddAllItems_orig(self);
#ifdef AML32
    if(!self || !s_fnTap) return;
    if(self[kOfsPause] != 0 || Game::LocalPed() > 0) return;   // pause menu / a game is loaded: no tile
    if(s_fnWanted && !s_fnWanted()) return;
    uint32_t& alloc = *(uint32_t*)(self + kOfsItems);
    uint32_t& count = *(uint32_t*)(self + kOfsItems + 4);
    FsBtn*&   data  = *(FsBtn**)(self + kOfsItems + 8);
    if(count > 64 || alloc > 256 || alloc < count || (count && !data))
    {
        logger->Error("Main menu tile: unexpected menu layout (alloc %u, count %u) - tile not added", alloc, count);
        return;
    }
    for(uint32_t i = 0; i < count; ++i) if(data[i].name == kTileKey) return;   // already there
    void* tex = MakeTileTexture();
    if(!tex) return;
    if(count >= alloc)
    {   // grow like OSArray::AllocNew (the game frees the buffer with delete[])
        const uint32_t na = (4 * count + 4) / 3 + 3;
        FsBtn* nd = (FsBtn*)::operator new[](na * sizeof(FsBtn), std::nothrow);
        if(!nd) return;
        if(data) { memcpy(nd, data, count * sizeof(FsBtn)); ::operator delete[]((void*)data); }
        data = nd; alloc = na;
    }
    ++*(int32_t*)((uint8_t*)tex + kOfsTexRef);
    FsBtn& b = data[count++];
    b.tex = tex; b.name = kTileKey; b.onPressed = &OnTilePressed;
    if(s_nAdds++ < 3) logger->Info("Main menu: [MultiPlayer] tile added to the game's title menu (tile %u)", count);
#endif
}
} // namespace

bool Game::InstallMainMenuTile(void (*onTap)(void*), bool (*wanted)())
{
    s_fnTap = onTap; s_fnWanted = wanted;
    if(s_bOn) return true;
#ifndef AML32
    logger->Info("Main menu tile: 64-bit game - MP's fallback tile is used");
    return false;
#else
    extern uintptr_t pGameLib;
    void* h = aml->GetLibHandle("libGTASA.so");
    if(!h || !pGameLib) return false;
    // the same check SAUtils makes: GTA SA 2.00 (the 32-bit 2.10 build has another menu layout)
    if(*(uint32_t*)(pGameLib + 0x202020) != 0xE8BDB001) { logger->Info("Main menu tile: not GTA SA 2.00 - MP's fallback tile is used"); return false; }
    void* add = (void*)aml->GetSym(h, "_ZN14MainMenuScreen11AddAllItemsEv");
    void* get = (void*)aml->GetSym(h, "_ZN5CText3GetEPKc");
    *(void**)&TexCreate = (void*)aml->GetSym(h, "_Z15RwTextureCreateP8RwRaster");
    if(!TexCreate) *(void**)&TexCreate = (void*)aml->GetSym(h, "RwTextureCreate");
    if(!add || !get || !TexCreate)
    {
        logger->Error("Main menu tile: AddAllItems %s, CText::Get %s, RwTextureCreate %s - MP's fallback tile is used",
            add ? "yes" : "no", get ? "yes" : "no", TexCreate ? "yes" : "no");
        return false;
    }
    for(size_t i = 0; i < sizeof(kTileText); ++i) s_Text[i] = (uint8_t)kTileText[i];
    aml->Hook(get, (void*)&HookTextGet, (void**)&TextGet_orig);
    if(!TextGet_orig) { logger->Error("Main menu tile: CText::Get hook failed"); return false; }
    aml->Hook(add, (void*)&HookAddAllItems, (void**)&AddAllItems_orig);
    if(!AddAllItems_orig) { logger->Error("Main menu tile: AddAllItems hook failed"); return false; }
    s_bOn = true;
    logger->Info("Main menu: [MultiPlayer] is a native tile of the game's title menu (MainMenuScreen::AddAllItems)");
    return true;
#endif
}
bool Game::MainMenuTileNative() { return s_bOn; }
