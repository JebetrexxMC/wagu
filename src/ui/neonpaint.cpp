// MP 4.13.28: PCMenu-style drawing for the neon title menu (see neonpaint.h).
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <atomic>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include "neonpaint.h"
#include "i18n.h"
#include <unordered_map>
#define STBI_NO_STDIO
#include "../../imgui/stb_image.h"   // 5.01.08: declarations only (the code is in hud.cpp)

namespace
{
    struct V2 { float x, y; };
    typedef void  (*DrawSpriteFn)(void* tex, V2* verts, V2* uvs, NeonPaint::Col* cols);
    typedef void  (*DrawQuadFn)(V2* verts, NeonPaint::Col* cols);
    typedef int   (*RenderStateSetFn)(int state, void* value);
    typedef void* (*PngReadFn)(const char*);
    typedef void* (*FindRasterFmtFn)(void* img, int type, int* w, int* h, int* d, int* flags);
    typedef void* (*RasterCreateFn)(int w, int h, int d, int flags);
    typedef void* (*RasterFromImgFn)(void* raster, void* img);
    typedef void* (*TexCreateFn)(void* raster);
    typedef void  (*ImgDestroyFn)(void* img);

    DrawSpriteFn     fnDrawSprite = nullptr;
    DrawQuadFn       fnDrawQuad = nullptr;
    RenderStateSetFn fnRenderState = nullptr;
    PngReadFn        fnPngRead = nullptr;
    FindRasterFmtFn  fnFindRasterFmt = nullptr;
    RasterCreateFn   fnRasterCreate = nullptr;
    RasterFromImgFn  fnRasterFromImg = nullptr;
    TexCreateFn      fnTexCreate = nullptr;
    ImgDestroyFn     fnImgDestroy = nullptr;
    typedef int   (*TexDestroyFn)(void* tex);
    TexDestroyFn     fnTexDestroy = nullptr;   // 5.02.77: the last language's letters are let go
    typedef void* (*ImgCreateFn)(int w, int h, int d);
    typedef void* (*ImgAllocFn)(void* img);
    ImgCreateFn      fnImgCreate = nullptr;   // 5.01.08: MP's own PNG decoder (stb_image) -> RwImage
    ImgAllocFn       fnImgAlloc = nullptr;
    int*             s_pRsGlobal = nullptr;
    bool             s_bInit = false, s_bLoaded = false, s_bFailed = false;

    enum { RS_TEXADDRESS = 2, RS_ZTEST = 6, RS_ZWRITE = 8, RS_TEXFILTER = 9, RS_SRCBLEND = 10, RS_DESTBLEND = 11, RS_VERTEXALPHA = 12 };

    struct NamedTex { char name[32]; NeonPaint::Tex t; };
    NamedTex s_Tex[80]; int s_nTex = 0;

    struct Glyph { float u0, v0, u1, v1; int w, h, xo, yo; float adv; bool ok; };
    struct Atlas { const NeonPaint::Tex* tex = nullptr; int size = 64, asc = 50; Glyph g[128]; };
    Atlas s_Font[NeonPaint::F_COUNT];
    // 5.02.77: the letters of the MP language (src/embed/neonintl_blob.S): ASCII + what its table uses, one font
    struct IntlAtlas { const NeonPaint::Tex* tex = nullptr; int size = 40, asc = 40; std::unordered_map<uint32_t, Glyph> g; std::string code; bool tried = false; };
    IntlAtlas s_Intl; uint32_t s_nIntlGen = 0xFFFFFFFFu;

    // touches
    std::mutex s_TouchMx;
    int   s_nFinger = -1;
    bool  s_bDown = false, s_bUp = false;
    float s_fX = 0, s_fY = 0, s_fPX = 0, s_fPY = 0, s_fUX = 0, s_fUY = 0;
    // the frame's snapshot
    bool  f_bDown = false, f_bTap = false;
    float f_fX = 0, f_fY = 0, f_fPX = 0, f_fPY = 0, f_fUX = 0, f_fUY = 0;
    float s_fMove = 0, f_fMove = 0, f_fLY = 0;   // 5.01.11: how far the finger went (a drag is no tap), the last frame's y
    bool  f_bWasDown = false;

