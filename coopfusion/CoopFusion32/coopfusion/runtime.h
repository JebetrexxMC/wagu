#pragma once

#include <cstdint>

struct ImDrawList;

namespace CoopFusion {
bool Init(uintptr_t gameLibrary);
void Shutdown();
bool IsConnected();
bool IsHosting();
uint16_t LocalId();
int RemotePlayerCount();
void DrawHud(ImDrawList* drawList);
void RenderConnectionUI();
bool MenuOpen();
bool WantsTouchAt(float x, float y, float screenWidth, float screenHeight);
}
