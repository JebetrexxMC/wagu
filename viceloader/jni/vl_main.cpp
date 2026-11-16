// VICE Loader: JNI + Dear ImGui frame loop on the overlay's GLES3 surface (com.thirtyfoxmc.viceloader.Native).
// The overlay window covers the screen only while the launcher or the chat keyboard is open; otherwise Java shows
// the small floating bubble and the game gets every touch.
#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <GLES3/gl3.h>
#include "ui_common.h"
#include "emoji.h"
#include "vkeyboard.h"
#include "backends/imgui_impl_opengl3.h"
#include "vl.h"
#include <mod/logger.h>
#include <mutex>
#include <string>
#include <vector>
#include <time.h>
#include "../../imgui/fredoka.h"
#include "../../imgui/arial.h"

float VL_UserScale();   // vl_panel.cpp

static JavaVM*        s_pVM = nullptr;
static jclass         s_Cls = nullptr;
static jmethodID      s_OnNative = nullptr;
static AAssetManager* s_pAssets = nullptr;
static std::string    s_FilesDir = "/data/local/tmp";
static bool           s_bImGui = false, s_bGl = false;
static int            s_nW = 0, s_nH = 0;
static float          s_fFontScale = 0;
struct VlTouch { int action; float x, y; };
static std::mutex           s_TouchMx;
static std::vector<VlTouch> s_Touches;

const char* VL_FilesDir() { return s_FilesDir.c_str(); }
bool VL_ReadAsset(const char* name, std::vector<unsigned char>& out)
{
    if(!s_pAssets || !name) return false;
    AAsset* a = AAssetManager_open(s_pAssets, name, AASSET_MODE_BUFFER);
    if(!a) return false;
    off_t n = AAsset_getLength(a); out.resize((size_t)n);
    bool ok = AAsset_read(a, out.data(), (size_t)n) == n;
    AAsset_close(a);
    return ok;
}
void VL_JavaCall(const char* what, const char* arg)
{
    if(!s_pVM || !s_Cls || !s_OnNative) return;
    JNIEnv* env = nullptr; bool attached = false;
    if(s_pVM->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) { if(s_pVM->AttachCurrentThread(&env, nullptr) != JNI_OK) return; attached = true; }
    jstring w = env->NewStringUTF(what), a = env->NewStringUTF(arg ? arg : "");
    env->CallStaticVoidMethod(s_Cls, s_OnNative, w, a);
    if(env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(w); env->DeleteLocalRef(a);
    if(attached) s_pVM->DetachCurrentThread();
}
static uint64_t NowMs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

// the San Andreas MP fonts: Fredoka 34 px x 1.08 + Arial merged (Cyrillic, Greek, Vietnamese ...) + the colour emoji
static void BuildFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    if(s_bGl) ImGui_ImplOpenGL3_DestroyFontsTexture();
    io.Fonts->Clear();
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0100, 0x024F, 0x0370, 0x03FF, 0x0400, 0x045F, 0x1EA0, 0x1EFF, 0x2010, 0x2040, 0x2190, 0x21FF, 0x25A0, 0x25FF, 0 };
    ImFontConfig fc; fc.FontDataOwnedByAtlas = false; fc.OversampleH = 2; fc.OversampleV = 2;
    ImFont* f = io.Fonts->AddFontFromMemoryTTF((void*)fredokaData, sizeof(fredokaData), UI::S(34.0f * 1.08f), &fc, ranges);
    { ImFontConfig mc = fc; mc.MergeMode = true; io.Fonts->AddFontFromMemoryTTF((void*)arialData, sizeof(arialData), UI::S(34.0f), &mc, ranges); }
    UI::EmojiAddGlyphs(io.Fonts, f, UI::S(34.0f));
    io.Fonts->Build();
    UI::EmojiBake(io.Fonts, f);
    io.FontDefault = f;
    VL_SetFont(f);
    if(s_bGl) ImGui_ImplOpenGL3_CreateFontsTexture();
}
static void ApplyScale()
{
    float base = (float)(s_nW < s_nH ? s_nW : s_nH) / 1080.0f * VL_UserScale();
    if(base < 0.3f) base = 0.3f;
    if(base == s_fFontScale) return;
    s_fFontScale = base;
    UI::SetBaseScale(base);
    ImGuiStyle st; ImGui::StyleColorsDark(&st); st.ScaleAllSizes(base); st.ScrollbarSize = UI::S(10);
    st.TouchExtraPadding = ImVec2(UI::S(4), UI::S(4));
    ImGui::GetStyle() = st;
    BuildFonts();
}

extern "C" {
void VL_CrashInstall(const std::string& filesDir);   // vl_crash.cpp

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) { s_pVM = vm; return JNI_VERSION_1_6; }