    bool LoadTex(const std::string& dir, const char* name);
    // 5.02.43: a texture straight from memory (the backgrounds built into libMP, src/embed/menubg_blob.S): MP's own
    // decoder (stb_image, PNG / JPEG) into a RwImage, then a raster + texture - no file is read or written
    bool LoadMem(const char* name, const unsigned char* data, size_t size)
    {
        for(int i = 0; i < s_nTex; ++i) if(!strcmp(s_Tex[i].name, name)) return true;
        if(s_nTex >= (int)(sizeof(s_Tex) / sizeof(s_Tex[0])) || !data || size < 16 || !fnImgCreate || !fnImgAlloc) return false;
        int w = 0, h = 0, c = 0;
        unsigned char* px = stbi_load_from_memory(data, (int)size, &w, &h, &c, 4);
        if(!px || w <= 0 || h <= 0) { if(px) stbi_image_free(px); logger->Error("Neon menu: built-in %s could not be decoded", name); return false; }
        void* img = fnImgCreate(w, h, 32);
        bool ok = false;
        if(img)
        {
            fnImgAlloc(img);
            struct RwImg { int flags, width, height, depth, stride; unsigned char* pixels; void* pal; };
            RwImg* ri = (RwImg*)img;
            if(ri->pixels && ri->stride >= w * 4) { for(int y = 0; y < h; ++y) memcpy(ri->pixels + (size_t)y * ri->stride, px + (size_t)y * w * 4, (size_t)w * 4); ok = true; }
        }
        stbi_image_free(px);
        if(!ok) { if(img) fnImgDestroy(img); logger->Error("Neon menu: built-in %s - no image (out of memory?)", name); return false; }
        void* tex = nullptr;
        int rw = 0, rh = 0, d = 0, flags = 0;
        fnFindRasterFmt(img, 4 /*rwRASTERTYPETEXTURE*/, &rw, &rh, &d, &flags);
        if(void* r = fnRasterCreate(rw, rh, d, flags)) { fnRasterFromImg(r, img); tex = fnTexCreate(r); }
        fnImgDestroy(img);
        if(!tex) { logger->Error("Neon menu: built-in %s - no texture (out of memory?)", name); return false; }
        NamedTex& n = s_Tex[s_nTex++];
        snprintf(n.name, sizeof(n.name), "%s", name); n.t.t = tex; n.t.w = w; n.t.h = h;
        logger->Info("Neon menu: built-in %s (%dx%d, %u bytes) loaded from libMP", name, w, h, (unsigned)size);
        return true;
    }
    bool LoadTexOld(const std::string& dir, const char* name)
    {
        if(s_nTex >= (int)(sizeof(s_Tex) / sizeof(s_Tex[0]))) return false;
        std::string p = dir + name;
        struct stat st; if(stat(p.c_str(), &st) != 0) { logger->Error("Neon menu: %s missing", p.c_str()); return false; }
        logger->Info("Neon menu: loading %s (%ld bytes)", name, (long)st.st_size);   // 4.13.35: breadcrumb (once per file)
        // 5.01.08: like PCMenu - the game's RtPNGImageRead with the full path, then with the path relative to the game's
        // files/ folder (the game's file layer refused the full Android_unprotected path on some phones: every PNG
        // failed), then MP's own PNG decoder (stb_image) into a RwImage, so no texture depends on the game's reader
        void* img = fnPngRead(p.c_str());
        const char* how = "game reader";
        if(!img)
        {
            size_t f = p.find("/files/");
            if(f != std::string::npos)
            {
                std::string rel = p.substr(f + 7);
                while(!rel.empty() && rel[0] == '/') rel.erase(0, 1);
                img = fnPngRead(rel.c_str());
                how = "game reader, relative path";
            }
        }
        if(!img && fnImgCreate && fnImgAlloc)
        {
            std::string data;
            if(FILE* f = fopen(p.c_str(), "rb")) { char b[16384]; size_t r; while((r = fread(b, 1, sizeof b, f)) > 0) data.append(b, r); fclose(f); }
            int w = 0, h = 0, c = 0;
            unsigned char* px = data.empty() ? nullptr : stbi_load_from_memory((const unsigned char*)data.data(), (int)data.size(), &w, &h, &c, 4);
            if(px && w > 0 && h > 0)
            {
                if((img = fnImgCreate(w, h, 32)) != nullptr)
                {
                    fnImgAlloc(img);
                    struct RwImg { int flags, width, height, depth, stride; unsigned char* pixels; void* pal; };
                    RwImg* ri = (RwImg*)img;
                    if(ri->pixels && ri->stride >= w * 4) { for(int y = 0; y < h; ++y) memcpy(ri->pixels + (size_t)y * ri->stride, px + (size_t)y * w * 4, (size_t)w * 4); how = "MP decoder"; }
                    else { fnImgDestroy(img); img = nullptr; }
                }
            }
            if(px) stbi_image_free(px);
        }
        if(!img) { logger->Error("Neon menu: %s is not a readable PNG (8-bit RGBA, not interlaced)", p.c_str()); return false; }
        if(strcmp(how, "game reader")) logger->Info("Neon menu: %s read by the %s", name, how);
        const int iw = ((int*)img)[1], ih = ((int*)img)[2];   // RwImage: flags, width, height, depth, stride, pixels
        void* tex = nullptr;
        int w = 0, h = 0, d = 0, flags = 0;
        fnFindRasterFmt(img, 4 /*rwRASTERTYPETEXTURE*/, &w, &h, &d, &flags);
        if(void* r = fnRasterCreate(w, h, d, flags)) { fnRasterFromImg(r, img); tex = fnTexCreate(r); }
        fnImgDestroy(img);
        if(!tex) { logger->Error("Neon menu: %s - no texture (out of memory?)", p.c_str()); return false; }
        NamedTex& n = s_Tex[s_nTex++];
        snprintf(n.name, sizeof(n.name), "%s", name); n.t.t = tex; n.t.w = iw; n.t.h = ih;
        return true;
    }
}
namespace NeonEmbed { std::string Builtin(const std::string& dir, const char* name); }
namespace {
    // 4.13.34: your own PNG in neon/ first; when it is missing or the game can't read it (a palette / 16-bit /
    // interlaced PNG, a JPEG renamed to .png, ...) the built-in one, so the neon / mp menus always show
    std::string s_szErr;   // 5.01.08: what did not load (neon_mode_status.txt)
    bool LoadTex(const std::string& dir, const char* name)
    {
        for(int i = 0; i < s_nTex; ++i) if(!strcmp(s_Tex[i].name, name)) return true;   // 5.01.08: loaded on an earlier try
        if(LoadTexOld(dir, name)) return true;
        const std::string b = NeonEmbed::Builtin(dir, name);
        if(b.empty()) return false;
        const size_t cut = b.rfind('/');
        const bool ok = LoadTexOld(b.substr(0, cut + 1), name);
        logger->Info("Neon menu: %s%s -> built-in %s %s", dir.c_str(), name, name, ok ? "used" : "failed too");
        return ok;
    }
    bool LoadFont(const std::string& dir, int idx, const char* base)
    {
        std::string tp = dir + base + ".txt";
        FILE* f = fopen(tp.c_str(), "rb");
        if(!f) { logger->Error("Neon menu: %s missing", tp.c_str()); return false; }
        Atlas& a = s_Font[idx]; memset(a.g, 0, sizeof(a.g));
        char line[160]; int size = 0, asc = 0, desc = 0, cap = 0, aw = 0, ah = 0;
        if(!fgets(line, sizeof(line), f) || sscanf(line, "FONT %d %d %d %d %d %d", &size, &asc, &desc, &cap, &aw, &ah) != 6 || aw <= 0 || ah <= 0)
        { fclose(f); logger->Error("Neon menu: %s is not a font description", tp.c_str()); return false; }
        a.size = size; a.asc = asc;
        while(fgets(line, sizeof(line), f))
        {
            int code, x, y, w, h, xo, yo; float adv;
            if(sscanf(line, "G %d %d %d %d %d %d %d %f", &code, &x, &y, &w, &h, &xo, &yo, &adv) == 8 && code >= 32 && code < 128)
            {
                Glyph& g = a.g[code];
                g.u0 = (float)x / aw; g.v0 = (float)y / ah; g.u1 = (float)(x + w) / aw; g.v1 = (float)(y + h) / ah;
                g.w = w; g.h = h; g.xo = xo; g.yo = yo; g.adv = adv; g.ok = true;
            }
        }
        fclose(f);
        char png[48]; snprintf(png, sizeof(png), "%s.png", base);
        if(!LoadTex(dir, png)) return false;
        a.tex = NeonPaint::T(png);
        return a.tex != nullptr;
    }
    inline NeonPaint::Col Lerp(NeonPaint::Col a, NeonPaint::Col b, float t)
    {
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return { (uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t), (uint8_t)(a.a + (b.a - a.a) * t) };
    }
}

