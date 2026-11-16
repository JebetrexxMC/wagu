// VICE Loader (com.thirtyfoxmc.viceloader) - shared declarations of the native part
#pragma once
#include <string>
#include <vector>
#include <stdint.h>
struct ImFont;

#define VL_VERSION      "4.13.63"
#define VL_BRIDGE_PORT  47790          // src/vc/vc_overlay.inc VC_OVERLAY_PORT (the game side, 127.0.0.1 only)
#define VL_LAN_PORT     7777           // VICE City LAN server default port
#define VL_LAN_NAME     "VICE City LAN server"

// vl_main.cpp
bool VL_ReadAsset(const char* name, std::vector<unsigned char>& out);
void VL_JavaCall(const char* what, const char* arg);   // "copy", "paste", "launch", "export", "toast", "collapse"
const char* VL_FilesDir();
// vl_shims.cpp
void VL_SetFont(ImFont* f);
void VL_PasteArrived(const char* text);
void VL_ForgetTextures();
// vl_panel.cpp
enum { VL_MODE_BUBBLE = 0, VL_MODE_PANEL = 1, VL_MODE_CHAT = 2 };
void VL_PanelInit();
void VL_PanelFrame(float w, float h);   // inside an ImGui frame
int  VL_Mode();                         // what the overlay shows now (Java resizes / passes touches)
void VL_Open(int mode);                 // from the bubble: 1 = launcher, 2 = chat keyboard
void VL_SetPref(const char* key, const char* value);