JNIEXPORT void JNICALL Java_com_thirtyfoxmc_viceloader_Native_init(JNIEnv* env, jclass cls, jobject assets, jstring files)
{
    if(!s_Cls) { s_Cls = (jclass)env->NewGlobalRef(cls); s_OnNative = env->GetStaticMethodID(s_Cls, "onNative", "(Ljava/lang/String;Ljava/lang/String;)V"); }
    s_pAssets = AAssetManager_fromJava(env, env->NewGlobalRef(assets));
    const char* f = env->GetStringUTFChars(files, nullptr); s_FilesDir = f; env->ReleaseStringUTFChars(files, f);
    logger->SetTag("VICE Loader");
    VL_CrashInstall(s_FilesDir);   // 4.13.64: files/crash_native.txt on a native crash
    if(!s_bImGui)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        s_bImGui = true;
        VL_PanelInit();
        logger->Info("VICE Loader %s ready (files %s)", VL_VERSION, s_FilesDir.c_str());
    }
}
JNIEXPORT void JNICALL Java_com_thirtyfoxmc_viceloader_Native_surfaceCreated(JNIEnv*, jclass)
{
    if(!s_bImGui) return;
    if(s_bGl) { ImGui_ImplOpenGL3_Shutdown(); s_bGl = false; }   // a new EGL context: the old GL objects are gone
    VL_ForgetTextures();
    ImGui_ImplOpenGL3_Init("#version 300 es");
    s_bGl = true;
    s_fFontScale = 0;
}
JNIEXPORT void JNICALL Java_com_thirtyfoxmc_viceloader_Native_surfaceChanged(JNIEnv*, jclass, jint w, jint h)
{
    s_nW = w; s_nH = h;
    glViewport(0, 0, w, h);
}
JNIEXPORT void JNICALL Java_com_thirtyfoxmc_viceloader_Native_touch(JNIEnv*, jclass, jint action, jfloat x, jfloat y)
{
    std::lock_guard<std::mutex> lk(s_TouchMx);
    if(s_Touches.size() < 256) s_Touches.push_back({ action, x, y });
}
JNIEXPORT void JNICALL Java_com_thirtyfoxmc_viceloader_Native_open(JNIEnv*, jclass, jint mode) { VL_Open(mode); }
JNIEXPORT void JNICALL Java_com_thirtyfoxmc_viceloader_Native_paste(JNIEnv* env, jclass, jstring t)
{
    const char* s = t ? env->GetStringUTFChars(t, nullptr) : nullptr;
    VL_PasteArrived(s ? s : "");
    if(s) env->ReleaseStringUTFChars(t, s);
}
JNIEXPORT void JNICALL Java_com_thirtyfoxmc_viceloader_Native_setPref(JNIEnv* env, jclass, jstring k, jstring v)
{
    const char* a = env->GetStringUTFChars(k, nullptr); const char* b = env->GetStringUTFChars(v, nullptr);
    VL_SetPref(a, b);
    env->ReleaseStringUTFChars(k, a); env->ReleaseStringUTFChars(v, b);
}
JNIEXPORT jint JNICALL Java_com_thirtyfoxmc_viceloader_Native_mode(JNIEnv*, jclass) { return VL_Mode(); }
// one frame; returns the mode (0 bubble: the window lets the game have the touches, 1 launcher, 2 chat keyboard)
JNIEXPORT jint JNICALL Java_com_thirtyfoxmc_viceloader_Native_render(JNIEnv*, jclass)
{
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    if(!s_bImGui || !s_bGl || s_nW <= 0 || s_nH <= 0) return VL_Mode();
    ApplyScale();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)s_nW, (float)s_nH);
    static uint64_t s_nLast = 0; uint64_t now = NowMs();
    io.DeltaTime = s_nLast ? (float)(now - s_nLast) / 1000.0f : 1.0f / 60.0f; if(io.DeltaTime <= 0) io.DeltaTime = 1.0f / 60.0f;
    s_nLast = now;
    {
        std::lock_guard<std::mutex> lk(s_TouchMx);
        for(auto& t : s_Touches)
        {
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(t.x, t.y);
            if(t.action == 0) io.AddMouseButtonEvent(0, true);
            else if(t.action == 1) io.AddMouseButtonEvent(0, false);
        }
        s_Touches.clear();
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    VL_PanelFrame((float)s_nW, (float)s_nH);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // a finger that left: park the mouse off screen so nothing stays "hovered"
    if(!io.MouseDown[0]) { static int s_nIdle = 0; if(++s_nIdle > 2) { io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); s_nIdle = 0; } }
    return VL_Mode();
}
}