bool NeonPaint::Init(void* h)
{
    if(!h) return false;
    *(void**)&fnDrawSprite    = (void*)aml->GetSym(h, "_ZN10MobileMenu10DrawSpriteEP9RwTextureP9CVector2DS3_P5CRGBA");
    *(void**)&fnDrawQuad      = (void*)aml->GetSym(h, "_ZN10MobileMenu8DrawQuadEP9CVector2DP5CRGBA");
    *(void**)&fnRenderState   = (void*)aml->GetSym(h, "_Z16RwRenderStateSet13RwRenderStatePv");
    *(void**)&fnPngRead       = (void*)aml->GetSym(h, "_Z14RtPNGImageReadPKc");
    *(void**)&fnFindRasterFmt = (void*)aml->GetSym(h, "_Z23RwImageFindRasterFormatP7RwImageiPiS1_S1_S1_");
    *(void**)&fnRasterCreate  = (void*)aml->GetSym(h, "_Z14RwRasterCreateiiii");
    *(void**)&fnRasterFromImg = (void*)aml->GetSym(h, "_Z20RwRasterSetFromImageP8RwRasterP7RwImage");
    *(void**)&fnTexCreate     = (void*)aml->GetSym(h, "_Z15RwTextureCreateP8RwRaster");
    *(void**)&fnImgDestroy    = (void*)aml->GetSym(h, "_Z14RwImageDestroyP7RwImage");
    *(void**)&fnImgCreate     = (void*)aml->GetSym(h, "_Z13RwImageCreateiii");
    *(void**)&fnImgAlloc      = (void*)aml->GetSym(h, "_Z21RwImageAllocatePixelsP7RwImage");
    *(void**)&fnTexDestroy    = (void*)aml->GetSym(h, "_Z16RwTextureDestroyP9RwTexture");
    s_pRsGlobal = (int*)aml->GetSym(h, "RsGlobal");
    s_bInit = fnDrawSprite && fnDrawQuad && fnRenderState && fnPngRead && fnFindRasterFmt && fnRasterCreate && fnRasterFromImg && fnTexCreate && fnImgDestroy && s_pRsGlobal;
    logger->Info("Neon menu: PCMenu-style drawing %s", s_bInit ? "ready (game sprites + PNG textures)" : "NOT available (game functions missing)");
    return s_bInit;
}
bool NeonPaint::Ready() { return s_bInit && s_bLoaded; }
bool NeonPaint::Failed() { return s_bFailed; }
// RsGlobal { const char* appName; int maximumWidth, maximumHeight; ... }: +4/+8 on 32-bit, +8/+12 on 64-bit (PCMenu)
float NeonPaint::W() { return s_pRsGlobal ? (float)*(int*)((char*)s_pRsGlobal + sizeof(void*)) : 1280.0f; }
float NeonPaint::H() { return s_pRsGlobal ? (float)*(int*)((char*)s_pRsGlobal + sizeof(void*) + 4) : 720.0f; }

