#pragma once

#include <cstddef>
#include <cstdint>

namespace coop_cutscene_mocap {

enum class State : uint8_t {
    Idle,
    Building,
    Ready,
    Loaded,
    Failed
};

void SetGameSearchRoots(const char* dataPath, const char* androidDataPath,
                        const char* internalPath, const char* androidDataRootPath);
void StartBuildAsync();

State GetState();
const char* GetLastError();
const char* GetSourcePath();

const uint8_t* GetAnimPackData(size_t* sizeOut);
size_t GetStandingClipCount();
const char* GetStandingClip(size_t index);
size_t GetWalkClipCount();
const char* GetWalkClip(size_t index);
float GetWalkSpeed(size_t index);

void MarkLoaded();
void MarkBlockMissing();
void MarkLoadFailed(const char* reason);

#ifdef COOP_CUTSCENE_MOCAP_TESTING
bool BuildFromImageForTesting(const char* path);
#endif

}
