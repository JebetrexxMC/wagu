#include <mod/amlmod.h>
#include <mod/config.h>
#include <mod/logger.h>
#include <sys/stat.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>
#include <unistd.h>

#include "runtime.h"
#include "ui/ui_common.h"
#include "imgui.h"
#include "rw_glue.h"
#include "RW/RenderWare.h"

MYMODCFG(net.coopfusion32.mobile, CoopFusion32, 1.0.0, CoopFusion)
NEEDGAME(com.rockstargames.gtasa)
BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.gtasa.utils, 1.6)
END_DEPLIST()

uintptr_t pGameLib = 0;
void* pGameHandle = nullptr;
RwReal* nearScreenZ = nullptr;
RwReal* recipNearClip = nullptr;
void (*SetScissorRect)(float*) = nullptr;

extern void ImGui_ImplRenderWare_RenderDrawData(ImDrawData* drawData);
extern bool ImGui_ImplRenderWare_Init();
extern void ImGui_ImplRenderWare_NewFrame();

namespace {
enum { TQ_POS, TQ_DOWN, TQ_UP, TQ_CLEAR };
struct TouchEvent { int type; float x; float y; };
std::mutex g_touchMutex;
std::vector<TouchEvent> g_touchQueue;
std::atomic<bool> g_imguiReady{false};
std::atomic<bool> g_rwReady{false};
std::atomic<bool> g_uiFingerDown{false};
bool g_ignoredFinger[16]{};
int g_mouseFinger = -1;
std::chrono::steady_clock::time_point g_lastUiTouch{};
float g_touchScaleX = 1.0f;
float g_touchScaleY = 1.0f;
bool g_touchScaleDetected = false;

void QueueTouch(int type, float x = 0.0f, float y = 0.0f) {
    std::lock_guard<std::mutex> lock(g_touchMutex);
    if (g_touchQueue.size() > 512)
        g_touchQueue.erase(g_touchQueue.begin(), g_touchQueue.begin() + 256);
    g_touchQueue.push_back({type, x, y});
}

void DrainTouchQueue() {
    std::vector<TouchEvent> events;
    {
        std::lock_guard<std::mutex> lock(g_touchMutex);
        events.swap(g_touchQueue);
    }
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    for (const TouchEvent& event : events) {
        switch (event.type) {
            case TQ_POS: io.AddMousePosEvent(event.x, event.y); break;
            case TQ_DOWN: io.AddMouseButtonEvent(0, true); break;
            case TQ_UP: io.AddMouseButtonEvent(0, false); break;
            case TQ_CLEAR: io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); break;
        }
    }
}

void EnsureImGui() {
    if (g_imguiReady.load()) return;
    if (!ImGui::GetCurrentContext()) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 9.0f;
        style.FrameRounding = 6.0f;
        style.GrabRounding = 6.0f;
        style.WindowPadding = ImVec2(16.0f, 14.0f);
        style.FramePadding = ImVec2(10.0f, 8.0f);
        style.ItemSpacing = ImVec2(8.0f, 8.0f);
    }
    if (!ImGui_ImplRenderWare_Init()) {
        if (logger) logger->Error("CoopFusion32: RenderWare ImGui initialization failed");
        return;
    }
    g_imguiReady.store(true);
}

void MapTouch(int& x, int& y) {
    const int renderWidth = RsGlobal ? RsGlobal->maximumWidth : 0;
    const int renderHeight = RsGlobal ? RsGlobal->maximumHeight : 0;
    if (!g_touchScaleDetected && renderWidth > 0 && renderHeight > 0 &&
        (x > renderWidth + 2 || y > renderHeight + 2)) {
        int deviceWidth = 0, deviceHeight = 0;
        aml->GetDisplaySize(&deviceWidth, &deviceHeight);
        if (deviceWidth < deviceHeight) std::swap(deviceWidth, deviceHeight);
        if (deviceWidth > renderWidth && deviceHeight > renderHeight) {
            g_touchScaleX = static_cast<float>(renderWidth) / deviceWidth;
            g_touchScaleY = static_cast<float>(renderHeight) / deviceHeight;
            g_touchScaleDetected = true;
            if (logger) logger->Info("CoopFusion touch scale: %dx%d -> %dx%d",
                deviceWidth, deviceHeight, renderWidth, renderHeight);
        }
    }
    if (g_touchScaleDetected) {
        x = static_cast<int>(x * g_touchScaleX);
        y = static_cast<int>(y * g_touchScaleY);
    }
}