namespace NeonEmbed { int Ensure(const std::string& dir); }   // 4.13.33 (src/ui/neon_embed.cpp)
extern "C" {   // 5.02.43: src/embed/menubg_blob.S
    extern const unsigned char mp_menubg_0[], mp_menubg_0_end[], mp_menubg_1[], mp_menubg_1_end[], mp_menubg_2[], mp_menubg_2_end[];
}
bool NeonPaint::Load(const char* mpDir)
{
    if(s_bLoaded) return true;
    if(s_bFailed || !s_bInit) return false;
    std::string dir = std::string(mpDir) + "/neon/";
    NeonEmbed::Ensure(dir);   // 4.13.33: missing / old files come from the copy built into MP
    static const char* const kTex[] = { "art.png", "logo.png", "shade_left.png", "shade_bottom.png", "shade_top.png", "card_identity.png",
        "card_featured.png", "dialog.png", "button.png", "pill.png", "discord.png", "arrow.png", "arrow_down.png", "dot.png", "white.png", "nb_bg.png", "nb_flags.png",
        "nb_button.png", "nb_card.png", "nb_dialog.png", "nb_pill.png" };   // 5.02.43: no nb_bg1080.png (the built-in backgrounds)
    // 5.01.08: a texture that does not load no longer turns the whole neon menu off (it is just not drawn); only no
    // font at all does. What failed goes to mp_log.txt + neon_mode_status.txt.
    s_szErr.clear();
    int texOk = 0, fontOk = 0;
    for(const char* n : kTex) { if(LoadTex(dir, n)) ++texOk; else s_szErr += std::string(s_szErr.empty() ? "" : ", ") + n; }
    static const char* const kFont[] = { "font_gothic", "font_caps", "font_ui" };
    for(int i = 0; i < F_COUNT; ++i)
    {
        if(s_Font[i].tex || LoadFont(dir, i, kFont[i])) ++fontOk;
        else s_szErr += std::string(s_szErr.empty() ? "" : ", ") + kFont[i] + ".png/.txt";
    }
    {   // 5.02.43: the three menu backgrounds, decoded from libMP itself (not from assets/MP, files/TYFMC or Host)
        const unsigned char* const b[3] = { mp_menubg_0, mp_menubg_1, mp_menubg_2 };
        const unsigned char* const e[3] = { mp_menubg_0_end, mp_menubg_1_end, mp_menubg_2_end };
        static const char* const nm[3] = { "menubg0", "menubg1", "menubg2" };
        for(int i = 0; i < 3; ++i) if(!LoadMem(nm[i], b[i], (size_t)(e[i] - b[i]))) s_szErr += std::string(s_szErr.empty() ? "" : ", ") + nm[i] + " (built-in)";
    }
    if(!fontOk)
    {
        s_bFailed = true;
        logger->Error("Neon menu: files/TYFMC/MP/neon/ could not be read (%s) - copy the TYFMC folder of the MP package again. The game's own menu is shown.", s_szErr.c_str());
        return false;
    }
    if(!s_szErr.empty()) logger->Error("Neon menu: not loaded (drawn without them): %s", s_szErr.c_str());
    s_bLoaded = true;
    logger->Info("Neon menu: %d/%d textures + %d/3 font atlases loaded from %s", texOk, (int)(sizeof(kTex) / sizeof(kTex[0])), fontOk, dir.c_str());
    return true;
}
const char* NeonPaint::LastError() { return s_szErr.c_str(); }
bool NeonPaint::Inited() { return s_bInit; }
void NeonPaint::Retry() { if(s_bFailed) { s_bFailed = false; logger->Info("Neon menu: trying the neon/ textures again"); } }

