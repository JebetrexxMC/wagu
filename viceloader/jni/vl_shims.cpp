// VICE Loader: the small parts of MP's UI layer (ui/hud.cpp, ui/gifs.cpp, ui/clipboard.cpp, ui/filepick.cpp) that the
// shared MP sources (ui/vkeyboard.cpp, ui/emoji.cpp) need. Inside the game these talk to RenderWare / the GTA APK;
// here they run on the overlay's own GLES3 context and the app's JNI.
#include "ui_common.h"
#include "hud.h"
#include "gifs.h"
#include "clipboard.h"
#include "filepick.h"
#include "vl.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../../imgui/stb_image.h"
#include <GLES3/gl3.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// ---- UI scale + fonts (ui/hud.cpp) ----
static float   s_fBase = 1.0f;
static ImFont* s_pFont = nullptr;
float   UI::S(float px) { return px * s_fBase; }
void    UI::SetBaseScale(float s) { s_fBase = s > 0.2f ? s : 0.2f; }
ImFont* UI::Font() { return s_pFont ? s_pFont : ImGui::GetFont(); }
ImFont* UI::PricedownFont() { return UI::Font(); }
ImFont* UI::GothicFont() { return UI::Font(); }
ImFont* UI::CapsFont() { return UI::Font(); }
void    VL_SetFont(ImFont* f) { s_pFont = f; }

// ---- textures from the APK assets (ui_bg.jpg: the launcher backdrop) ----
struct VlTex { GLuint id = 0; int w = 0, h = 0; bool tried = false; };
static std::map<std::string, VlTex> s_Tex;
void* HUD::Texture(const char* file, ImVec2* uv)
{
    VlTex& t = s_Tex[file ? file : ""];
    if(!t.tried)
    {
        t.tried = true;
        std::vector<unsigned char> data;
        if(VL_ReadAsset(file, data))
        {
            int w = 0, h = 0, n = 0;
            unsigned char* px = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &n, 4);
            if(px)
            {
                glGenTextures(1, &t.id);
                glBindTexture(GL_TEXTURE_2D, t.id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                stbi_image_free(px);
                t.w = w; t.h = h;
            }
        }
    }
    if(!t.id) return nullptr;
    if(uv) *uv = ImVec2(1, 1);
    return (void*)(intptr_t)t.id;
}
// 4.13.64: a server's preview picture from the online lobby (p/<token>, JPG / PNG bytes) - key = token + crc
void* VL_TextureMem(const char* key, const std::string& bytes)
{
    VlTex& t = s_Tex[key ? key : ""];
    if(!t.tried)
    {
        t.tried = true;
        int w = 0, h = 0, n = 0;
        unsigned char* px = stbi_load_from_memory((const unsigned char*)bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
        if(px)
        {
            glGenTextures(1, &t.id);
            glBindTexture(GL_TEXTURE_2D, t.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            stbi_image_free(px);
            t.w = w; t.h = h;
        }
    }
    return t.id ? (void*)(intptr_t)t.id : nullptr;
}
void VL_ForgetTextures() { s_Tex.clear(); }   // the GL context was lost (the overlay surface was re-created)
void* HUD::GifFrame(int, ImVec2*, ImVec2*, float*) { return nullptr; }

// ---- GIF picker: not in the overlay (the chat keyboard hides it while Gifs::Ready() is false) ----
static std::vector<std::string> s_GifCats = { "All" };
void Gifs::Init() {}
bool Gifs::Ready() { return false; }
const Gifs::Info* Gifs::Get(int) { return nullptr; }
const std::vector<std::string>& Gifs::Categories() { return s_GifCats; }
void Gifs::List(const char*, const char*, std::vector<int>& out) { out.clear(); }
bool Gifs::SheetPath(int, std::string&) { return false; }
int  Gifs::ParseToken(const char*) { return 0; }

// ---- clipboard (the app's ClipboardManager through JNI) ----
static std::mutex  s_ClipMx;
static bool        s_bPasteReady = false;
static std::string s_Paste;
void Clipboard::Copy(const char* text) { VL_JavaCall("copy", text ? text : ""); }
void Clipboard::RequestPaste() { VL_JavaCall("paste", ""); }
bool Clipboard::PollPaste(std::string& out)
{
    std::lock_guard<std::mutex> lk(s_ClipMx);
    if(!s_bPasteReady) return false;
    s_bPasteReady = false; out = s_Paste; return true;
}
void VL_PasteArrived(const char* text) { std::lock_guard<std::mutex> lk(s_ClipMx); s_Paste = text ? text : ""; s_bPasteReady = true; }

// ---- FilePick: the overlay window can't take the phone keyboard focus, so the MP keyboard is always used ----
bool FilePick::Open(const char*, const char*) { return false; }
int  FilePick::State() { return 0; }
long long FilePick::Copied() { return 0; }
std::string FilePick::Name() { return ""; }
std::string FilePick::Error() { return ""; }
void FilePick::Reset() {}
bool FilePick::AskText(const char*, const char*, int, bool, bool) { return false; }
int  FilePick::TextState() { return 0; }
std::string FilePick::Text() { return ""; }
std::string FilePick::TextError() { return ""; }
void FilePick::CancelText() {}
void FilePick::TextReset() {}