bool BlockGameTouch() {
    if (CoopFusion::MenuOpen() || g_uiFingerDown.load()) {
        g_lastUiTouch = std::chrono::steady_clock::now();
        return true;
    }
    const auto elapsed = std::chrono::steady_clock::now() - g_lastUiTouch;
    return elapsed < std::chrono::milliseconds(350);
}

DECL_HOOK(bool, TI_IsTouched, int widget, void* point, int finger) {
    return BlockGameTouch() ? false : TI_IsTouched(widget, point, finger);
}
DECL_HOOK(bool, TI_IsReleased, int widget, void* point, int finger) {
    return BlockGameTouch() ? false : TI_IsReleased(widget, point, finger);
}
DECL_HOOK(bool, TI_IsDoubleTapped, int widget, bool second, int finger) {
    return BlockGameTouch() ? false : TI_IsDoubleTapped(widget, second, finger);
}
DECL_HOOK(bool, TI_IsHeldDown, int widget, int finger) {
    return BlockGameTouch() ? false : TI_IsHeldDown(widget, finger);
}
DECL_HOOK(bool, TI_IsSwipedLeft, int widget, int finger) {
    return BlockGameTouch() ? false : TI_IsSwipedLeft(widget, finger);
}
DECL_HOOK(bool, TI_IsSwipedRight, int widget, int finger) {
    return BlockGameTouch() ? false : TI_IsSwipedRight(widget, finger);
}
DECL_HOOK(bool, TI_IsSwipedUp, int widget, int finger) {
    return BlockGameTouch() ? false : TI_IsSwipedUp(widget, finger);
}
DECL_HOOK(bool, TI_IsSwipedDown, int widget, int finger) {
    return BlockGameTouch() ? false : TI_IsSwipedDown(widget, finger);
}
DECL_HOOK(bool, Widget_IsTouched, void* self, void* point) {
    return BlockGameTouch() ? false : Widget_IsTouched(self, point);
}

void HookTouchInterface() {
    int hooked = 0;
#define CF_HOOK_IF(name, symbol) do { \
    void* address = reinterpret_cast<void*>(aml->GetSym(pGameHandle, symbol)); \
    if (address) { HOOK(name, address); ++hooked; } \
} while (0)
    CF_HOOK_IF(TI_IsTouched, "_ZN15CTouchInterface9IsTouchedENS_9WidgetIDsEP9CVector2Di");
    CF_HOOK_IF(TI_IsReleased, "_ZN15CTouchInterface10IsReleasedENS_9WidgetIDsEP9CVector2Di");
    CF_HOOK_IF(TI_IsDoubleTapped, "_ZN15CTouchInterface14IsDoubleTappedENS_9WidgetIDsEbi");
    CF_HOOK_IF(TI_IsHeldDown, "_ZN15CTouchInterface10IsHeldDownENS_9WidgetIDsEi");
    CF_HOOK_IF(TI_IsSwipedLeft, "_ZN15CTouchInterface12IsSwipedLeftENS_9WidgetIDsEi");
    CF_HOOK_IF(TI_IsSwipedRight, "_ZN15CTouchInterface13IsSwipedRightENS_9WidgetIDsEi");
    CF_HOOK_IF(TI_IsSwipedUp, "_ZN15CTouchInterface10IsSwipedUpENS_9WidgetIDsEi");
    CF_HOOK_IF(TI_IsSwipedDown, "_ZN15CTouchInterface12IsSwipedDownENS_9WidgetIDsEi");
    CF_HOOK_IF(Widget_IsTouched, "_ZN7CWidget9IsTouchedEP9CVector2D");
#undef CF_HOOK_IF
    if (logger) logger->Info("CoopFusion UI touch guards installed: %d", hooked);
}

DECL_HOOKv(Render2dStuff) {
    Render2dStuff();
    if (!g_rwReady.exchange(true)) InitRenderWareFunctions();
    if (!RsGlobal || RsGlobal->maximumWidth <= 0 || RsGlobal->maximumHeight <= 0)
        return;

    EnsureImGui();
    if (!g_imguiReady.load()) return;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(RsGlobal->maximumWidth),
                            static_cast<float>(RsGlobal->maximumHeight));
    io.DeltaTime = 1.0f / 60.0f;
    DrainTouchQueue();
    ImGui_ImplRenderWare_NewFrame();
    ImGui::NewFrame();
    CoopFusion::DrawHud(ImGui::GetBackgroundDrawList());
    CoopFusion::RenderConnectionUI();
    ImGui::Render();
    ImGui_ImplRenderWare_RenderDrawData(ImGui::GetDrawData());
}