const NeonPaint::Tex* NeonPaint::T(const char* name)
{
    for(int i = 0; i < s_nTex; ++i) if(!strcmp(s_Tex[i].name, name)) return &s_Tex[i].t;
    return nullptr;
}

// 5.02.78: the server browser rows draw the bg_server.jar pictures (HUD::Texture rasters), wrapped once in an RwTexture
const NeonPaint::Tex* NeonPaint::FromRaster(void* raster, int w, int h)
{
    struct RT { void* r; Tex t; };
    static RT s_R[96]; static int s_nR = 0;
    if(!raster || !fnTexCreate) return nullptr;
    for(int i = 0; i < s_nR; ++i) if(s_R[i].r == raster) return s_R[i].t.t ? &s_R[i].t : nullptr;
    if(s_nR >= (int)(sizeof(s_R) / sizeof(s_R[0]))) return nullptr;
    RT& e = s_R[s_nR++]; e.r = raster; e.t.t = fnTexCreate(raster); e.t.w = w; e.t.h = h;
    return e.t.t ? &e.t : nullptr;
}

void NeonPaint::Begin()
{
    fnRenderState(RS_ZTEST, (void*)0);
    fnRenderState(RS_ZWRITE, (void*)0);
    fnRenderState(RS_VERTEXALPHA, (void*)1);
    fnRenderState(RS_SRCBLEND, (void*)5);   // rwBLENDSRCALPHA
    fnRenderState(RS_DESTBLEND, (void*)6);  // rwBLENDINVSRCALPHA
    fnRenderState(RS_TEXFILTER, (void*)2);  // rwFILTERLINEAR
    fnRenderState(RS_TEXADDRESS, (void*)3); // rwTEXTUREADDRESSCLAMP
}
void NeonPaint::End()
{
    fnRenderState(RS_ZTEST, (void*)1);
    fnRenderState(RS_ZWRITE, (void*)1);
}
void NeonPaint::Quad(float x0, float y0, float x1, float y1, Col c)
{
    if(const Tex* w = T("white.png")) { Img(w, x0, y0, x1, y1, c); return; }
    V2 v[4] = { {x0, y0}, {x1, y0}, {x0, y1}, {x1, y1} };
    Col cc[4] = { c, c, c, c };
    fnDrawQuad(v, cc);
}
void NeonPaint::ImgUV(const Tex* t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Col top, Col bottom)
{
    if(!t || !t->t) return;
    V2 v[4]  = { {x0, y0}, {x1, y0}, {x0, y1}, {x1, y1} };
    V2 uv[4] = { {u0, v0}, {u1, v0}, {u0, v1}, {u1, v1} };
    Col cc[4] = { top, top, bottom, bottom };
    fnDrawSprite(t->t, v, uv, cc);
}
void NeonPaint::Img(const Tex* t, float x0, float y0, float x1, float y1, Col c) { ImgUV(t, x0, y0, x1, y1, 0, 0, 1, 1, c, c); }

