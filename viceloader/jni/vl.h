// VICE Loader (com.thirtyfoxmc.viceloader) - shared declarations of the native part
#pragma once
#include <string>
#include <vector>
#include <stdint.h>
struct ImFont;

#define VL_VERSION      "5.00.01"
#define VL_BRIDGE_PORT  47790          // src/vc/vc_overlay.inc VC_OVERLAY_PORT (the game side, 127.0.0.1 only)
#define VL_LAN_PORT     7777           // VICE City LAN server default port
#define VL_LAN_NAME     "VICE City LAN server"

// vl_main.cpp
bool VL_ReadAsset(const char* name, std::vector<unsigned char>& out);
void VL_JavaCall(const char* what, const char* arg);   // "copy", "paste", "launch", "export", "toast", "click", "url", "mic", "prefs", "barreset"
const char* VL_FilesDir();
// vl_shims.cpp
void VL_SetFont(ImFont* f);
void VL_PasteArrived(const char* text);
void VL_ForgetTextures();
void* VL_TextureMem(const char* key, const std::string& bytes);   // 4.13.64: server preview pictures
// vl_panel.cpp
enum { VL_MODE_BUBBLE = 0, VL_MODE_PANEL = 1, VL_MODE_CHAT = 2, VL_MODE_MENU = 3, VL_MODE_PAGE = 4 };   // 3 = the ⋮ dropdown under the bar, 4 = a San Andreas page window (Player list / Help) over the game
// VL_Open(100 + tab): tabs of vl_panel.cpp (6 Players, 7 Voice chat, 8 MonetLoader, 9 Help) - OverlayService.java uses these
void VL_PanelInit();
void VL_PanelFrame(float w, float h);   // inside an ImGui frame
int  VL_Mode();                         // what the overlay shows now (Java resizes / passes touches)
void VL_Open(int mode);                 // from the bar: 1 = launcher, 2 = chat keyboard, 3 = ⋮ dropdown, 100 + tab = that page
void VL_SetPref(const char* key, const char* value);
