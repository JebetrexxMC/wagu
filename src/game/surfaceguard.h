#pragma once
// 5.02.66: SurfaceInfos_c getter check + repair (CantSprintOn crash, fault address 0x6C). See surfaceguard.cpp.
namespace SurfaceGuard
{
    void Init(void* gameHandle);      // when MP loads
    void Check(const char* when);     // after all mods loaded (and from Tick)
    void Tick();                      // once per game frame: first frame + every ~10 s
}