// ---- 5.02.77: the MP language ----
namespace NeonIntl { bool Find(const char* code, const unsigned char** png, size_t* pngN, const unsigned char** txt, size_t* txtN); }   // neon_embed.cpp
namespace
{
    void DropTex(const char* name)
    {
        for(int i = 0; i < s_nTex; ++i) if(!strcmp(s_Tex[i].name, name))
        {
            if(fnTexDestroy && s_Tex[i].t.t) fnTexDestroy(s_Tex[i].t.t);
            for(int j = i + 1; j < s_nTex; ++j) s_Tex[j - 1] = s_Tex[j];
            --s_nTex; return;
        }
    }
    // the atlas of the language in use (loaded on the render thread the first time a text is drawn after a change)
    const IntlAtlas* Intl()
    {
        const uint32_t gen = I18n::Generation();
        if(gen != s_nIntlGen)
        {
            s_nIntlGen = gen;
            const char* code = I18n::CurrentCode();
            if(s_Intl.code != code)
            {
                if(s_Intl.tex) { char old[40]; snprintf(old, sizeof(old), "intl_%s.png", s_Intl.code.c_str()); DropTex(old); }
                s_Intl.tex = nullptr; s_Intl.g.clear(); s_Intl.code = code; s_Intl.tried = false;
            }
        }
        if(s_Intl.tex) return &s_Intl;
        if(s_Intl.tried || I18n::Current() == 0 || !s_bInit) return nullptr;
        s_Intl.tried = true;
        const unsigned char *png = nullptr, *txt = nullptr; size_t pn = 0, tn = 0;
        if(!NeonIntl::Find(s_Intl.code.c_str(), &png, &pn, &txt, &tn)) { logger->Info("Neon menu: no letters built in for %s (ASCII only)", s_Intl.code.c_str()); return nullptr; }
        std::string t((const char*)txt, tn); int size = 0, asc = 0, desc = 0, cap = 0, aw = 0, ah = 0;
        size_t at = t.find('\n');
        if(at == std::string::npos || sscanf(t.c_str(), "FONT %d %d %d %d %d %d", &size, &asc, &desc, &cap, &aw, &ah) != 6 || aw <= 0 || ah <= 0) return nullptr;
        s_Intl.size = size; s_Intl.asc = asc;
        while(at != std::string::npos && at + 1 < t.size())
        {
            const size_t nx = t.find('\n', at + 1);
            unsigned code; int x, y, w, h, xo, yo; float adv;
            if(sscanf(t.c_str() + at + 1, "G %u %d %d %d %d %d %d %f", &code, &x, &y, &w, &h, &xo, &yo, &adv) == 8)
            {
                Glyph g; g.u0 = (float)x / aw; g.v0 = (float)y / ah; g.u1 = (float)(x + w) / aw; g.v1 = (float)(y + h) / ah;
                g.w = w; g.h = h; g.xo = xo; g.yo = yo; g.adv = adv; g.ok = true; s_Intl.g[code] = g;
            }
            at = nx;
        }
        char nm[40]; snprintf(nm, sizeof(nm), "intl_%s.png", s_Intl.code.c_str());
        if(!LoadMem(nm, png, pn)) { logger->Error("Neon menu: the %s letters did not load", s_Intl.code.c_str()); return nullptr; }
        s_Intl.tex = NeonPaint::T(nm);
        logger->Info("Neon menu: %s letters loaded (%d)", s_Intl.code.c_str(), (int)s_Intl.g.size());
        return s_Intl.tex ? &s_Intl : nullptr;
    }
    bool HighBytes(const char* s) { for(; *s; ++s) if((unsigned char)*s >= 0x80) return true; return false; }
    // one UTF-8 letter at p -> its code point, p moved on (a broken byte = '?')
    uint32_t NextCp(const unsigned char*& p)
    {
        uint32_t c = *p;
        if(c < 0x80) { ++p; return c; }
        int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
        if(n < 0) { ++p; return '?'; }
        c &= (0x3F >> n); ++p;
        for(int i = 0; i < n; ++i) { if((*p & 0xC0) != 0x80) return '?'; c = (c << 6) | (*p++ & 0x3F); }
        return c;
    }
    const Glyph* IntlGlyph(const IntlAtlas* in, uint32_t c)
    {
        auto it = in->g.find(c); if(it != in->g.end()) return &it->second;
        it = in->g.find('?'); return it != in->g.end() ? &it->second : nullptr;
    }
}
const char* NeonPaint::Tr(const char* s)
{
    if(!s || !*s || I18n::Current() == 0) return s;
    const char* e = nullptr;
    const char* r = I18n::TextAny(nullptr, s, nullptr, &e);
    if(r == s) return s;
    // the result is a table string (0-terminated) or the ring buffer (0-terminated too)
    return r;
}

