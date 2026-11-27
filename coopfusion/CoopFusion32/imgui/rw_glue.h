#pragma once
// CoopFusion32 RenderWare bindings for the embedded Dear ImGui backend.
#include <stdint.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "RW/RenderWare.h"

extern uintptr_t pGameLib;
extern void* pGameHandle;
extern RwReal* nearScreenZ;
extern RwReal* recipNearClip;
extern void (*SetScissorRect)(float*);