DECL_HOOKv(AND_TouchEvent, int type, int finger, int x, int y) {
    if (finger < 0 || finger >= 16 || !RsGlobal) {
        AND_TouchEvent(type, finger, x, y);
        return;
    }
    constexpr int kRelease = 1;
    constexpr int kPush = 2;
    constexpr int kMove = 3;
    constexpr int kPointerUp = 4;
    const bool release = type == kRelease || type == kPointerUp;
    int mappedX = x, mappedY = y;
    MapTouch(mappedX, mappedY);

    if (type == kPush) {
        const bool want = CoopFusion::WantsTouchAt(
            static_cast<float>(mappedX), static_cast<float>(mappedY),
            static_cast<float>(RsGlobal->maximumWidth),
            static_cast<float>(RsGlobal->maximumHeight));
        if (want) {
            g_ignoredFinger[finger] = true;
            if (g_mouseFinger < 0) {
                QueueTouch(TQ_POS, static_cast<float>(mappedX), static_cast<float>(mappedY));
                QueueTouch(TQ_DOWN);
                g_mouseFinger = finger;
            }
            g_uiFingerDown.store(true);
            return;
        }
    } else if (g_ignoredFinger[finger]) {
        if (type == kMove && finger == g_mouseFinger)
            QueueTouch(TQ_POS, static_cast<float>(mappedX), static_cast<float>(mappedY));
        if (release) {
            if (finger == g_mouseFinger) {
                QueueTouch(TQ_POS, static_cast<float>(mappedX), static_cast<float>(mappedY));
                QueueTouch(TQ_UP);
                g_mouseFinger = -1;
            }
            g_ignoredFinger[finger] = false;
            bool any = false;
            for (bool ignored : g_ignoredFinger) any = any || ignored;
            g_uiFingerDown.store(any);
            return;
        }
        return;
    }
    AND_TouchEvent(type, finger, x, y);
}

void QuickExit() {
    CoopFusion::Shutdown();
    if (logger) logger->Info("CoopFusion32 closing");
    std::fflush(nullptr);
    _exit(0);
}

void TerminateHandler() {
    if (logger) logger->Error("CoopFusion32: uncaught C++ exception");
    std::fflush(nullptr);
    std::abort();
}
}

extern "C" JNIEXPORT void OnModPreLoad() {
    logger->SetTag(BYBIT("CoopFusion32", "CoopFusion64"));
}

extern "C" JNIEXPORT void OnModLoad() {
    std::atexit(QuickExit);
    std::set_terminate(TerminateHandler);
    pGameLib = aml->GetLib("libGTASA.so");
    pGameHandle = aml->GetLibHandle("libGTASA.so");
    if (!pGameLib || !pGameHandle) {
        if (logger) logger->Error("CoopFusion32: libGTASA.so not found");
        return;
    }

    SET_TO(nearScreenZ, aml->GetSym(pGameHandle, "_ZN9CSprite2d11NearScreenZE"));
    SET_TO(recipNearClip, aml->GetSym(pGameHandle, "_ZN9CSprite2d13RecipNearClipE"));
    SET_TO(SetScissorRect, aml->GetSym(pGameHandle, "_ZN7CWidget10SetScissorER5CRect"));

    CoopFusion::Init(pGameLib);
    HOOKSYM(Render2dStuff, pGameHandle, "_Z13Render2dStuffv");
    HookTouchInterface();
#ifdef AML32
    if (*(uint32_t*)(pGameLib + 0x202020) == 0xE8BDB001)
        HOOKPLT(AND_TouchEvent, pGameLib + 0x675DE4);
    else
        HOOKSYM(AND_TouchEvent, pGameHandle, "_Z14AND_TouchEventiiii");
#else
    HOOKSYM(AND_TouchEvent, pGameHandle, "_Z14AND_TouchEventiiii");
#endif
    if (logger) logger->Info("CoopFusion32 standalone runtime loaded (%s)",
                              BYBIT("ARMv7", "ARM64"));
}