float NeonPaint::TextW(int font, const char* s, float px, float track)
{
    if(font < 0 || font >= F_COUNT || !s) return 0;
    s = Tr(s);
    const Atlas& a = s_Font[font];
    if(HighBytes(s))
        if(const IntlAtlas* in = Intl())
        {
            const float sc = px / (float)in->size; float w = 0; int n = 0;
            for(const unsigned char* p = (const unsigned char*)s; *p; ++n)
            {
                const uint32_t c = NextCp(p);
                if(const Glyph* g = IntlGlyph(in, c)) w += g->adv * sc + (g->adv > 0 ? track : 0);
            }
            return n ? w - track : 0;
        }
    const float sc = px / (float)a.size;
    float w = 0; int n = 0;
    for(const unsigned char* p = (const unsigned char*)s; *p; ++p, ++n)
    {
        if(*p >= 0x80 && (*p & 0xC0) == 0x80) { --n; continue; }   // one '?' per letter, not per byte
        unsigned c = *p; if(c < 32 || c > 126 || !a.g[c].ok) c = '?';
        w += a.g[c].adv * sc + track;
    }
    return n ? w - track : 0;
}
void NeonPaint::Text(int font, const char* s, float x, float y, float px, Col top, Col bottom, float track)
{
    if(font < 0 || font >= F_COUNT || !s) return;
    s = Tr(s);
    const Atlas& a = s_Font[font]; if(!a.tex) return;
    const float sc = px / (float)a.size, base = y + a.asc * sc;
    float pen = x;
    if(HighBytes(s))
        if(const IntlAtlas* in = Intl())
        {   // the language's own letters (one font), on the same baseline as the ASCII font
            const float s2 = px / (float)in->size;
            for(const unsigned char* p = (const unsigned char*)s; *p;)
            {
                const uint32_t c = NextCp(p);
                const Glyph* g = IntlGlyph(in, c); if(!g) continue;
                if(g->w > 0 && g->h > 0)
                {
                    const float gx = pen + g->xo * s2, gy = base + g->yo * s2, gx1 = gx + g->w * s2, gy1 = gy + g->h * s2;
                    ImgUV(in->tex, gx, gy, gx1, gy1, g->u0, g->v0, g->u1, g->v1, Lerp(top, bottom, (gy - y) / px), Lerp(top, bottom, (gy1 - y) / px));
                }
                pen += g->adv * s2 + (g->adv > 0 ? track : 0);   // a combining mark (Thai, Vietnamese) adds no space
            }
            return;
        }
    for(const unsigned char* p = (const unsigned char*)s; *p; ++p)
    {
        if(*p >= 0x80 && (*p & 0xC0) == 0x80) continue;
        unsigned c = *p; if(c < 32 || c > 126 || !a.g[c].ok) c = '?';
        const Glyph& g = a.g[c];
        if(g.w > 0 && g.h > 0)
        {
            const float gx = pen + g.xo * sc, gy = base + g.yo * sc, gx1 = gx + g.w * sc, gy1 = gy + g.h * sc;
            ImgUV(a.tex, gx, gy, gx1, gy1, g.u0, g.v0, g.u1, g.v1, Lerp(top, bottom, (gy - y) / px), Lerp(top, bottom, (gy1 - y) / px));
        }
        pen += g.adv * sc + track;
    }
}

void NeonPaint::OnTouch(int kind, int finger, float x, float y)
{
    std::lock_guard<std::mutex> lk(s_TouchMx);
    if(kind == 1)
    {
        // 5.02.47: the newest finger wins. Before, a second finger was ignored while the first was down - and when
        // the first one's release was lost (the touch source switches between the touch hook and the game's
        // pointer table as the menu opens / closes), the menu stayed locked on that finger and no tap worked.
        s_nFinger = finger; s_bDown = true; s_fX = s_fPX = x; s_fY = s_fPY = y; s_fMove = 0;
    }
    else if(finger == s_nFinger)
    {
        if(kind == 2 && (x < 0 || y < 0)) return;   // 5.02.74
        s_fX = x; s_fY = y;
        if(x >= 0 && y >= 0) { const float d = fabsf(x - s_fPX) + fabsf(y - s_fPY); if(d > s_fMove) s_fMove = d; }
        if(kind == 3)
        {   // 5.02.74: some phones give the release no coordinates (-1 / 0,0): the last place the finger was
            if(x < 0 || y < 0 || (x == 0 && y == 0)) { x = s_fX >= 0 ? s_fX : s_fPX; y = s_fY >= 0 ? s_fY : s_fPY; }
            s_bDown = false; s_bUp = true; s_fUX = x; s_fUY = y; s_nFinger = -1;
        }
    }
}
void NeonPaint::FrameInput()
{
    std::lock_guard<std::mutex> lk(s_TouchMx);
    f_fLY = (f_bWasDown && s_bDown) ? f_fY : s_fY;
    f_bDown = s_bDown; f_fX = s_fX; f_fY = s_fY; f_fPX = s_fPX; f_fPY = s_fPY; f_fMove = s_fMove; f_bWasDown = s_bDown;
    f_bTap = s_bUp; f_fUX = s_fUX; f_fUY = s_fUY; s_bUp = false;
}
static inline bool In(float x, float y, float x0, float y0, float x1, float y1) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
bool NeonPaint::Tap(float x0, float y0, float x1, float y1) { return f_bTap && f_fMove < H() * 0.05f &&   /* 5.02.47: 3.5 % lost taps with a little finger roll */ In(f_fPX, f_fPY, x0, y0, x1, y1) && (In(f_fUX, f_fUY, x0, y0, x1, y1) || f_fMove < H() * 0.02f); }   // 5.02.74: pressed on it and barely moved = its tap
bool NeonPaint::Held(float x0, float y0, float x1, float y1) { return f_bDown && In(f_fX, f_fY, x0, y0, x1, y1) && In(f_fPX, f_fPY, x0, y0, x1, y1); }
void NeonPaint::EatTap() { f_bTap = false; }
// 5.01.11: lists scroll with the finger, sliders follow it
float NeonPaint::DragDY(float x0, float y0, float x1, float y1) { return (f_bDown && In(f_fPX, f_fPY, x0, y0, x1, y1)) ? f_fY - f_fLY : 0.0f; }
bool  NeonPaint::Dragging() { return f_bDown && f_fMove >= H() * 0.035f; }
bool  NeonPaint::HeldX(float x0, float y0, float x1, float y1, float* x) { if(!(f_bDown && In(f_fPX, f_fPY, x0, y0, x1, y1))) return false; if(x) *x = f_fX; return true; }
