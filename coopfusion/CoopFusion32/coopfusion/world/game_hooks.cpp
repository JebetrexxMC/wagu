#include "game_hooks.h"

using namespace CoopFusionNet;

#include <mod/amlmod.h>
#include <mod/logger.h>

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cctype>
#include <chrono>
#include <deque>
#include <cstdint>
#include <array>
#include <cstring>
#include <new>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gtasa_v200_armv7.h"
#include "../diagnostics/weapon_log.h"
#include "../diagnostics/cutscene_anim_log.h"
#include "../ui/ui_common.h"
#include "special_models.h"
#include "../platform/psdk_extern.h"
#include <aml-psdk/game_sa/engine/Streaming.h>
#include <aml-psdk/game_sa/engine/ModelInfo.h>
#include <aml-psdk/game_sa/entity/Camera.h>
#include <aml-psdk/game_sa/entity/PlayerPed.h>
#include <aml-psdk/game_sa/other/PlayerData.h>
#include <aml-psdk/game_sa/entity/Vehicle.h>
#include <aml-psdk/game_sa/entity/Automobile.h>
#include <aml-psdk/game_sa/entity/Bike.h>
#include <aml-psdk/game_sa/other/WeaponInfo.h>
#include <aml-psdk/game_sa/other/Pools.h>
#include <aml-psdk/game_sa/ai/TaskManager.h>
#include <aml-psdk/game_sa/ai/PedIntelligence.h>
#include <aml-psdk/game_sa/enum/TaskType.h>
#include <aml-psdk/game_sa/ai/tasks/Task.h>
#include <aml-psdk/game_sa/other/Clock.h>
#include "AnimBlendFrameData.h"
#include "sync_types.h"
#include "sync_interpolation.h"
#include "remote_aim.h"
#include "cutscene_partner_logic.h"
#include <aml-psdk/game_sa/engine/RsGlobal.h>
#include <aml-psdk/game_sa/engine/Sprite.h>
#include <aml-psdk/game_sa/engine/Sprite2d.h>
#include <aml-psdk/game_sa/engine/Font.h>
#include <aml-psdk/game_sa/engine/Radar.h>
#include <aml-psdk/renderware/RwStream.h>
#include "cutscene_mocap.h"

namespace
{

uint64_t SteadyNowMs();

constexpr bool kBonePoseSyncEnabled = false;

uintptr_t g_game = 0;
CoopClient* g_client = nullptr;
ISAUtils* g_saUtils = nullptr;
static const SCRIPT_COMMAND kScmSetCharAccuracy = { 0x02E2, "ii" };
static const SCRIPT_COMMAND kScmCreateChar = { 0x009A, "iifffv" };
static const SCRIPT_COMMAND kScmAddExplosion = { 0x020C, "fffi" };
static const SCRIPT_COMMAND kScmRemoveVehicleMod = { 0x06E8, "ii" };
static const SCRIPT_COMMAND kScmAddVehicleMod = { 0x06E7, "iiv" };
static const SCRIPT_COMMAND kScmRequestVehicleMod = { 0x06E9, "i" };
static const SCRIPT_COMMAND kScmHasVehicleModLoaded = { 0x06EA, "i" };
static const SCRIPT_COMMAND kScmGetCurrentVehiclePaintjob = { 0x0988, "iv" };
static const SCRIPT_COMMAND kScmGiveVehiclePaintjob = { 0x06ED, "ii" };
gtasa_v200_armv7::ProcessControlFn g_originalPedProcess = nullptr;
gtasa_v200_armv7::ProcessControlFn g_originalGenericPedProcess = nullptr;
gtasa_v200_armv7::ProcessControlFn g_originalVehicleProcess = nullptr;
gtasa_v200_armv7::ProcessControlFn g_originalPlaneProcess = nullptr;
gtasa_v200_armv7::ProcessControlFn g_originalHeliProcess = nullptr;
gtasa_v200_armv7::ProcessControlFn g_originalBikeProcess = nullptr;
gtasa_v200_armv7::ProcessControlFn g_originalBoatProcess = nullptr;
using WorldProcessFn = void (*)();
WorldProcessFn g_originalWorldProcess = nullptr;
using EnterCarAsDriverCtorFn = void (*)(void*, CVehicle*);
using TaskAllocateFn = void* (*)(unsigned int);
using TaskManagerSetTaskFn = void (*)(void*, CTask*, int, bool);
EnterCarAsDriverCtorFn g_originalEnterCarAsDriverCtor = nullptr;
EnterCarAsDriverCtorFn g_resolvedEnterCarAsDriverCtor = nullptr;
TaskAllocateFn g_taskAllocate = nullptr;
TaskManagerSetTaskFn g_taskManagerSetTask = nullptr;
constexpr unsigned int kRemoteEnterCarDriverTaskSize = 0x50;
gtasa_v200_armv7::FireInstantHitFn g_originalFireInstantHit = nullptr;
struct ShotVec3 { float x, y, z; };
using DoBulletImpactFn = void (*)(void*, void*, void*, ShotVec3*, ShotVec3*, void*, int);
using GenerateDamageEventFn = bool (*)(CPed*, CEntity*, int, int, int, int);
using AddProjectileFn = bool (*)(void*, int, ShotVec3, float, ShotVec3*, void*);
using AddExplosionFn = bool (*)(void*, void*, int, ShotVec3, uint32_t, uint8_t, float, uint8_t);
using WeaponFireFn = bool (*)(void*, void*, ShotVec3*, ShotVec3*, void*, ShotVec3*, ShotVec3*);
WeaponFireFn g_weaponFire = nullptr;
DoBulletImpactFn g_originalDoBulletImpact = nullptr;
GenerateDamageEventFn g_originalGenerateDamageEvent = nullptr;
GenerateDamageEventFn g_resolvedGenerateDamageEvent = nullptr;
uintptr_t g_fightTaskVtableAddressPoint = 0;
AddProjectileFn g_originalAddProjectile = nullptr;
AddProjectileFn g_projectileReplayFn = nullptr;
AddExplosionFn g_originalAddExplosion = nullptr;
bool g_replayingNativeExplosion = false;
uint64_t g_lastExplosionCaptureMs = 0;
bool g_shotImpactHooked = false;
bool g_replayingNativeShot = false;
void* g_replayShooter = nullptr;
uint64_t g_lastShotgunCaptureMs = 0;
gtasa_v200_armv7::EntityGetBoundCentreFn g_getBoundCentre = nullptr;
gtasa_v200_armv7::PedSetMoveStateFn g_setMoveState = nullptr;
gtasa_v200_armv7::EntityTeleportFn g_entityTeleport = nullptr;
gtasa_v200_armv7::PedSetMoveAnimFn g_setMoveAnim = nullptr;
using GiveWeaponFn = void (*)(void*, eWeaponType, unsigned int, bool);
using GetFirstAssociationFn = void* (*)(RpClump*);
using GetNextAssociationFn = void* (*)(void*);
using RemoveAllAssociationsFn = void (*)(RpClump*);
using BlendAnimationFn = void* (*)(RpClump*, int, int, float);
using BlendAnimationHierarchyFn = void* (*)(RpClump*, void*, int, float);
using GetAnimBlockNameFn = const char* (*)(int);
using GetAnimationBlockIndexFn = int (*)(const char*);
using GetAnimationBlockFn = void* (*)(const char*);
using GetAnimationFn = void* (*)(const char*, const void*);
using FindGroundZFor3DCoordFn = float (*)(float, float, float, bool*, CEntity**);
using RwStreamOpenFn = RwStream* (*)(RwStreamType, RwStreamAccessType, const void*);
using RwStreamCloseFn = RwBool (*)(RwStream*, void*);
using LoadAnimFileFn = void (*)(RwStream*, bool, const char (*)[32]);
using CutsceneTimeFn = int (*)();
using DoGunFlashFn = bool (*)(void*, int, bool);
GetFirstAssociationFn g_getFirstAssociation = nullptr;
GetNextAssociationFn g_getNextAssociation = nullptr;
RemoveAllAssociationsFn g_removeAllAssociations = nullptr;
BlendAnimationFn g_blendAnimation = nullptr;
BlendAnimationHierarchyFn g_blendAnimationByHierarchy = nullptr;
GetAnimBlockNameFn g_getAnimBlockName = nullptr;
GetAnimationBlockIndexFn g_getAnimationBlockIndex = nullptr;
GetAnimationBlockFn g_getAnimationBlock = nullptr;
GetAnimationFn g_getAnimation = nullptr;
FindGroundZFor3DCoordFn g_findGroundZFor3DCoord = nullptr;
RwStreamOpenFn g_rwStreamOpen = nullptr;
RwStreamCloseFn g_rwStreamClose = nullptr;
LoadAnimFileFn g_loadAnimFile = nullptr;
CutsceneTimeFn g_getCutsceneTimeMs = nullptr;
int32_t* g_animManagerNumBlocks = nullptr;
int32_t* g_animManagerNumAnimations = nullptr;
int32_t* g_cutsceneLoadStatus = nullptr;
bool* g_cutsceneAnimLoaded = nullptr;
bool g_cutsceneMocapBuildStarted = false;
bool g_cutsceneMocapFailureLogged = false;
bool g_cutsceneMocapReadyLogged = false;
uint64_t g_cutsceneMocapNextLoadAttemptMs = 0;
DoGunFlashFn g_doGunFlash = nullptr;
std::unordered_map<int, uint64_t> g_animationBlockNextRequestMs;
std::unordered_set<int> g_animationBlockWaitLogged;
std::unordered_set<int> g_animationBlockReadyLogged;
std::unordered_set<int> g_animationBlockLookupErrorLogged;
constexpr int kAnimationStreamingModelBase = 25575;
constexpr uintptr_t kAnimationBlockLoadedOffset = 0x10;
constexpr uintptr_t kAnimAssocGroupsPtrOffset = 0x942184;
constexpr uintptr_t kNumAnimAssocDefinitionsOffset = 0x9421BC;
constexpr uintptr_t kAnimHierarchyArrayOffset = 0x93209C;
constexpr uintptr_t kNumAnimationsOffset = 0x94217C;
constexpr uintptr_t kAnimHierarchyStride = 0x18;
constexpr uintptr_t kAnimHierarchyCount = 2500;
constexpr uintptr_t kAssocHierarchy = 0x14;
constexpr int kMaximumAnimationGroups = 512;
using SetCurrentWeaponFn = void (*)(void*, eWeaponType);
using RequestSpecialCharFn = void (*)(int, const char*, int);
using RequestSpecialModelFn = void (*)(int, const char*, int);
GiveWeaponFn g_giveWeapon = nullptr;
SetCurrentWeaponFn g_setCurrentWeapon = nullptr;
RequestSpecialCharFn g_originalRequestSpecialChar = nullptr;
RequestSpecialModelFn g_originalRequestSpecialModel = nullptr;




static const SCRIPT_COMMAND kScmGetWeaponTypeModel = { 0x0781, "iv" };
static const SCRIPT_COMMAND kScmGetCurrentCharWeapon = { 0x0470, "iv" };
static const SCRIPT_COMMAND kScmStoreWantedLevel = { 0x01C0, "iv" };
static const SCRIPT_COMMAND kScmRequestModel = { 0x0247, "i" };
static const SCRIPT_COMMAND kScmHasModelLoaded = { 0x0248, "i" };
static const SCRIPT_COMMAND kScmLoadAllRequestedModels = { 0x038B, "" };
static const SCRIPT_COMMAND kScmLoadSpecialCharacter = { 0x023C, "is" };
static const SCRIPT_COMMAND kScmHasSpecialCharacterLoaded = { 0x023D, "i" };
static const SCRIPT_COMMAND kScmGiveWeaponToChar = { 0x01B2, "iii" };
static const SCRIPT_COMMAND kScmSetCurrentCharWeapon = { 0x01B9, "ii" };
static const SCRIPT_COMMAND kScmFreezeCharPosition = { 0x04D7, "ii" };
static const SCRIPT_COMMAND kScmSetCharCoordinates = { 0x00A1, "ifff" };
static const SCRIPT_COMMAND kScmTaskPlayAnim = { 0x0605, "issfiiiii" };
static const SCRIPT_COMMAND kScmIsCharPlayingAnim = { 0x0611, "iss" };
static const SCRIPT_COMMAND kScmSetCharHeading = { 0x0173, "if" };
static const SCRIPT_COMMAND kScmClearCharTasksImmediately = { 0x0792, "i" };
static const SCRIPT_COMMAND kScmSetCharAnimSpeed = { 0x0393, "isf" };

static const SCRIPT_COMMAND kScmRemoveCharFromCarMaintainPosition = { 0x09C9, "i" };
static const SCRIPT_COMMAND kScmWarpCharFromCarToCoord = { 0x0362, "ifff" };
static const SCRIPT_COMMAND kScmSetCharProofs = { 0x02AB, "iiiiii" };
static const SCRIPT_COMMAND kScmTaskDie = { 0x05BE, "i" };
static const SCRIPT_COMMAND kScmWarpCharIntoCar = { 0x036A, "ii" };
static const SCRIPT_COMMAND kScmWarpCharIntoCarAsPassenger = { 0x0430, "iii" };
static const SCRIPT_COMMAND kScmSetCarCoordinates = { 0x00AB, "ifff" };



static const SCRIPT_COMMAND kScmTaskAimGunAtCoord = { 0x0667, "ifffi" };
static const SCRIPT_COMMAND kScmTaskShootAtCoord = { 0x0668, "ifffi" };
static const SCRIPT_COMMAND kScmTaskDriveBy = { 0x0713, "iiiffffiii" };
static const SCRIPT_COMMAND kScmClearCharTasks = { 0x0687, "i" };


uint32_t g_scmWeaponModelOutput = 0;
uint32_t g_scmCurrentWeaponOutput = 0xFFFFFFFFu;
uint32_t g_scmWantedOutput = 0;

uint8_t LocalWantedLevel()
{
    if (!g_saUtils) return 0;
    g_scmWantedOutput = 0;
    g_saUtils->ScriptCommand(&kScmStoreWantedLevel, 0, &g_scmWantedOutput);
    return static_cast<uint8_t>(std::min<uint32_t>(6u, g_scmWantedOutput));
}

bool IsSelectableWeaponType(uint32_t weaponType)
{


    return weaponType <= 0x2Eu || weaponType == 0x3Au;
}

uint8_t CurrentWeaponTypeForPed(CPed* ped)
{
    if (!ped) return 0;
    const unsigned slot = static_cast<unsigned>(ped->m_nSelectedWepSlot);
    uint8_t nativeWeapon = slot < 13
        ? static_cast<uint8_t>(ped->m_aWeapons[slot].m_eWeaponType) : 0;




    if (g_saUtils) {
        const int pedRef = CPools::GetPedRef(ped);
        if (pedRef > 0 && CPools::GetPed(pedRef) == ped) {
            g_scmCurrentWeaponOutput = 0xFFFFFFFFu;
            g_saUtils->ScriptCommand(&kScmGetCurrentCharWeapon, pedRef,
                                     &g_scmCurrentWeaponOutput);
            if (g_scmCurrentWeaponOutput != 0xFFFFFFFFu &&
                IsSelectableWeaponType(g_scmCurrentWeaponOutput))
                return static_cast<uint8_t>(g_scmCurrentWeaponOutput);
        }
    }
    return IsSelectableWeaponType(nativeWeapon) ? nativeWeapon : 0;
}
CPed* CreateRemotePedForModel(int pedType, int modelId, float x, float y, float z,
                              bool specialModel, int* outRef = nullptr) {
    if (outRef) *outRef = 0;
    if (!g_saUtils) return nullptr;
    if (!specialModel) return g_saUtils->CreatePed(pedType, modelId, x, y, z, outRef);
    int ref = 0;
    g_saUtils->ScriptCommand(&kScmCreateChar, pedType, modelId, x, y, z - 1.0f, &ref);
    if (ref <= 0) return nullptr;
    CPed* ped = CPools::GetPed(ref);
    if (!ped || CPools::GetPed(ref) != ped) return nullptr;
    if (outRef) *outRef = ref;
    return ped;
}

std::array<std::array<char, coop_special::kNameCapacity>, 11> g_specialCharNames{};
std::array<uint32_t, 11> g_specialCharNameRevision{};
std::array<uint64_t, 11> g_specialCharNameChangeMs{};
std::mutex g_specialCharNamesMutex;

enum class RemoteSpecialSlotPhase : uint8_t { Idle, AwaitingUnload, AwaitingLoad, Ready };
std::array<RemoteSpecialSlotPhase, 11> g_remoteSpecialSlotPhase{};




struct StableSpecialIdentity {
    uint16_t modelIndex = 0;
    std::string name;
};
std::unordered_map<uint32_t, StableSpecialIdentity> g_localSpecialIdentities;
std::unordered_map<uint32_t, std::array<uint32_t, 11>> g_localSpecialRevisionFloor;
std::unordered_map<uint32_t, std::string> g_localSpecialNameBeforeNormal;
std::unordered_map<uint32_t, uint16_t> g_lastUnresolvedSpecialIdentityModel;
std::vector<CPed*> g_spawnedNpcs;
std::unordered_map<uint16_t, CPed*> g_remotePeds;
CPed* LiveRemotePed(uint16_t id);

struct PendingLocalCarjackEvent {
    CVehicle* vehicle = nullptr;
    int vehiclePoolRef = 0;
    uint32_t vehicleKey = 0;
    uint64_t expiresAtMs = 0;
};
PendingLocalCarjackEvent g_pendingLocalCarjackEvent{};
uint32_t g_lastLocalCarjackVehicleKey = 0;
uint64_t g_lastLocalCarjackSendMs = 0;

struct QueuedRemoteVehicleEnter {
    RemoteVehicleEnter event{};
    uint64_t receivedAtMs = 0;
};
struct RemoteCarjackAttempt {
    uint16_t vehicleId = 0;
    int pedPoolRef = 0;
    int vehiclePoolRef = 0;
    const CPed* victimPed = nullptr;
    uint64_t expiresAtMs = 0;
};
std::vector<QueuedRemoteVehicleEnter> g_pendingRemoteVehicleEnters;
std::unordered_map<uint16_t, RemoteCarjackAttempt> g_remoteCarjackAttempts;
constexpr uint64_t kRemoteCarjackTaskDurationMs = 6500;
constexpr uint64_t kRemoteCarjackVictimGraceMs = 8000;
constexpr uint64_t kRemoteCarjackEventLifetimeMs = 5000;

struct PendingRemotePlayerHit { RemotePlayerHit hit{}; uint64_t expiresAtMs = 0; };
std::deque<PendingRemotePlayerHit> g_pendingRemotePlayerHits;
constexpr uint64_t kRemotePlayerHitRetryLifetimeMs = 3000;
constexpr size_t kMaxPendingRemotePlayerHits = 64;

struct PendingRemoteShot { RemoteShot shot{}; uint64_t expiresAtMs = 0; };
std::deque<PendingRemoteShot> g_pendingRemoteShots;
constexpr size_t kMaxPendingRemoteShots = 128;
constexpr uint64_t kRemoteShotRetryLifetimeMs = 2800;


std::unordered_map<uint16_t, int> g_remotePedRefs;


struct RemoteClothesApplyCache {
    CPed* ped = nullptr;
    int pedRef = 0;
    CPlayerData* ownedPlayerData = nullptr;
    PlayerClothesDesc* ownedDesc = nullptr;
    PlayerClothesDesc applied{};
    PlayerClothesDesc builtSnapshot{};
    bool appliedValid = false;
};
std::unordered_map<uint16_t, RemoteClothesApplyCache> g_remoteClothesCache;

using RebuildPlayerClothesFn = void (*)(CPlayerPed*, bool);
using PlayerDataCtorFn = void (*)(CPlayerData*);
using PlayerDataDtorFn = void (*)(CPlayerData*);

RebuildPlayerClothesFn GetRebuildPlayerClothesFn()
{
    static bool lookedUp = false;
    static RebuildPlayerClothesFn fn = nullptr;
    if (!lookedUp) {
        lookedUp = true;
        fn = GetMainLibrarySymbol<RebuildPlayerClothesFn>("_ZN8CClothes13RebuildPlayerEP10CPlayerPedb");
    }
    return fn;
}

constexpr uint16_t kStatFat = 21;
constexpr uint16_t kStatMuscle = 23;
using GetStatValueFn = float (*)(uint16_t);
using SetStatValueFn = void (*)(uint16_t, float);

GetStatValueFn GetStatGetter()
{
    static bool lookedUp = false;
    static GetStatValueFn fn = nullptr;
    if (!lookedUp) {
        lookedUp = true;
        fn = GetMainLibrarySymbol<GetStatValueFn>("_ZN6CStats12GetStatValueEt");
    }
    return fn;
}

SetStatValueFn GetStatSetter()
{
    static bool lookedUp = false;
    static SetStatValueFn fn = nullptr;
    if (!lookedUp) {
        lookedUp = true;
        fn = GetMainLibrarySymbol<SetStatValueFn>("_ZN6CStats12SetStatValueEtf");
    }
    return fn;
}

void ReadLocalBodyStats(PlayerClothesDesc& desc)
{
    GetStatValueFn get = GetStatGetter();
    if (!get) return;
    const float fat = get(kStatFat);
    const float muscle = get(kStatMuscle);
    if (std::isfinite(fat) && std::isfinite(muscle)) {
        desc.fatStat = fat;
        desc.muscleStat = muscle;
    }
}

PlayerDataCtorFn GetPlayerDataCtorFn()
{
    static bool lookedUp = false;
    static PlayerDataCtorFn fn = nullptr;
    if (!lookedUp) {
        lookedUp = true;
        fn = GetMainLibrarySymbol<PlayerDataCtorFn>("_ZN14CPlayerPedDataC2Ev");
    }
    return fn;
}

PlayerDataDtorFn GetPlayerDataDtorFn()
{
    static bool lookedUp = false;
    static PlayerDataDtorFn fn = nullptr;
    if (!lookedUp) {
        lookedUp = true;
        fn = GetMainLibrarySymbol<PlayerDataDtorFn>("_ZN14CPlayerPedDataD2Ev");
    }
    return fn;
}

void RebuildLocalClothesFromOwnDesc(const char* reason)
{
    CPlayerPed* local = FindPlayerPed(-1);
    if (!local || !local->m_pPlayerData || !local->m_pPlayerData->m_pPedClothesDesc) return;
    RebuildPlayerClothesFn rebuild = GetRebuildPlayerClothesFn();
    if (!rebuild) return;
    rebuild(local, false);
    logger->Info("CLOTHES_LOCAL_REBUILD reason=%s fat=%.1f muscle=%.1f", reason,
                 GetStatGetter() ? GetStatGetter()(kStatFat) : -1.0f,
                 GetStatGetter() ? GetStatGetter()(kStatMuscle) : -1.0f);
}

void ReleaseRemoteClothesCacheEntry(std::unordered_map<uint16_t, RemoteClothesApplyCache>::iterator it)
{
    if (it == g_remoteClothesCache.end()) return;
    RemoteClothesApplyCache& cache = it->second;
    if (cache.ownedPlayerData) {
        const bool pedStillLive = cache.ped && cache.pedRef && CPools::GetPed(cache.pedRef) == cache.ped;
        if (pedStillLive && cache.ped->m_pPlayerData == cache.ownedPlayerData)
            cache.ped->m_pPlayerData = nullptr;
        cache.ownedPlayerData->m_pPedClothesDesc = nullptr;
        if (PlayerDataDtorFn dtor = GetPlayerDataDtorFn()) dtor(cache.ownedPlayerData);
        ::operator delete(cache.ownedPlayerData);
    }
    delete cache.ownedDesc;
    g_remoteClothesCache.erase(it);
}

void ReleaseRemoteClothesForPed(CPed* ped)
{
    if (!ped) return;
    for (auto it = g_remoteClothesCache.begin(); it != g_remoteClothesCache.end();) {
        if (it->second.ped != ped) { ++it; continue; }
        auto doomed = it++;
        ReleaseRemoteClothesCacheEntry(doomed);
    }
}

bool ApplyRemotePlayerClothes(uint16_t playerId, CPed* ped, const PlayerClothesDesc& clothes)
{
    if (!ped || !ped->m_pRwClump) return false;
    const uint16_t modelId = *reinterpret_cast<const uint16_t*>(
        reinterpret_cast<uintptr_t>(ped) + gtasa_v200_armv7::kCPedModelIndexOffset);
    if (modelId != 0) return false;

    RebuildPlayerClothesFn rebuild = GetRebuildPlayerClothesFn();
    if (!rebuild) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            logger->Error("CLOTHES_SYNC: CClothes::RebuildPlayer symbol unavailable in libGTASA.so");
        }
        return false;
    }

    auto found = g_remoteClothesCache.find(playerId);
    if (found != g_remoteClothesCache.end() && found->second.ped == ped &&
        found->second.appliedValid && ped->m_pPlayerData &&
        ped->m_pPlayerData->m_pPedClothesDesc &&
        std::memcmp(&found->second.applied, &clothes, sizeof(clothes)) == 0 &&
        std::memcmp(ped->m_pPlayerData->m_pPedClothesDesc, &found->second.builtSnapshot, sizeof(clothes)) == 0)
        return true;
    if (found != g_remoteClothesCache.end() && found->second.ped != ped)
        ReleaseRemoteClothesCacheEntry(found);

    RemoteClothesApplyCache& state = g_remoteClothesCache[playerId];
    state.ped = ped;
    state.pedRef = CPools::GetPedRef(ped);
    if (!ped->m_pPlayerData && state.ownedPlayerData && state.ownedDesc) {
        ped->m_pPlayerData = state.ownedPlayerData;
    }
    if (!ped->m_pPlayerData) {
        PlayerDataCtorFn ctor = GetPlayerDataCtorFn();
        if (!ctor) {
            static bool ctorLogged = false;
            if (!ctorLogged) {
                ctorLogged = true;
                logger->Error("CLOTHES_SYNC: CPlayerPedData constructor symbol unavailable");
            }
            return false;
        }
        auto* data = static_cast<CPlayerData*>(::operator new(sizeof(CPlayerData), std::nothrow));
        if (!data) return false;
        std::memset(data, 0, sizeof(CPlayerData));
        ctor(data);
        auto* desc = new (std::nothrow) PlayerClothesDesc{};
        if (!desc) {
            data->m_pPedClothesDesc = nullptr;
            if (PlayerDataDtorFn dtor = GetPlayerDataDtorFn()) dtor(data);
            ::operator delete(data);
            return false;
        }
        data->m_pPedClothesDesc = reinterpret_cast<CPedClothesDesc*>(desc);
        ped->m_pPlayerData = data;
        state.ownedPlayerData = data;
        state.ownedDesc = desc;
    } else if (!ped->m_pPlayerData->m_pPedClothesDesc) {
        return false;
    }

    auto* nativeDesc = reinterpret_cast<PlayerClothesDesc*>(ped->m_pPlayerData->m_pPedClothesDesc);
    *nativeDesc = clothes;
    {
        GetStatValueFn getStat = GetStatGetter();
        SetStatValueFn setStat = GetStatSetter();
        const bool swapStats = getStat && setStat &&
            std::isfinite(clothes.fatStat) && std::isfinite(clothes.muscleStat);
        float localFat = 0.0f, localMuscle = 0.0f;
        if (swapStats) {
            localFat = getStat(kStatFat);
            localMuscle = getStat(kStatMuscle);
            setStat(kStatFat, clothes.fatStat);
            setStat(kStatMuscle, clothes.muscleStat);
        }
        rebuild(reinterpret_cast<CPlayerPed*>(ped), false);
        if (swapStats) {
            setStat(kStatFat, localFat);
            setStat(kStatMuscle, localMuscle);
        }
    }
    state.ped = ped;
    state.applied = clothes;
    std::memcpy(&state.builtSnapshot, ped->m_pPlayerData->m_pPedClothesDesc, sizeof(state.builtSnapshot));
    state.appliedValid = true;
    RebuildLocalClothesFromOwnDesc("after remote rebuild");
    logger->Info("CLOTHES_SYNC_APPLIED remote=%u model=%u fat=%.1f muscle=%.1f",
                 static_cast<unsigned>(playerId), static_cast<unsigned>(modelId),
                 clothes.fatStat, clothes.muscleStat);
    return true;
}


constexpr float kRemoteTeleportDistance = 25.0f;
std::unordered_map<uint16_t, CVehicle*> g_remoteVehicles;



struct VehicleEntityBinding {
    CVehicle* vehicle = nullptr;
    int poolRef = 0;
    bool createdByFusion = false;
};
std::unordered_map<uint32_t, VehicleEntityBinding> g_localVehicleEntities;
std::unordered_map<uint16_t, VehicleEntityBinding> g_vehicleEntityByNetworkId;
std::unordered_map<uint32_t, int> g_remoteMissionMarkerBlips;
struct RemoteVehicleVisualState {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    uint64_t lastUpdateMs = 0;
    float rightX = 1.0f, rightY = 0.0f, rightZ = 0.0f;
    float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
    float atX = 0.0f, atY = 0.0f, atZ = 1.0f;
    bool initialized = false;
};
std::unordered_map<uint16_t, RemoteVehicleVisualState> g_remoteVehicleVisuals;
std::unordered_map<uint16_t, float> g_remoteVehicleBaseZ;
std::unordered_map<uint32_t, CPed*> g_remoteNpcs;
struct RemoteNpcVisualState {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    uint64_t lastUpdateMs = 0;
    bool initialized = false;
    uint8_t weapon = 0xFF;
    uint64_t nextWeaponTryMs = 0;
    uint64_t lastHealthReportMs = 0;
    uint64_t lastAimTaskMs = 0;
    float lastAimX = 0.0f, lastAimY = 0.0f, lastAimZ = 0.0f;
    bool driveByTaskActive = false;
    bool lastAimFiring = false;
    bool animationInitialized = false;
    bool deathApplied = false;
    uint8_t animationAssociationCount = 0;
    std::array<AnimationAssociationState,64> animationAssociations{};
};
std::unordered_map<uint32_t, RemoteNpcVisualState> g_remoteNpcVisuals;
std::unordered_map<uint32_t, uint32_t> g_localNpcRefs;
uint64_t g_lastNpcSyncMs = 0;
bool g_remoteNameTagsVisible = true;
bool g_remoteHealthBarsVisible = true;
std::unordered_map<uint16_t, uint64_t> g_lastVehicleClaimMs;
struct LocalVehicleMotionSample {
    uint32_t key = 0;
    uint64_t timeMs = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    bool initialized = false;
};
LocalVehicleMotionSample g_localVehicleMotionSample{};


float g_remoteNameTagScale = 0.5f;



struct WeaponIconRect { float left, bottom, right, top; };
using GetWeaponInfoForIconFn = void* (*)(int weaponType, int skill);
using RequestWeaponIconModelFn = void (*)(int model, int flags);
using LoadWeaponIconModelsFn = void (*)(bool priority);
using DrawWeaponIconFn = void (*)(void* self, void* ped, WeaponIconRect rect, float alpha);
GetWeaponInfoForIconFn g_getWeaponInfoForIcon = nullptr;
RequestWeaponIconModelFn g_requestWeaponIconModel = nullptr;
LoadWeaponIconModelsFn g_loadWeaponIconModels = nullptr;
DrawWeaponIconFn g_drawWeaponIcon = nullptr;
std::unordered_set<int> g_streamedWeaponIconModels;

struct RemoteVisualState {


    RemoteVisualState() {}
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    uint64_t lastUpdateMs = 0;
    float lastTargetX = 0.0f, lastTargetY = 0.0f;
    uint8_t weapon = 0;
    uint8_t shootingFrames = 0;
    uint16_t fireAnimGroup = 0;
    int16_t fireAnimId = 0;
    uint8_t fireAnimFrames = 0;
    bool animationInitialized = false;
    bool initialized = false;
    bool wasInVehicle = false;
    uint8_t lastVehicleSeat = 0;
    uint64_t lastAimTaskMs = 0;
    AnimationProfile latestAimProfile{};
    bool latestAimProfileValid = false;
    uint32_t animationSequence = 0;
    uint8_t animationLocomotion = 0, animationAction = 0;
    uint16_t animationFlags = 0;
    uint8_t animationAssociationCount = 0;
    std::array<AnimationAssociationState, 64> animationAssociations{};
};
RemoteVisualState MakeInitialRemoteVisualState(const RemotePlayer& player)
{
    RemoteVisualState visual{};
    visual.x = player.x;
    visual.y = player.y;
    visual.z = player.z;
    visual.heading = player.heading;
    visual.lastUpdateMs = SteadyNowMs();
    visual.lastTargetX = player.x;
    visual.lastTargetY = player.y;
    visual.weapon = 0xFF;
    visual.wasInVehicle = player.vehicleId != 0;
    visual.lastVehicleSeat = player.vehicleSeat;
    visual.initialized = true;
    return visual;
}
std::unordered_map<uint16_t, RemoteVisualState> g_remoteVisuals;
std::unordered_set<uint16_t> g_heldRemoteCutscenePartners;
struct CutscenePartnerRuntime {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    float facingHeading = 0.0f;
    bool hasPlacement = false;
    bool hasShot = false;
    bool hasFacingHeading = false;
    bool shotReactionPending = false;
    bool fidgetActive = false;
    bool sceneActionPlayed = false;
    bool activeAnimationConfirmed = false;
    bool baseAnimationInstalled = false;
    unsigned int lastShotIndex = 0;
    uint8_t animationStage = 0;
    uint8_t fidgetSequence = 0;
    uint8_t idleProfile = 0xFF;
    uint64_t lastAnimationCheckMs = 0;
    uint64_t nextFidgetAtMs = 0;
    uint64_t nextWalkSpeedAtMs = 0;
    float activeWalkBaseSpeed = 0.0f;
    size_t lastMocapClipIndex = static_cast<size_t>(-1);
    size_t activeWalkClipIndex = static_cast<size_t>(-1);
    uint32_t mocapClipSequence = 0;
    std::string activeAnimation;
    std::string activeAnimationBlock;
    std::string lastSceneAction;
    std::string sceneName;
};
std::unordered_map<uint16_t, CutscenePartnerRuntime> g_cutscenePartnerRuntime;
coop_cutscene::ShotTracker g_cutsceneShotTracker;
coop_cutscene::ActorMotionTracker g_cutsceneActorMotion;
CVector g_cutsceneShotRight(0.0f, 0.0f, 0.0f);
char g_cutsceneShotName[9]{};
uint64_t g_cutsceneSceneStartMs = 0;
bool* g_cutsceneManagerRunning = nullptr;
bool* g_cutsceneManagerProcessing = nullptr;
CEntity** g_cutsceneManagerObjects = nullptr;
int32_t* g_cutsceneManagerObjectCount = nullptr;
char* g_cutsceneManagerName = nullptr;
bool g_cutsceneManagerSymbolsReady = false;
bool g_cutsceneWasActive = false;
void ReleaseRemoteCutscenePartner(uint16_t id, const char* reason);
std::unordered_map<const CPed*, uint64_t> g_remoteTaskPermitUntilMs;
std::unordered_map<const CPed*, uint64_t> g_remoteCarjackVictimUntilMs;
std::unordered_map<uint32_t, uint64_t> g_remoteCarjackNpcRemovalUntilMs;
std::unordered_set<uint32_t> g_deferredRemovedCarjackNpcs;
std::unordered_map<uint16_t, uint16_t> g_lastRemotePlayerHealth;
std::unordered_map<uint16_t, uint16_t> g_lastRejectedRemoteModels;
struct RemoteSpecialLease {
    int activeSlot = 0;
    std::string activeName;
    int pendingSlot = 0;
    std::string pendingName;
};
std::unordered_map<uint64_t, RemoteSpecialLease> g_remoteSpecialLeases;


std::array<int, 11> g_remoteSlotRefs{};
std::array<std::string, 11> g_remoteSlotHeldName{};
std::array<bool, 11> g_remoteSlotWasOurs{};
std::array<std::string, 11> g_remoteSlotResidentName{};
std::array<std::string, 11> g_remoteSlotRequestedName{};
std::array<uint64_t, 11> g_remoteSlotNextRetryMs{};

bool HasPedModelInfo(uint16_t modelId)
{
    if (modelId >= kModelInfoCapacity) return false;
    CBaseModelInfo** modelInfos = CModelInfo::ms_modelInfoPtrs;
    if (!modelInfos || !modelInfos[modelId]) return false;
    return modelInfos[modelId]->GetModelType() == MODEL_INFO_PED;
}

bool HasReadyPedModel(uint16_t modelId)
{
    if (!HasPedModelInfo(modelId)) return false;
    CBaseModelInfo** modelInfos = CModelInfo::ms_modelInfoPtrs;
    return modelInfos && modelInfos[modelId] && modelInfos[modelId]->m_pRwClump != nullptr;
}

bool IsSafeNetworkVehicleModel(uint16_t modelId)
{
    if (modelId < 400 || modelId >= kModelInfoCapacity || !CStreaming::IsInitialised() ||
        !CStreaming::IsObjectInCdImage(static_cast<int>(modelId))) return false;
    CBaseModelInfo** modelInfos = CModelInfo::ms_modelInfoPtrs;
    return modelInfos && modelInfos[modelId] && modelInfos[modelId]->GetModelType() == MODEL_INFO_VEHICLE;
}

bool IsSafeNetworkPedModel(uint16_t modelId)
{
    if (modelId == 0) return true;
    return modelId < kModelInfoCapacity && CStreaming::IsInitialised() &&
           HasPedModelInfo(modelId) && CStreaming::IsObjectInCdImage(static_cast<int>(modelId));
}

void LoadNetworkPedModel(uint16_t modelId)
{



    g_saUtils->LoadModelId(static_cast<int>(modelId));
    CStreaming::LoadAllRequestedModels(false);
}

static void InvalidateLocalSpecialIdentitiesForModel(uint16_t modelIndex)
{
    for (auto it = g_localSpecialIdentities.begin(); it != g_localSpecialIdentities.end();) {
        if (it->second.modelIndex == modelIndex) it = g_localSpecialIdentities.erase(it);
        else ++it;
    }
}

void HookRequestSpecialChar(int slot, const char* modelName, int flags)
{
    char normalized[coop_special::kNameCapacity]{};
    if (slot >= 1 && slot <= 10) {
        const bool known = coop_special::NormalizeName(modelName, normalized, sizeof(normalized));
        bool changed = false;
        {
            std::scoped_lock lock(g_specialCharNamesMutex);
            changed = std::strcmp(g_specialCharNames[slot].data(), normalized) != 0;
            std::snprintf(g_specialCharNames[slot].data(), g_specialCharNames[slot].size(), "%s", normalized);
            if (changed) {
                ++g_specialCharNameRevision[slot];
                g_specialCharNameChangeMs[slot] = SteadyNowMs();
            }
        }
        if (changed) {
            InvalidateLocalSpecialIdentitiesForModel(static_cast<uint16_t>(289 + slot));
            WeaponFileLog("SPECIAL_CHAR_REQUEST slot=%d name=%.15s known=%d", slot,
                          known ? normalized : (modelName ? modelName : ""), known ? 1 : 0);
        }
    }
    if (g_originalRequestSpecialChar) g_originalRequestSpecialChar(slot, modelName, flags);
}

void HookRequestSpecialModel(int modelId, const char* modelName, int flags)
{
    const int slot = modelId - 289;
    if (slot >= 1 && slot <= 10) {
        char normalized[coop_special::kNameCapacity]{};
        const bool known = coop_special::NormalizeName(modelName, normalized, sizeof(normalized));
        bool changed = false;
        {
            std::scoped_lock lock(g_specialCharNamesMutex);
            changed = std::strcmp(g_specialCharNames[slot].data(), normalized) != 0;
            std::snprintf(g_specialCharNames[slot].data(), g_specialCharNames[slot].size(), "%s", normalized);
            if (changed) {
                ++g_specialCharNameRevision[slot];
                g_specialCharNameChangeMs[slot] = SteadyNowMs();
            }
        }
        if (changed) {
            const bool ourPendingRequest = g_remoteSlotRefs[slot] > 0 &&
                !g_remoteSlotHeldName[slot].empty() &&
                std::strcmp(g_remoteSlotHeldName[slot].c_str(), normalized) == 0;
            g_remoteSlotWasOurs[slot] = ourPendingRequest;
            InvalidateLocalSpecialIdentitiesForModel(static_cast<uint16_t>(modelId));
            WeaponFileLog("SPECIAL_MODEL_REQUEST model=%d slot=%d name=%.15s known=%d owned=%d",
                          modelId, slot, known ? normalized : (modelName ? modelName : ""),
                          known ? 1 : 0, ourPendingRequest ? 1 : 0);
        }
    }
    if (g_originalRequestSpecialModel) g_originalRequestSpecialModel(modelId, modelName, flags);
}

std::string CurrentSpecialNameForModelIndex(uint16_t modelIndex, uint32_t pedRef)
{
    const int slot = coop_special::SlotFromModelIndex(modelIndex);
    if (!slot) return {};

    std::string requestedName;
    uint32_t requestRevision = 0;
    {
        std::scoped_lock lock(g_specialCharNamesMutex);
        char normalized[coop_special::kNameCapacity]{};
        if (coop_special::NormalizeName(g_specialCharNames[slot].data(), normalized, sizeof(normalized)))
            requestedName = normalized;
        requestRevision = g_specialCharNameRevision[slot];
    }

    const auto floorIt = g_localSpecialRevisionFloor.find(pedRef);
    bool freshRequest = floorIt == g_localSpecialRevisionFloor.end() ||
        requestRevision > floorIt->second[slot];
    const auto previousSpecial = g_localSpecialNameBeforeNormal.find(pedRef);
    if (previousSpecial != g_localSpecialNameBeforeNormal.end() &&
        requestedName == previousSpecial->second && !freshRequest)
        requestedName.clear();
    if (freshRequest && !requestedName.empty()) return requestedName;

    CBaseModelInfo** modelInfos = CModelInfo::ms_modelInfoPtrs;
    if (modelInfos && modelInfos[modelIndex]) {
        char liveName[coop_special::kNameCapacity]{};
        if (coop_special::NormalizeName(modelInfos[modelIndex]->m_szModelName,
                                        liveName, sizeof(liveName))) {
            const std::string live(liveName);
            if (previousSpecial == g_localSpecialNameBeforeNormal.end() || live != previousSpecial->second)
                return live;
        }
    }

    return {};
}

std::string StableSpecialNameForPed(CPed* ped, uint16_t modelIndex)
{
    if (!ped) return {};
    const uint32_t ref = static_cast<uint32_t>(CPools::GetPedRef(ped));
    const bool isSpecial = modelIndex >= coop_special::kFirstModelIndex &&
                           modelIndex <= coop_special::kLastModelIndex;
    if (!isSpecial) {
        if (ref) {
            auto previous = g_localSpecialIdentities.find(ref);
            if (previous != g_localSpecialIdentities.end()) {
                std::array<uint32_t, 11> revisions{};
                std::array<uint64_t, 11> changedAt{};
                std::array<std::string, 11> requestedNames{};
                {
                    std::scoped_lock lock(g_specialCharNamesMutex);
                    revisions = g_specialCharNameRevision;
                    changedAt = g_specialCharNameChangeMs;
                    for (int slot = 1; slot <= 10; ++slot)
                        requestedNames[slot] = g_specialCharNames[slot].data();
                }
                const int oldSlot = coop_special::SlotFromModelIndex(previous->second.modelIndex);
                const uint64_t nowMs = SteadyNowMs();
                for (int slot = 1; slot <= 10; ++slot) {
                    const bool isRecentDifferentRequest = slot != oldSlot &&
                        !requestedNames[slot].empty() && requestedNames[slot] != previous->second.name &&
                        changedAt[slot] != 0 && nowMs >= changedAt[slot] && nowMs - changedAt[slot] <= 1500;
                    if (isRecentDifferentRequest && revisions[slot] > 0) --revisions[slot];
                }
                g_localSpecialRevisionFloor[ref] = revisions;
                g_localSpecialNameBeforeNormal[ref] = previous->second.name;
                g_localSpecialIdentities.erase(previous);
            }
        }
        return {};
    }

    const std::string current = CurrentSpecialNameForModelIndex(modelIndex, ref);
    if (!current.empty()) {
        const auto previous = ref ? g_localSpecialIdentities.find(ref) : g_localSpecialIdentities.end();
        const bool changed = previous == g_localSpecialIdentities.end() ||
            previous->second.modelIndex != modelIndex || previous->second.name != current;
        if (ref) g_localSpecialIdentities[ref] = {modelIndex, current};
        if (changed) WeaponFileLog("SPECIAL_PED_CAPTURE ref=%u model=%hu name=%s", ref, modelIndex, current.c_str());
        g_localSpecialNameBeforeNormal.erase(ref);
        g_localSpecialRevisionFloor.erase(ref);
        g_lastUnresolvedSpecialIdentityModel.erase(ref);
        return current;
    }

    if (ref) g_localSpecialIdentities.erase(ref);
    auto unresolved = g_lastUnresolvedSpecialIdentityModel.find(ref);
    if (unresolved == g_lastUnresolvedSpecialIdentityModel.end() || unresolved->second != modelIndex) {
        g_lastUnresolvedSpecialIdentityModel[ref] = modelIndex;
        WeaponFileLog("SPECIAL_PED_IDENTITY_UNRESOLVED ref=%u model=%hu", ref, modelIndex);
    }
    return {};
}

void ForgetLocalSpecialIdentity(uint32_t ref)
{
    if (ref) g_localSpecialIdentities.erase(ref);
}

static int CurrentLocalSpecialSlotIndex()
{
    CPlayerPed* localPed = FindPlayerPed(-1);
    if (!localPed) return 0;
    const uint16_t modelIndex = *reinterpret_cast<const uint16_t*>(
        reinterpret_cast<uintptr_t>(localPed) + gtasa_v200_armv7::kCPedModelIndexOffset);
    return coop_special::SlotFromModelIndex(modelIndex);
}

uint64_t RemoteNpcSpecialLeaseKey(uint32_t npcId)
{
    return (uint64_t{1} << 63) | npcId;
}

void ReleaseRemoteSpecialSlot(uint64_t , int slot, const std::string& )
{

    if (slot < coop_special::kFirstRemoteSlot || slot > coop_special::kLastRemoteSlot ||
        g_remoteSlotRefs[slot] <= 0)
        return;
    if (--g_remoteSlotRefs[slot] > 0) return;
    if (CStreaming::IsInitialised() && slot != CurrentLocalSpecialSlotIndex()) {
        CStreaming::SetSpecialCharIsDeletable(slot);
        CStreaming::SetMissionDoesntRequireSpecialChar(slot);
    }
    g_remoteSlotRequestedName[slot].clear();
    g_remoteSlotNextRetryMs[slot] = 0;
    if (g_remoteSpecialSlotPhase[slot] == RemoteSpecialSlotPhase::AwaitingUnload)
        g_remoteSpecialSlotPhase[slot] = RemoteSpecialSlotPhase::Idle;
}

void ReleaseRemoteSpecialLease(uint64_t ownerKey)
{
    auto it = g_remoteSpecialLeases.find(ownerKey);
    if (it == g_remoteSpecialLeases.end()) return;
    ReleaseRemoteSpecialSlot(ownerKey, it->second.pendingSlot, it->second.pendingName);
    ReleaseRemoteSpecialSlot(ownerKey, it->second.activeSlot, it->second.activeName);
    g_remoteSpecialLeases.erase(it);
}

void ReleasePendingRemoteSpecialSlot(uint64_t ownerKey)
{
    auto it = g_remoteSpecialLeases.find(ownerKey);
    if (it == g_remoteSpecialLeases.end() || !it->second.pendingSlot) return;
    ReleaseRemoteSpecialSlot(ownerKey, it->second.pendingSlot, it->second.pendingName);
    it->second.pendingSlot = 0;
    it->second.pendingName.clear();
    if (!it->second.activeSlot) g_remoteSpecialLeases.erase(it);
}

void CommitRemoteSpecialSlot(uint64_t ownerKey, int slot, const std::string& modelName)
{
    auto it = g_remoteSpecialLeases.find(ownerKey);
    if (it == g_remoteSpecialLeases.end()) return;
    auto& lease = it->second;
    if (slot == lease.pendingSlot && lease.pendingName == modelName) {
        const int oldSlot = lease.activeSlot;
        const std::string oldName = lease.activeName;
        lease.activeSlot = slot;
        lease.activeName = modelName;
        lease.pendingSlot = 0;
        lease.pendingName.clear();
        if (oldSlot && oldSlot != slot) ReleaseRemoteSpecialSlot(ownerKey, oldSlot, oldName);
    }
}

static std::string SpecialNameForGameStreaming(const std::string& modelName)
{
    std::string result = modelName;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

static bool RemoteSpecialCharacterLoaded(int slot)
{
    if (slot < 1 || slot > 10) return false;
    if (g_saUtils) return g_saUtils->ScriptCommand(&kScmHasSpecialCharacterLoaded, slot) != 0;
    return CStreaming::HasSpecialCharLoaded(slot);
}

static bool RemoteSpecialCharacterLoadedForName(int slot, const std::string& modelName)
{
    if (slot < 1 || slot > 10 || g_remoteSpecialSlotPhase[slot] == RemoteSpecialSlotPhase::AwaitingUnload)
        return false;
    char normalized[coop_special::kNameCapacity]{};
    if (!coop_special::NormalizeName(modelName.c_str(), normalized, sizeof(normalized))) return false;
    const bool loaded = RemoteSpecialCharacterLoaded(slot);
    if (!loaded) return false;

    if (g_remoteSlotResidentName[slot] == normalized) return true;
    if (g_remoteSpecialSlotPhase[slot] != RemoteSpecialSlotPhase::AwaitingLoad) return false;

    bool requestedNameMatches = false;
    {
        std::scoped_lock lock(g_specialCharNamesMutex);
        requestedNameMatches = std::strcmp(g_specialCharNames[slot].data(), normalized) == 0;
    }
    if (!requestedNameMatches) return false;
    g_remoteSlotResidentName[slot] = normalized;
    g_remoteSpecialSlotPhase[slot] = RemoteSpecialSlotPhase::Ready;
    return true;
}

static void RequestRemoteSpecialCharacter(int slot, const std::string& modelName)
{
    if (slot < 1 || slot > 10 || modelName.empty() || !g_saUtils) return;
    const std::string gameName = SpecialNameForGameStreaming(modelName);
    const uint64_t now = SteadyNowMs();
    std::string previousName = g_remoteSlotResidentName[slot];
    if (previousName.empty()) {
        std::scoped_lock lock(g_specialCharNamesMutex);
        previousName = g_specialCharNames[slot].data();
    }

    if (RemoteSpecialCharacterLoadedForName(slot, modelName)) return;

    if (g_remoteSpecialSlotPhase[slot] == RemoteSpecialSlotPhase::AwaitingUnload) {
        if (RemoteSpecialCharacterLoaded(slot)) {
            if (now >= g_remoteSlotNextRetryMs[slot]) {
                CStreaming::SetSpecialCharIsDeletable(slot);
                CStreaming::SetMissionDoesntRequireSpecialChar(slot);
                CStreaming::RemoveModel(static_cast<int>(coop_special::ModelIndexFromSlot(slot)));
                g_remoteSlotNextRetryMs[slot] = now + 100;
                WeaponFileLog("REMOTE_SPECIAL_WAIT_UNLOAD slot=%d wanted=%s", slot, modelName.c_str());
            }
            return;
        }
        g_remoteSpecialSlotPhase[slot] = RemoteSpecialSlotPhase::AwaitingLoad;
        g_remoteSlotResidentName[slot].clear();
        previousName.clear();
        {
            std::scoped_lock lock(g_specialCharNamesMutex);
            g_specialCharNames[slot][0] = '\0';
        }
        g_remoteSlotNextRetryMs[slot] = 0;
    }

    const bool oldDifferent = !previousName.empty() && previousName != modelName;
    const bool previousModelLoaded = RemoteSpecialCharacterLoaded(slot);
    if (oldDifferent && previousModelLoaded && g_remoteSlotWasOurs[slot]) {
        CStreaming::SetSpecialCharIsDeletable(slot);
        CStreaming::SetMissionDoesntRequireSpecialChar(slot);
        CStreaming::RemoveModel(static_cast<int>(coop_special::ModelIndexFromSlot(slot)));
        {
            std::scoped_lock lock(g_specialCharNamesMutex);
            g_specialCharNames[slot][0] = '\0';
        }
        g_remoteSlotRequestedName[slot] = gameName;
        g_remoteSlotHeldName[slot] = modelName;
        g_remoteSlotWasOurs[slot] = true;
        g_remoteSpecialSlotPhase[slot] = RemoteSpecialSlotPhase::AwaitingUnload;
        g_remoteSlotNextRetryMs[slot] = now + 100;
        WeaponFileLog("REMOTE_SPECIAL_SLOT_RELOAD slot=%d old=%s new=%s phase=unload", slot,
                      previousName.c_str(), modelName.c_str());
        return;
    }

    if (g_remoteSlotRequestedName[slot] == gameName && now < g_remoteSlotNextRetryMs[slot] &&
        previousName == modelName && g_remoteSpecialSlotPhase[slot] == RemoteSpecialSlotPhase::AwaitingLoad)
        return;

    if (previousName != modelName) {
        std::scoped_lock lock(g_specialCharNamesMutex);
        g_specialCharNames[slot][0] = '\0';
    }
    g_remoteSlotRequestedName[slot] = gameName;
    g_remoteSlotNextRetryMs[slot] = now + 750;
    g_remoteSlotHeldName[slot] = modelName;
    g_remoteSlotWasOurs[slot] = true;
    g_remoteSpecialSlotPhase[slot] = RemoteSpecialSlotPhase::AwaitingLoad;

    g_saUtils->ScriptCommand(&kScmLoadSpecialCharacter, slot, gameName.c_str());
    g_saUtils->ScriptCommand(&kScmLoadAllRequestedModels);
    if (!g_originalRequestSpecialModel && !g_originalRequestSpecialChar) {
        std::scoped_lock lock(g_specialCharNamesMutex);
        std::snprintf(g_specialCharNames[slot].data(), g_specialCharNames[slot].size(), "%s", modelName.c_str());
    }
    const bool loadedNow = RemoteSpecialCharacterLoadedForName(slot, modelName);
    WeaponFileLog("REMOTE_SPECIAL_LOAD_OPCODE slot=%d name=%s previous=%s loaded=%u phase=%u hook=%u",
                  slot, gameName.c_str(), previousName.c_str(), loadedNow ? 1u : 0u,
                  static_cast<unsigned>(g_remoteSpecialSlotPhase[slot]),
                  (g_originalRequestSpecialModel || g_originalRequestSpecialChar) ? 1u : 0u);
}

int EnsureRemoteSpecialSlot(uint64_t ownerKey, const std::string& modelName)
{
    auto& lease = g_remoteSpecialLeases[ownerKey];
    const int localSpecialSlot = CurrentLocalSpecialSlotIndex();
    if (lease.activeSlot && lease.activeSlot != localSpecialSlot && lease.activeName == modelName) return lease.activeSlot;
    if (lease.pendingSlot && lease.pendingSlot != localSpecialSlot && lease.pendingName == modelName) return lease.pendingSlot;
    if (lease.pendingSlot) {
        ReleaseRemoteSpecialSlot(ownerKey, lease.pendingSlot, lease.pendingName);
        lease.pendingSlot = 0;
        lease.pendingName.clear();
    }
    if (!CStreaming::IsInitialised()) return 0;


    for (int slot = coop_special::kLastRemoteSlot; slot >= coop_special::kFirstRemoteSlot; --slot) {
        if (slot != localSpecialSlot && g_remoteSlotRefs[slot] > 0 && g_remoteSlotHeldName[slot] == modelName) {
            ++g_remoteSlotRefs[slot];
            lease.pendingSlot = slot;
            lease.pendingName = modelName;
            return slot;
        }
    }


    auto findFree = [&]() {
        for (int slot = coop_special::kLastRemoteSlot; slot >= coop_special::kFirstRemoteSlot; --slot) {
            if (slot != localSpecialSlot && g_remoteSlotRefs[slot] == 0 && !RemoteSpecialCharacterLoaded(slot)) return slot;
        }
        for (int slot = coop_special::kLastRemoteSlot; slot >= coop_special::kFirstRemoteSlot; --slot) {
            if (slot == localSpecialSlot || g_remoteSlotRefs[slot] != 0 || !RemoteSpecialCharacterLoaded(slot)) continue;
            bool nameMatchesHeld = false;
            nameMatchesHeld = !g_remoteSlotHeldName[slot].empty() &&
                g_remoteSlotHeldName[slot] == modelName && g_remoteSlotWasOurs[slot] &&
                g_remoteSlotResidentName[slot] == modelName;
            if (nameMatchesHeld) return slot;
        }
        for (int slot = coop_special::kLastRemoteSlot; slot >= coop_special::kFirstRemoteSlot; --slot) {
            if (slot != localSpecialSlot && g_remoteSlotRefs[slot] == 0 && RemoteSpecialCharacterLoaded(slot) && g_remoteSlotWasOurs[slot])
                return slot;
        }
        return 0;
    };
    int selected = findFree();
    if (!selected) {




        bool releasedButLoaded = false;
        for (int slot = coop_special::kLastRemoteSlot; slot >= coop_special::kFirstRemoteSlot; --slot)
            if (g_remoteSlotRefs[slot] == 0 && RemoteSpecialCharacterLoaded(slot)) releasedButLoaded = true;
        static uint64_t lastSweepMs = 0;
        const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if (releasedButLoaded && nowMs - lastSweepMs >= 1000) {
            lastSweepMs = nowMs;
            CStreaming::RemoveAllUnusedModels();
            selected = findFree();
        }
    }
    if (!selected) return 0;

    g_remoteSlotRefs[selected] = 1;
    g_remoteSlotHeldName[selected] = modelName;
    lease.pendingSlot = selected;
    lease.pendingName = modelName;
    RequestRemoteSpecialCharacter(selected, modelName);
    CStreaming::LoadAllRequestedModels(false);
    return selected;
}












constexpr uint64_t kRemoteNpcModelRetryMs = 250;
constexpr uint64_t kRemoteNpcModelGiveUpMs = 4000;
constexpr uint64_t kRemoteNpcModelRecheckMs = 5000;

constexpr uint16_t kSpecialLoadWaitKey = 0xFFFF;
constexpr uint16_t kUnnamedSpecialWaitKey = 0xFFFE;

struct RemoteNpcModelWait {
    uint16_t modelId = 0;
    uint64_t firstFailMs = 0;
    uint64_t nextTryMs = 0;
};
std::unordered_map<uint32_t, RemoteNpcModelWait> g_remoteNpcModelWait;
std::unordered_set<uint64_t> g_remoteNpcLogged;

uint64_t SteadyNowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}


bool RemoteNpcMayTryModel(uint32_t npcId, uint16_t modelId)
{
    if (modelId == 0) return true;
    auto it = g_remoteNpcModelWait.find(npcId);
    if (it == g_remoteNpcModelWait.end() || it->second.modelId != modelId) return true;
    return SteadyNowMs() >= it->second.nextTryMs;
}



bool RemoteNpcNoteModelFailure(uint32_t npcId, uint16_t modelId)
{
    const uint64_t now = SteadyNowMs();
    auto& w = g_remoteNpcModelWait[npcId];
    if (w.modelId != modelId) {
        w = RemoteNpcModelWait{};
        w.modelId = modelId;
        w.firstFailMs = now;
    }
    const bool giveUp = now - w.firstFailMs >= kRemoteNpcModelGiveUpMs;
    w.nextTryMs = now + (giveUp ? kRemoteNpcModelRecheckMs : kRemoteNpcModelRetryMs);
    return !giveUp;
}



bool RemoteNpcLogOnce(uint32_t npcId, uint8_t reason, uint16_t detail)
{
    if (g_remoteNpcLogged.size() > 4096) g_remoteNpcLogged.clear();
    const uint64_t key = (static_cast<uint64_t>(npcId) << 24) |
                         (static_cast<uint64_t>(reason) << 16) | detail;
    return g_remoteNpcLogged.insert(key).second;
}

ImU32 NameTagColor(uint8_t colorIndex)
{
    static const ImU32 palette[] = {
        IM_COL32(255, 255, 255, 255),
        IM_COL32(255, 235, 40, 255),
        IM_COL32(70, 235, 90, 255),
        IM_COL32(60, 220, 235, 255),
        IM_COL32(80, 145, 255, 255),
        IM_COL32(200, 100, 255, 255),
        IM_COL32(255, 165, 55, 255),
        IM_COL32(255, 75, 75, 255),
    };
    return palette[colorIndex <= 7 ? colorIndex : 0];
}

void DrawRemoteWeaponIcon(CPed* ped, uint8_t weaponType, float cx, float cy, float iconSize, float alpha = 255.0f) {
    if (!g_drawWeaponIcon || !ped || weaponType > 0x3A) return;
    if (weaponType != 0 && g_getWeaponInfoForIcon && g_requestWeaponIconModel && g_loadWeaponIconModels) {
        void* info = g_getWeaponInfoForIcon(static_cast<int>(weaponType), 1);
        if (info) {

            const int model = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(info) + 0x0C);
            if (model > 0 && model < 30000 && g_streamedWeaponIconModels.find(model) == g_streamedWeaponIconModels.end()) {
                g_requestWeaponIconModel(model, 0x1A);
                g_loadWeaponIconModels(true);
                g_streamedWeaponIconModels.insert(model);
            }
        }
    }
    auto* bytes = reinterpret_cast<uint8_t*>(ped);

    int8_t* activeSlot = reinterpret_cast<int8_t*>(bytes + 0x71C);
    uint32_t* slotZeroType = reinterpret_cast<uint32_t*>(bytes + 0x5A4);
    const int8_t oldSlot = *activeSlot;
    const uint32_t oldWeapon = *slotZeroType;
    *activeSlot = 0;
    *slotZeroType = static_cast<uint32_t>(weaponType);
    const float half = iconSize * 0.5f;
    WeaponIconRect rect{cx - half, cy + half, cx + half, cy - half};

    g_drawWeaponIcon(nullptr, reinterpret_cast<void*>(ped), rect, alpha);
    *activeSlot = oldSlot;
    *slotZeroType = oldWeapon;
}



bool CaptureFireAssociation(CPed* ped, uint16_t& group, uint16_t& animId);

void HookFireInstantHit(void* weapon, void* entity, void* origin, void* target,
                        void* damagedEntity, void* source, void* direction,
                        bool unknown1, bool unknown2)
{
    if (g_originalFireInstantHit)
        g_originalFireInstantHit(weapon, entity, origin, target, damagedEntity, source, direction, unknown1, unknown2);
    if (g_shotImpactHooked) return;
    CPlayerPed* localPlayer = FindPlayerPed(-1);
    if (!g_client || !localPlayer || !g_getBoundCentre) return;
    gtasa_v200_armv7::CVectorAbi centre{};
    g_getBoundCentre(reinterpret_cast<void*>(localPlayer), centre);
    const uint8_t weaponType = CurrentWeaponTypeForPed(localPlayer);
    if (weaponType == 0 || weaponType > 0x3A) return;


    float dx = 0.0f, dy = 1.0f, dz = 0.0f;
    if (direction) {
        const auto* vector = reinterpret_cast<const gtasa_v200_armv7::CVectorAbi*>(direction);
        if (std::isfinite(vector->x) && std::isfinite(vector->y) && std::isfinite(vector->z))
            dx = vector->x, dy = vector->y, dz = vector->z;
    } else if (target && origin) {
        const auto* start = reinterpret_cast<const gtasa_v200_armv7::CVectorAbi*>(origin);
        const auto* end = reinterpret_cast<const gtasa_v200_armv7::CVectorAbi*>(target);
        dx = end->x - start->x; dy = end->y - start->y; dz = end->z - start->z;
    } else if (const auto* matrix = localPlayer->GetMatrix()) {
        const auto& forward = matrix->GetForward();
        dx = forward.x; dy = forward.y; dz = forward.z;
    }
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(length) || length < 0.001f) return;
    dx /= length; dy /= length; dz /= length;


    uint16_t fireGroup = 0, fireAnimId = 0;
    CaptureFireAssociation(localPlayer, fireGroup, fireAnimId);
    g_client->SendShot(weaponType, centre.x, centre.y, centre.z + 0.7f, dx, dy, dz, fireGroup, fireAnimId);
    WeaponFileLog("SHOT_SEND_FIRE_HOOK weapon=%u origin=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f)", weaponType, centre.x, centre.y, centre.z, dx, dy, dz);
}

void HookDoBulletImpact(void* weaponSelf, void* owner, void* victim, ShotVec3* start, ShotVec3* end, void* colPoint, int impactFlags)
{
    CPlayerPed* localPlayer = FindPlayerPed(-1);
    if (!g_replayingNativeShot && g_client && localPlayer && owner && start && end &&
        (owner == static_cast<void*>(localPlayer) || (localPlayer->m_pVehicle && owner == static_cast<void*>(localPlayer->m_pVehicle)))) {
        int weaponId = -1; std::memcpy(&weaponId, weaponSelf, sizeof(weaponId));
        if (weaponId >= 22 && weaponId <= 38 && weaponId != 35 && weaponId != 36 && weaponId != 37) {
            ShotVec3 impact = *end;
            if (colPoint) {
                float hit[3]{}; std::memcpy(hit, colPoint, sizeof(hit));
                const float ds=(hit[0]-start->x)*(hit[0]-start->x)+(hit[1]-start->y)*(hit[1]-start->y)+(hit[2]-start->z)*(hit[2]-start->z);
                const float de=(end->x-start->x)*(end->x-start->x)+(end->y-start->y)*(end->y-start->y)+(end->z-start->z)*(end->z-start->z);
                if (std::isfinite(hit[0])&&std::isfinite(hit[1])&&std::isfinite(hit[2])&&ds>0.01f&&ds<=de+1.0f) impact={hit[0],hit[1],hit[2]};
            }
            const uint64_t now=SteadyNowMs(); const bool shotgun=weaponId==25||weaponId==26||weaponId==27;
            if (!shotgun || now-g_lastShotgunCaptureMs>90) {
                if (shotgun) g_lastShotgunCaptureMs=now;
                uint16_t group=0, anim=0; CaptureFireAssociation(static_cast<CPed*>(localPlayer), group, anim);
                g_client->SendShotEvent(0, static_cast<uint8_t>(weaponId), start->x,start->y,start->z, impact.x,impact.y,impact.z,0.0f,group,anim);
            }
        }
    }
    if (g_originalDoBulletImpact) g_originalDoBulletImpact(weaponSelf,owner,victim,start,end,colPoint,impactFlags);
}

// AML Multiplayer 1.3 combat sync: who hurt the local player last (a remote player's ped, by gun replay or melee),
// so the death is reported with its killer: DEATH|killer|weapon -> KILL to everyone (kill feed, kills / deaths)
uint16_t g_lastAttackerId = 0;
uint8_t g_lastAttackWeapon = 255;
uint64_t g_lastAttackMs = 0;
bool g_localDead = false;

void NoteLocalAttacker(CEntity* creator, int weapon)
{
    if (!creator) return;
    for (const auto& remote : g_remotePeds) {
        if (static_cast<CEntity*>(remote.second) != creator) continue;
        g_lastAttackerId = remote.first;
        g_lastAttackWeapon = static_cast<uint8_t>(weapon >= 0 && weapon <= 255 ? weapon : 255);
        g_lastAttackMs = SteadyNowMs();
        return;
    }
}

void UpdateLocalDeath()
{
    CPlayerPed* localPlayer = FindPlayerPed(-1);
    if (!g_client || !localPlayer || !g_client->IsConnected()) { g_localDead = false; return; }
    const bool dead = localPlayer->m_fHealth <= 0.0f;
    if (dead && !g_localDead) {
        const bool recent = g_lastAttackerId && SteadyNowMs() - g_lastAttackMs < 10000;
        g_client->SendDeath(recent ? g_lastAttackerId : 0, recent ? g_lastAttackWeapon : 255);
        WeaponFileLog("LOCAL_DEATH killer=%u weapon=%u", recent ? g_lastAttackerId : 0, recent ? g_lastAttackWeapon : 255);
        g_lastAttackerId = 0;
    }
    g_localDead = dead;
}

bool HookGenerateDamageEvent(CPed* victim, CEntity* creator, int weapon, int damage, int piece, int direction)
{
    CPlayerPed* localPlayer = FindPlayerPed(-1);
    if (localPlayer && victim == static_cast<CPed*>(localPlayer) && creator && damage > 0) NoteLocalAttacker(creator, weapon);
    if (g_client && localPlayer && victim && creator == static_cast<CEntity*>(localPlayer) &&
        weapon >= 0 && weapon <= 15 && damage > 0) {
        for (const auto& remote : g_remotePeds) {
            if (remote.second != victim || LiveRemotePed(remote.first) != victim) continue;
            uint8_t fightSet = 0xFF;
            uint8_t fightMove = 0xFF;
            if (localPlayer->m_pIntelligence) {
                if (CTaskSimpleFight* fight = localPlayer->m_pIntelligence->GetTaskFighting()) {
                    const auto* bytes = reinterpret_cast<const uint8_t*>(fight);
                    fightSet = bytes[0x24];
                    fightMove = bytes[0x25];
                }
            }
            const uint8_t safePiece = static_cast<uint8_t>(std::max(0, std::min(9, piece)));
            const uint8_t safeDirection = static_cast<uint8_t>(std::max(0, std::min(3, direction)));
            g_client->SendPlayerHit(remote.first, static_cast<uint8_t>(weapon),
                                    static_cast<uint16_t>(std::min(10000, damage)),
                                    safePiece, safeDirection, fightSet, fightMove);
            WeaponFileLog("PLAYER_MELEE_HIT_SEND target=%u weapon=%d damage=%d piece=%d direction=%d fight=%u/%u",
                          remote.first, weapon, damage, piece, direction, fightSet, fightMove);
            return true;
        }
    }

    GenerateDamageEventFn original = g_originalGenerateDamageEvent
        ? g_originalGenerateDamageEvent : g_resolvedGenerateDamageEvent;
    return original ? original(victim, creator, weapon, damage, piece, direction) : true;
}

class RemoteFightAs {
public:
    RemoteFightAs(CPed* hitter, uint8_t comboSet, uint8_t move)
    {
        if (!hitter || !hitter->m_pIntelligence || !g_fightTaskVtableAddressPoint ||
            comboSet < 4 || comboSet > 16 || move > 4) return;
        auto* intelligence = reinterpret_cast<uint8_t*>(hitter->m_pIntelligence);
        m_slot = reinterpret_cast<void**>(intelligence + 4 + 0x14);
        m_saved = *m_slot;
        const uintptr_t vtable = g_fightTaskVtableAddressPoint;
        std::memcpy(m_task, &vtable, sizeof(vtable));
        m_task[0x24] = comboSet;
        m_task[0x25] = move;
        *m_slot = m_task;
    }
    ~RemoteFightAs() { if (m_slot) *m_slot = m_saved; }
    RemoteFightAs(const RemoteFightAs&) = delete;
    RemoteFightAs& operator=(const RemoteFightAs&) = delete;
private:
    void** m_slot = nullptr;
    void* m_saved = nullptr;
    alignas(4) uint8_t m_task[0x28]{};
};

void ApplyPendingRemotePlayerHits()
{
    if (!g_client) return;
    for (const auto& hit : g_client->DrainPlayerHits()) {
        if (g_pendingRemotePlayerHits.size() >= kMaxPendingRemotePlayerHits)
            g_pendingRemotePlayerHits.pop_front();
        g_pendingRemotePlayerHits.push_back({hit, SteadyNowMs() + kRemotePlayerHitRetryLifetimeMs});
    }
    CPlayerPed* localPlayer = FindPlayerPed(-1);
    if (!localPlayer) return;
    const uint64_t now = SteadyNowMs();
    for (auto it = g_pendingRemotePlayerHits.begin(); it != g_pendingRemotePlayerHits.end();) {
        if (now >= it->expiresAtMs) {
            WeaponFileLog("PLAYER_MELEE_HIT_EXPIRED attacker=%u weapon=%u", it->hit.attackerId, it->hit.weapon);
            it = g_pendingRemotePlayerHits.erase(it);
            continue;
        }
        CPed* attacker = LiveRemotePed(it->hit.attackerId);
        if (!attacker) { ++it; continue; }
        GenerateDamageEventFn original = g_originalGenerateDamageEvent
            ? g_originalGenerateDamageEvent : g_resolvedGenerateDamageEvent;
        if (!original) { ++it; continue; }
        {
            RemoteFightAs fight(attacker, it->hit.fightSet, it->hit.fightMove);
            NoteLocalAttacker(static_cast<CEntity*>(attacker), it->hit.weapon);
            const bool accepted = original(localPlayer, attacker, it->hit.weapon, it->hit.damage,
                                           it->hit.piece, it->hit.direction);
            WeaponFileLog("PLAYER_MELEE_HIT_APPLY attacker=%u weapon=%u damage=%u piece=%u direction=%u accepted=%d fight=%u/%u",
                          it->hit.attackerId, it->hit.weapon, it->hit.damage, it->hit.piece,
                          it->hit.direction, accepted ? 1 : 0, it->hit.fightSet, it->hit.fightMove);
        }
        it = g_pendingRemotePlayerHits.erase(it);
    }
}

int ProjectileModelForWeapon(int w) {
    switch(w) { case 16:return 342; case 17:return 343; case 18:return 344; case 19: case 20: case 35: case 36:return 345; default:return -1; }
}

bool HookAddProjectile(void* creator, int weaponId, ShotVec3 position, float force, ShotVec3* direction, void* victim)
{
    CPlayerPed* localPlayer = FindPlayerPed(-1);
    const bool localPedCreator = localPlayer && creator == static_cast<void*>(localPlayer);
    const bool localVehicleCreator = localPlayer && localPlayer->m_pVehicle &&
        creator == static_cast<void*>(localPlayer->m_pVehicle);
    if (!g_replayingNativeShot && g_client && localPlayer && creator &&
        (localPedCreator || localVehicleCreator) &&
        std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)) {
        ShotVec3 capturedDirection{0.0f, 1.0f, 0.0f};
        bool haveDirection = false;
        if (direction && std::isfinite(direction->x) && std::isfinite(direction->y) && std::isfinite(direction->z)) {
            capturedDirection = *direction;
            haveDirection = (std::fabs(direction->x) + std::fabs(direction->y) + std::fabs(direction->z)) > 0.0001f;
        }
        if (!haveDirection) {
            auto* placeable = static_cast<CPlaceable*>(creator);
            if (const CMatrix* matrix = placeable->GetMatrix()) {
                const auto& forward = matrix->GetForward();
                if (std::isfinite(forward.x) && std::isfinite(forward.y) && std::isfinite(forward.z) &&
                    (std::fabs(forward.x) + std::fabs(forward.y) + std::fabs(forward.z)) > 0.0001f) {
                    capturedDirection = {forward.x, forward.y, forward.z};
                    haveDirection = true;
                }
            }
        }
        if (haveDirection) {
            uint16_t group = 0, anim = 0;
            CaptureFireAssociation(static_cast<CPed*>(localPlayer), group, anim);
            const uint8_t kind = localPedCreator ? 1 : 2;
            uint16_t networkVehicleId = 0;
            if (kind == 2 && localPlayer->m_pVehicle) {
                const uint32_t localVehicleKey = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(localPlayer->m_pVehicle));
                networkVehicleId = g_client->LocalVehicleNetworkIdForKey(localVehicleKey);
            }
            g_client->SendShotEvent(kind, static_cast<uint8_t>(weaponId), position.x, position.y, position.z,
                                    capturedDirection.x, capturedDirection.y, capturedDirection.z,
                                    force, group, anim, networkVehicleId);
            WeaponFileLog("LOCAL_PROJECTILE_CAPTURE kind=%u weapon=%u model=%d vehicle=%u dirSource=%s force=%.2f pos=(%.2f,%.2f,%.2f) dir=(%.3f,%.3f,%.3f)",
                          kind, static_cast<unsigned>(weaponId), ProjectileModelForWeapon(weaponId),
                          static_cast<unsigned>(networkVehicleId), direction ? "native" : "creator-matrix", force,
                          position.x, position.y, position.z, capturedDirection.x, capturedDirection.y, capturedDirection.z);
        } else {
            WeaponFileLog("LOCAL_PROJECTILE_CAPTURE_SKIPPED weapon=%u reason=no-direction", static_cast<unsigned>(weaponId));
        }
    }
    return g_originalAddProjectile ? g_originalAddProjectile(creator, weaponId, position, force, direction, victim) : false;
}

bool HookAddExplosion(void* victim, void* creator, int type, ShotVec3 pos,
                      uint32_t time, uint8_t sound, float shake, uint8_t visibility) {
    CPlayerPed* localPlayer = FindPlayerPed(-1);
    if (!g_replayingNativeExplosion && g_client && localPlayer && type >= 0 && type <= 12 &&
        std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z)) {
        const void* localPed = static_cast<void*>(localPlayer);
        const void* localVehicle = localPlayer->m_pVehicle ? static_cast<void*>(localPlayer->m_pVehicle) : nullptr;
        if (creator && (creator == localPed || creator == localVehicle)) {
            const uint64_t now = SteadyNowMs();
            if (now - g_lastExplosionCaptureMs >= 80) {
                g_lastExplosionCaptureMs = now;
                g_client->SendExplosionEvent(static_cast<uint8_t>(type), pos.x, pos.y, pos.z);
                WeaponFileLog("LOCAL_EXPLOSION_CAPTURE type=%d pos=(%.2f,%.2f,%.2f)", type, pos.x, pos.y, pos.z);
            }
        }
    }
    return g_originalAddExplosion ? g_originalAddExplosion(victim, creator, type, pos, time, sound, shake, visibility) : false;
}

float AngleDelta(float target, float current)
{
    while (target - current > 180.0f) target -= 360.0f;
    while (target - current < -180.0f) target += 360.0f;
    return target - current;
}

float SmoothValue(float current, float target, float factor)
{
    return current + (target - current) * factor;
}

float HeadingDegrees(CPlaceable* placeable)
{
    if (!placeable) return 0.0f;
    float heading = placeable->m_placement.m_fHeading * 57.2957795f;
    if (auto* matrix = placeable->GetMatrix()) {
        const auto& forward = matrix->GetForward();
        if (std::isfinite(forward.x) && std::isfinite(forward.y) &&
            (std::fabs(forward.x) > 0.0001f || std::fabs(forward.y) > 0.0001f))
            heading = std::atan2(-forward.x, forward.y) * 57.2957795f;
    }
    if (!std::isfinite(heading)) return 0.0f;
    heading = std::fmod(heading, 360.0f);
    return heading < 0.0f ? heading + 360.0f : heading;
}













CVehicle* GetSeatedVehicle(CPed* ped, uint8_t* outSeat = nullptr)
{
    if (outSeat) *outSeat = 0;
    if (!ped) return nullptr;
    CVehicle* vehicle = ped->m_pVehicle;
    if (!vehicle) return nullptr;
    if (vehicle->m_pDriver == ped) return vehicle;
    for (uint8_t si = 0; si < 8; ++si) {
        if (vehicle->m_apPassengers[si] == ped) {
            if (outSeat) *outSeat = static_cast<uint8_t>(si + 1);
            return vehicle;
        }
    }
    if (ped->bInVehicle) return vehicle;
    return nullptr;
}

















struct LocalAssociationGate {
    static constexpr uint64_t kExitSettleMs = 1500;
    CVehicle* lastSeated = nullptr;
    uint64_t unseatedSinceMs = 0;



    bool ShouldSuppress(CVehicle* seated, CVehicle* rawPtr, uint64_t nowMs)
    {
        if (seated) { lastSeated = seated; unseatedSinceMs = 0; return true; }
        if (!rawPtr) { lastSeated = nullptr; unseatedSinceMs = 0; return false; }
        if (rawPtr != lastSeated) { unseatedSinceMs = 0; return true; }
        if (!unseatedSinceMs) unseatedSinceMs = nowMs;
        return nowMs - unseatedSinceMs < kExitSettleMs;
    }
};

AnimationProfile CaptureAnimationProfile(CPed* ped, uint32_t sequence)
{
    AnimationProfile profile{};
    if (!ped) return profile;
    profile.sequence = sequence;
    profile.locomotion = static_cast<uint8_t>(std::max(0, std::min(7, static_cast<int>(ped->m_eMoveStateAnim))));
    profile.animGroup = static_cast<uint16_t>(std::min(0xFFFFu, ped->m_nAnimGroup));
    profile.weaponState = 0;
    const unsigned slot = static_cast<unsigned>(ped->m_nSelectedWepSlot);
    if (slot < 13) profile.weaponState = static_cast<uint8_t>(ped->m_aWeapons[slot].m_nState);
    profile.aimYaw = std::isfinite(ped->m_fAimingRotation) ? ped->m_fAimingRotation : 0.0f;
    profile.aimPitch = std::isfinite(ped->m_pedIK.m_TorsoOrien.m_fPitch) ? ped->m_pedIK.m_TorsoOrien.m_fPitch : 0.0f;
    profile.blend = 1.0f;
    profile.phase = 0.0f;
    profile.flags = ANIM_ON_GROUND | ANIM_LOOPING;


    uint8_t profileSeat = 0;
    CVehicle* profileVehicle = GetSeatedVehicle(ped, &profileSeat);
    if (profileVehicle) profile.flags |= ANIM_IN_VEHICLE;
    if (ped->bIsDucking) profile.flags |= ANIM_DUCKING;
    if (ped->GetHealth() <= 0.0f || ped->m_ePedState == PEDSTATE_DIE || ped->m_ePedState == PEDSTATE_DEAD) {
        profile.action = AnimationAction::Death; profile.flags |= ANIM_DEAD;
    } else if (profileVehicle) {
        profile.action = profileSeat == 0 ? AnimationAction::Driving : AnimationAction::Passenger;
    } else {
        switch (ped->m_ePedState) {
            case PEDSTATE_JUMP: profile.action = AnimationAction::Jump; break;
            case PEDSTATE_FALL: profile.action = AnimationAction::Fall; break;
            case PEDSTATE_GETUP: profile.action = AnimationAction::GetUp; break;
            case PEDSTATE_ATTACK: profile.action = AnimationAction::Attack; profile.flags |= ANIM_UPPER_BODY | ANIM_FIRING; break;
            case PEDSTATE_AIMGUN: profile.action = AnimationAction::Aim; profile.flags |= ANIM_UPPER_BODY | ANIM_AIMING; break;
            case PEDSTATE_HANDS_UP: profile.action = AnimationAction::HandsUp; break;
            case PEDSTATE_FACE_PHONE:
            case PEDSTATE_MAKE_PHONECALL:
            case PEDSTATE_ANSWER_MOBILE: profile.action = AnimationAction::Phone; break;
            case PEDSTATE_CHAT: profile.action = AnimationAction::Chat; break;
            default: profile.action = profile.locomotion == kMoveStill ? AnimationAction::Idle : AnimationAction::Locomotion; break;
        }
        if (ped->bIsDucking && profile.action == AnimationAction::Idle) profile.action = AnimationAction::Crouch;
    }



    if (!profileVehicle && profile.weaponState == static_cast<uint8_t>(WEAPONSTATE_FIRING)) {
        profile.flags |= ANIM_FIRING | ANIM_UPPER_BODY;
        if (profile.action == AnimationAction::Idle || profile.action == AnimationAction::Locomotion)
            profile.action = AnimationAction::Attack;
    }
    return profile;
}

bool IsFirstPersonWeaponAimMode(eCamMode mode)
{
    return mode == MODE_SNIPER || mode == MODE_M16_1STPERSON ||
           mode == MODE_ROCKETLAUNCHER || mode == MODE_ROCKETLAUNCHER_HS ||
           mode == MODE_HELICANNON_1STPERSON || mode == MODE_CAMERA;
}

bool IsWeaponAimCameraMode(eCamMode mode)
{
    return mode == MODE_AIMING || mode == MODE_AIMWEAPON ||
           mode == MODE_AIMWEAPON_ATTACHED || mode == MODE_AIMWEAPON_FROMCAR ||
           IsFirstPersonWeaponAimMode(mode);
}

void CaptureLocalAimDirection(CPed* ped, AnimationProfile& profile)
{
    if (!ped) return;
    const eCamMode cameraMode = TheCamera.GetActiveMode();
    const bool firstPerson = IsFirstPersonWeaponAimMode(cameraMode);
    const bool cameraAiming = IsWeaponAimCameraMode(cameraMode);
    if (cameraAiming) profile.flags |= ANIM_AIMING | ANIM_UPPER_BODY;
    if (firstPerson) profile.flags |= ANIM_AIM_FIRST_PERSON;
    else profile.flags &= static_cast<uint16_t>(~ANIM_AIM_FIRST_PERSON);

    const bool aiming = (profile.flags & (ANIM_AIMING | ANIM_FIRING)) != 0;
    if (!aiming) {
        profile.aimPitch = 0.0f;
        return;
    }

    const CVector& forward = TheCamera.GetActiveCam().m_vecFront;
    const float horizontal2 = forward.x * forward.x + forward.y * forward.y;
    if (std::isfinite(horizontal2) && horizontal2 > 1.0e-8f &&
        std::isfinite(forward.z)) {
        profile.aimYaw = coop_aim::YawFromDirection(forward.x, forward.y);
        profile.aimPitch = coop_aim::ClampPitch(
            coop_aim::PitchFromDirection(forward.x, forward.y, forward.z),
            coop_aim::kMaxPlayerPitch);
    } else {
        const CVector& matrixForward = TheCamera.m_mCameraMatrix.GetForward();
        profile.aimYaw = coop_aim::YawFromDirection(matrixForward.x, matrixForward.y);
        profile.aimPitch = coop_aim::ClampPitch(
            coop_aim::PitchFromDirection(matrixForward.x, matrixForward.y, matrixForward.z),
            coop_aim::kMaxPlayerPitch);
    }

    if (cameraAiming && profile.action != AnimationAction::Attack)
        profile.action = AnimationAction::Aim;
}

void ApplyRemoteAnimationProfile(CPed* ped, const AnimationProfile& profile, bool inVehicle, bool shooting)
{
    if (!ped) return;

    if (!inVehicle && !(profile.flags & ANIM_IN_VEHICLE) && g_setMoveState && g_setMoveAnim) {
        const int move = shooting ? kMoveStill : static_cast<int>(profile.locomotion <= kMoveSprint ? profile.locomotion : kMoveStill);
        g_setMoveState(ped, move);
        g_setMoveAnim(ped);
    }

}

bool IsValidAnimationGroupIndex(int group);

void ApplyRemoteAnimationGroup(CPed* ped, const AnimationProfile& profile)
{
    if (!ped || !IsValidAnimationGroupIndex(static_cast<int>(profile.animGroup))) return;
    if (ped->m_nAnimGroup != profile.animGroup)
        ped->m_nAnimGroup = profile.animGroup;
}

constexpr uintptr_t kAssocGroup = 0x0E, kAssocBlend = 0x18, kAssocBlendDelta = 0x1C, kAssocCurrentTime = 0x20, kAssocSpeed = 0x24, kAssocTimeStep = 0x28, kAssocAnimId = 0x2C, kAssocFlags = 0x2E;
constexpr uint16_t kAnimAssocFlagsStartedLooped = 0x0003;
constexpr uint16_t kAnimAssocFlagRetireWhenFaded = 0x0004;

struct AnimAssocGroupView {
    void* animBlock;
    void* associations;
    uint32_t numAnimations;
    int32_t idOffset;
    int32_t groupId;
};
static_assert(sizeof(AnimAssocGroupView) == 0x14, "Unexpected ARMv7 animation group layout");

bool IsValidAnimationGroupIndex(int group) {
    if (!g_game || group < 0 || group >= kMaximumAnimationGroups) return false;
    const int32_t count = *reinterpret_cast<const int32_t*>(g_game + kNumAnimAssocDefinitionsOffset);
    return count > 0 && count <= kMaximumAnimationGroups && group < count;
}

bool IsHierarchyEntry(uintptr_t h) {
    if (!g_game) return false;
    const uintptr_t base = g_game + kAnimHierarchyArrayOffset;
    return h >= base && h + kAnimHierarchyStride <= base + kAnimHierarchyStride * kAnimHierarchyCount &&
           ((h - base) % kAnimHierarchyStride) == 0;
}

bool HierarchyLooksLoaded(uintptr_t h) {
    if (!IsHierarchyEntry(h)) return false;
    const uint32_t hash = *reinterpret_cast<const uint32_t*>(h);
    const uint32_t sequences = *reinterpret_cast<const uint32_t*>(h + 4);
    const uint16_t sequenceCount = *reinterpret_cast<const uint16_t*>(h + 8);
    float totalTime = 0.0f;
    std::memcpy(&totalTime, reinterpret_cast<const void*>(h + 0xC), sizeof(totalTime));
    return hash != 0 && sequences != 0 && sequenceCount > 0 && sequenceCount <= 256 &&
           std::isfinite(totalTime) && totalTime > 0.0f && totalTime < 3600.0f;
}

void* FindHierarchyByHash(uint32_t hash) {
    if (!g_game || hash == 0) return nullptr;
    const int32_t used = *reinterpret_cast<const int32_t*>(g_game + kNumAnimationsOffset);
    if (used <= 0 || static_cast<uintptr_t>(used) > kAnimHierarchyCount) return nullptr;
    const uintptr_t base = g_game + kAnimHierarchyArrayOffset;
    for (int32_t i = 0; i < used; ++i) {
        const uintptr_t h = base + static_cast<uintptr_t>(i) * kAnimHierarchyStride;
        if (*reinterpret_cast<const uint32_t*>(h) == hash && HierarchyLooksLoaded(h))
            return reinterpret_cast<void*>(h);
    }
    return nullptr;
}

bool IsUngroupedAnimation(const AnimationAssociationState& s) {
    return s.hierarchyHash != 0 || s.group == 0xFFFFu || s.animationId < 0;
}

bool GroupAnimationMetadataReady(const AnimationAssociationState& s) {
    const int group = static_cast<int>(s.group);
    if (!IsValidAnimationGroupIndex(group) || s.animationId < 0) return false;
    const auto* groups = *reinterpret_cast<AnimAssocGroupView* const*>(g_game + kAnimAssocGroupsPtrOffset);
    if (!groups) return false;
    const AnimAssocGroupView& g = groups[group];
    if (!g.associations || !g.animBlock || g.numAnimations > 65536) return false;
    const int32_t index = static_cast<int32_t>(s.animationId) - g.idOffset;
    return index >= 0 && static_cast<uint32_t>(index) < g.numAnimations;
}

void ReadAssociation(void* p, AnimationAssociationState& s) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    s.group = *reinterpret_cast<uint16_t*>(a + kAssocGroup);
    s.animationId = *reinterpret_cast<int16_t*>(a + kAssocAnimId);
    s.flags = *reinterpret_cast<uint16_t*>(a + kAssocFlags);
    s.blend = *reinterpret_cast<float*>(a + kAssocBlend);
    s.blendDelta = *reinterpret_cast<float*>(a + kAssocBlendDelta);
    s.currentTime = *reinterpret_cast<float*>(a + kAssocCurrentTime);
    s.speed = *reinterpret_cast<float*>(a + kAssocSpeed);
    s.timeStep = *reinterpret_cast<float*>(a + kAssocTimeStep);
    s.hierarchyHash = 0;
    if (s.group == 0xFFFFu || s.animationId < 0) {
        const uintptr_t hierarchy = *reinterpret_cast<const uintptr_t*>(a + kAssocHierarchy);
        if (HierarchyLooksLoaded(hierarchy))
            s.hierarchyHash = *reinterpret_cast<const uint32_t*>(hierarchy);
    }
}
[[maybe_unused]] void WriteAssociation(void* p,const AnimationAssociationState& s){const uintptr_t a=reinterpret_cast<uintptr_t>(p);*reinterpret_cast<float*>(a+kAssocBlend)=s.blend;*reinterpret_cast<float*>(a+kAssocBlendDelta)=s.blendDelta;*reinterpret_cast<float*>(a+kAssocCurrentTime)=s.currentTime;*reinterpret_cast<float*>(a+kAssocSpeed)=s.speed;*reinterpret_cast<float*>(a+kAssocTimeStep)=s.timeStep;*reinterpret_cast<uint16_t*>(a+kAssocFlags)=s.flags;}













using SetAssocBlendFn = void (*)(void*, float, float);
SetAssocBlendFn g_setAssocBlend = nullptr;
constexpr float kAssocFadeRate = 8.0f;


uint8_t CaptureActiveAssociations(CPed* ped, AnimationAssociationState* out)
{
    if (!ped || !ped->m_pRwClump || !g_getFirstAssociation || !g_getNextAssociation || !out) return 0;
    uint8_t n = 0;
    for (void* a = g_getFirstAssociation(ped->m_pRwClump); a && n < 64; a = g_getNextAssociation(a)) {
        AnimationAssociationState state{};
        ReadAssociation(a, state);
        if (state.blend <= 0.001f) continue;
        if (!std::isfinite(state.blend) || !std::isfinite(state.blendDelta) ||
            !std::isfinite(state.currentTime) || !std::isfinite(state.speed) ||
            !std::isfinite(state.timeStep)) continue;
        out[n++] = state;
    }
    return n;
}




bool EnsureAnimationBlocksReady(uint16_t remotePlayerId,
                                const AnimationAssociationState* states,
                                unsigned count)
{
    if (!states || count == 0) return true;

    if (!g_getAnimBlockName || !g_getAnimationBlockIndex || !g_getAnimationBlock) {
        static bool loggedMissingApi = false;
        if (!loggedMissingApi) {
            loggedMissingApi = true;
            WeaponFileLog("ANIM_SYNC_STREAM_GATE_UNAVAILABLE getBlockName=%d getBlockIndex=%d getBlock=%d; using v11 playback without readiness gate",
                          g_getAnimBlockName != nullptr, g_getAnimationBlockIndex != nullptr,
                          g_getAnimationBlock != nullptr);
        }
        return true;
    }

    const uint64_t now = SteadyNowMs();
    bool allReady = true;
    bool requestedAny = false;
    std::array<bool, 1025> checkedBlocks{};

    for (unsigned i = 0; i < count; ++i) {
        const AnimationAssociationState& state = states[i];
        if (IsUngroupedAnimation(state)) {
            if (!FindHierarchyByHash(state.hierarchyHash)) {
                allReady = false;
                const uint64_t key = (static_cast<uint64_t>(state.hierarchyHash) << 1) | 1u;
                if (g_animationBlockWaitLogged.insert(static_cast<int>(key ^ (key >> 32))).second)
                    WeaponFileLog("ANIM_SYNC_WAIT_HIERARCHY remote=%u hash=%08x; retaining animation for retry",
                                  static_cast<unsigned>(remotePlayerId), static_cast<unsigned>(state.hierarchyHash));
            }
            continue;
        }
        const int group = static_cast<int>(state.group);
        if (!IsValidAnimationGroupIndex(group)) {
            if (g_animationBlockLookupErrorLogged.insert(-1000 - group).second)
                WeaponFileLog("ANIM_SYNC_BAD_GROUP remote=%u group=%d; deferring invalid association set",
                              static_cast<unsigned>(remotePlayerId), group);
            return false;
        }

        const char* blockName = g_getAnimBlockName(group);
        if (!blockName || !blockName[0]) {
            if (g_animationBlockLookupErrorLogged.insert(group).second)
                WeaponFileLog("ANIM_SYNC_BLOCK_NAME_MISSING remote=%u group=%d; association set deferred",
                              static_cast<unsigned>(remotePlayerId), group);
            return false;
        }

        const int blockIndex = g_getAnimationBlockIndex(blockName);
        if (blockIndex < 0 || blockIndex > 1024) {
            const int key = 2000 + group;
            if (g_animationBlockLookupErrorLogged.insert(key).second)
                WeaponFileLog("ANIM_SYNC_BLOCK_INDEX_MISSING remote=%u group=%d block=%s index=%d; association set deferred",
                              static_cast<unsigned>(remotePlayerId), group, blockName, blockIndex);
            return false;
        }
        if (checkedBlocks[static_cast<size_t>(blockIndex)]) continue;
        checkedBlocks[static_cast<size_t>(blockIndex)] = true;

        void* block = g_getAnimationBlock(blockName);
        const bool loaded = block &&
            (*reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(block) + kAnimationBlockLoadedOffset) != 0);
        if (loaded) {
            g_animationBlockNextRequestMs.erase(blockIndex);
            if (g_animationBlockWaitLogged.count(blockIndex) &&
                g_animationBlockReadyLogged.insert(blockIndex).second) {
                WeaponFileLog("ANIM_SYNC_BLOCK_READY remote=%u group=%d block=%s blockIndex=%d",
                              static_cast<unsigned>(remotePlayerId), group, blockName, blockIndex);
            }
            continue;
        }

        allReady = false;
        uint64_t& nextRequest = g_animationBlockNextRequestMs[blockIndex];
        if (now >= nextRequest) {
            const int streamingModel = kAnimationStreamingModelBase + blockIndex;
            if (streamingModel >= kAnimationStreamingModelBase && streamingModel < 30000) {
                CStreaming::RequestModel(streamingModel, 2);
                requestedAny = true;
            }
            nextRequest = now + 250;
            if (g_animationBlockWaitLogged.insert(blockIndex).second) {
                WeaponFileLog("ANIM_SYNC_WAIT_BLOCK remote=%u group=%d block=%s blockIndex=%d streamingModel=%d",
                              static_cast<unsigned>(remotePlayerId), group, blockName, blockIndex,
                              streamingModel);
            }
        }
    }

    if (requestedAny) CStreaming::LoadAllRequestedModels(false);
    return allReady;
}

void ApplyAssociationStates(CPed* ped, uint32_t remoteEntityId,
                            const AnimationAssociationState* states, unsigned associationCount,
                            uint8_t& visualAssociationCount,
                            std::array<AnimationAssociationState,64>& visualAssociations,
                            bool& visualAnimationInitialized)
{
    if (!ped || !ped->m_pRwClump || !g_getFirstAssociation || !g_getNextAssociation) return;

    RpClump* clump = ped->m_pRwClump;
    const unsigned n = std::min<unsigned>(associationCount, 64);

    if (n == 0) {
        if (visualAssociationCount && g_setAssocBlend) {
            const unsigned oldCount = std::min<unsigned>(visualAssociationCount, 64);
            for (unsigned i = 0; i < oldCount; ++i) {
                const auto& old = visualAssociations[i];
                for (void* a = g_getFirstAssociation(clump); a; a = g_getNextAssociation(a)) {
                    AnimationAssociationState current{};
                    ReadAssociation(a, current);
                    if (current.group == old.group && current.animationId == old.animationId) {
                        g_setAssocBlend(a, 0.0f, -kAssocFadeRate);
                        break;
                    }
                }
            }
        }
        visualAssociationCount = 0;
        visualAnimationInitialized = false;
        return;
    }

    if (!EnsureAnimationBlocksReady(static_cast<uint16_t>(remoteEntityId & 0xFFFFu), states, n)) return;

    bool changed = !visualAnimationInitialized || visualAssociationCount != n;
    if (!changed) {
        for (unsigned i = 0; i < n; ++i) {
            if (visualAssociations[i].group != states[i].group ||
                visualAssociations[i].animationId != states[i].animationId ||
                visualAssociations[i].hierarchyHash != states[i].hierarchyHash) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) {
        for (unsigned i = 0; i < n && !changed; ++i) {
            bool found = false;
            for (void* a = g_getFirstAssociation(clump); a; a = g_getNextAssociation(a)) {
                AnimationAssociationState current{};
                ReadAssociation(a, current);
                if (IsUngroupedAnimation(states[i])) {
                    const uintptr_t hierarchy = *reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(a) + kAssocHierarchy);
                    found = states[i].hierarchyHash != 0 && HierarchyLooksLoaded(hierarchy) &&
                            *reinterpret_cast<const uint32_t*>(hierarchy) == states[i].hierarchyHash;
                } else {
                    found = current.group == states[i].group && current.animationId == states[i].animationId;
                }
                if (found) break;
            }
            if (!found) changed = true;
        }
    }

    if (changed && g_removeAllAssociations) {
        bool allTargetsSafe = true;
        for (unsigned i = 0; i < n; ++i) {
            if (IsUngroupedAnimation(states[i])) {
                if (!g_blendAnimationByHierarchy || !FindHierarchyByHash(states[i].hierarchyHash)) allTargetsSafe = false;
            } else if (!g_blendAnimation || !GroupAnimationMetadataReady(states[i])) {
                allTargetsSafe = false;
            }
        }
        if (!allTargetsSafe) return;
        g_removeAllAssociations(clump);
        for (unsigned i = 0; i < n; ++i) {
            const auto& state = states[i];
            if (IsUngroupedAnimation(state)) {
                void* hierarchy = FindHierarchyByHash(state.hierarchyHash);
                if (hierarchy) g_blendAnimationByHierarchy(clump, hierarchy, static_cast<int>(state.flags), kAssocFadeRate);
            } else {
                if (GroupAnimationMetadataReady(state))
                    g_blendAnimation(clump, static_cast<int>(state.group), static_cast<int>(state.animationId), kAssocFadeRate);
            }
        }
    }

    for (void* a = g_getFirstAssociation(clump); a; a = g_getNextAssociation(a)) {
        AnimationAssociationState current{};
        ReadAssociation(a, current);
        const uintptr_t associationAddress = reinterpret_cast<uintptr_t>(a);
        const uintptr_t hierarchy = *reinterpret_cast<const uintptr_t*>(associationAddress + kAssocHierarchy);
        for (unsigned i = 0; i < n; ++i) {
            const bool match = IsUngroupedAnimation(states[i])
                ? (states[i].hierarchyHash != 0 && HierarchyLooksLoaded(hierarchy) &&
                   *reinterpret_cast<const uint32_t*>(hierarchy) == states[i].hierarchyHash)
                : (current.group == states[i].group && current.animationId == states[i].animationId);
            if (match) {
                WriteAssociation(a, states[i]);
                break;
            }
        }
    }

    visualAssociationCount = static_cast<uint8_t>(n);
    for (unsigned i = 0; i < n; ++i) visualAssociations[i] = states[i];
    visualAnimationInitialized = true;
}













constexpr bool kBlockRemotePedEvents = true;
constexpr bool kBlockRemotePedTasks = true;

using ScanForEventsFn = void (*)(void*, CPed*);
using IntelligenceFn = void (*)(void*);
ScanForEventsFn g_originalScanForEvents = nullptr;
IntelligenceFn g_originalProcessEventHandler = nullptr;
IntelligenceFn g_originalIntelligenceProcess = nullptr;

uint64_t NowSteadyMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool IsRemotePlayerPed(const CPed* ped)
{
    if (!ped) return false;
    for (const auto& entry : g_remotePeds)
        if (entry.second == ped) return true;
    return false;
}

bool IsRemotePedTaskPermitted(const CPed* ped)
{
    auto it = g_remoteTaskPermitUntilMs.find(ped);
    if (it == g_remoteTaskPermitUntilMs.end()) return false;
    if (NowSteadyMs() >= it->second) {
        g_remoteTaskPermitUntilMs.erase(it);
        return false;
    }
    return true;
}

bool IsRemoteCarjackVictimProtected(const CPed* ped)
{
    if (!ped) return false;
    auto it = g_remoteCarjackVictimUntilMs.find(ped);
    if (it == g_remoteCarjackVictimUntilMs.end()) return false;
    if (NowSteadyMs() >= it->second) {
        g_remoteCarjackVictimUntilMs.erase(it);
        return false;
    }
    return true;
}

CPed* OwnerOfIntelligence(const void* intelligence)
{
    return intelligence ? *reinterpret_cast<CPed* const*>(intelligence) : nullptr;
}

void HookScanForEvents(void* scanner, CPed* ped)
{
    if (kBlockRemotePedEvents && IsRemotePlayerPed(ped) && !IsRemotePedTaskPermitted(ped)) return;
    if (g_originalScanForEvents) g_originalScanForEvents(scanner, ped);
}

void HookProcessEventHandler(void* intelligence)
{
    CPed* owner = OwnerOfIntelligence(intelligence);
    if (kBlockRemotePedEvents && IsRemotePlayerPed(owner) && !IsRemotePedTaskPermitted(owner)) return;
    if (g_originalProcessEventHandler) g_originalProcessEventHandler(intelligence);
}

void HookIntelligenceProcess(void* intelligence)
{
    CPed* owner = OwnerOfIntelligence(intelligence);
    if (kBlockRemotePedTasks && IsRemotePlayerPed(owner) && owner && owner->m_fHealth > 0.0f) {
        const uint64_t now = NowSteadyMs();
        auto permit = g_remoteTaskPermitUntilMs.find(owner);
        if (permit != g_remoteTaskPermitUntilMs.end() && now < permit->second) {


            if (g_originalIntelligenceProcess) g_originalIntelligenceProcess(intelligence);
            return;
        }
        if (permit != g_remoteTaskPermitUntilMs.end()) g_remoteTaskPermitUntilMs.erase(permit);



        if (GetSeatedVehicle(owner)) {
            if (g_originalIntelligenceProcess) g_originalIntelligenceProcess(intelligence);
        }
        return;
    }
    if (g_originalIntelligenceProcess) g_originalIntelligenceProcess(intelligence);
}

void InstallRemotePedAiHooks()
{
    void* lib = aml->GetLibHandle("libGTASA.so");
    if (!lib) return;
    auto scan = aml->GetSym(lib, "_ZN13CEventScanner13ScanForEventsER4CPed");
    auto events = aml->GetSym(lib, "_ZN16CPedIntelligence19ProcessEventHandlerEv");
    auto process = aml->GetSym(lib, "_ZN16CPedIntelligence7ProcessEv");
    const bool scanHooked = scan && aml->Hook(reinterpret_cast<void*>(scan), reinterpret_cast<void*>(HookScanForEvents),
                                              reinterpret_cast<void**>(&g_originalScanForEvents));
    const bool eventsHooked = events && aml->Hook(reinterpret_cast<void*>(events), reinterpret_cast<void*>(HookProcessEventHandler),
                                                  reinterpret_cast<void**>(&g_originalProcessEventHandler));
    const bool processHooked = process && aml->Hook(reinterpret_cast<void*>(process), reinterpret_cast<void*>(HookIntelligenceProcess),
                                                    reinterpret_cast<void**>(&g_originalIntelligenceProcess));
    WeaponFileLog("REMOTE_AI hooks: scanForEvents=%d eventHandler=%d process=%d (blockEvents=%d blockAmbientTasks=%d directVehicleWarp=1)",
                  scanHooked, eventsHooked, processHooked, kBlockRemotePedEvents, kBlockRemotePedTasks);
}




bool RunRemotePedTask(CPed* ped, const SCRIPT_COMMAND* command, const CVector& target, int durationMs)
{
    if (!ped || !g_saUtils || durationMs <= 0) return false;
    const int pedRef = CPools::GetPedRef(ped);
    if (pedRef <= 0 || CPools::GetPed(pedRef) != ped) return false;
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)) return false;
    const bool issued = g_saUtils->ScriptCommand(command, pedRef, target.x, target.y, target.z, durationMs);
    if (issued)
        g_remoteTaskPermitUntilMs[ped] = NowSteadyMs() + static_cast<uint64_t>(durationMs + 220);
    else
        g_remoteTaskPermitUntilMs.erase(ped);
    return issued;
}

void ApplyRemoteAimHeadingContext(CPed* ped, const AnimationProfile& profile,
                                  float bodyHeadingDegrees)
{
    if (!ped || ped->m_fHealth <= 0.0f) return;
    const bool firing = (profile.flags & ANIM_FIRING) != 0 ||
                        profile.weaponState == static_cast<uint8_t>(WEAPONSTATE_FIRING);
    const bool aiming = (profile.flags & ANIM_AIMING) != 0 || firing;
    if (!aiming) return;

    const float yaw = coop_aim::NormalizeYawRadians(profile.aimYaw);

    const unsigned slot = static_cast<unsigned>(ped->m_nSelectedWepSlot);
    CWeaponInfo* weaponInfo = nullptr;
    if (slot < 13 && g_getWeaponInfoForIcon) {
        const int weaponType = static_cast<int>(ped->m_aWeapons[slot].m_eWeaponType);
        weaponInfo = static_cast<CWeaponInfo*>(g_getWeaponInfoForIcon(weaponType, 1));
    }

    const bool firstPerson = (profile.flags & ANIM_AIM_FIRST_PERSON) != 0;
    const bool melee = weaponInfo && weaponInfo->m_nWeaponFire == WEAPON_FIRE_MELEE;
    const bool weaponNeedsBodyTurn = weaponInfo && !melee &&
        (!weaponInfo->m_nFlags.bAimWithArm || ped->bIsDucking) && !ped->m_pVehicle;

    if (firstPerson) {
        ped->m_fAimingRotation = yaw;
        ped->m_fCurrentRotation = yaw;
    } else if (weaponNeedsBodyTurn) {
        const float bodyAimYaw = coop_aim::NormalizeYawRadians(yaw - 0.05f);
        ped->m_fAimingRotation = bodyAimYaw;
        ped->m_fCurrentRotation = bodyAimYaw;
    } else {
        const float bodyYaw = coop_aim::NormalizeYawRadians(
            bodyHeadingDegrees * (coop_aim::kPi / 180.0f));
        ped->m_fAimingRotation = bodyYaw;
    }
}

void UpdateRemoteAimAndShooting(CPed* ped, const RemotePlayer& player, RemoteVisualState& visual)
{
    if (!ped || ped->m_fHealth <= 0.0f) {
        visual.latestAimProfileValid = false;
        return;
    }

    const AnimationProfile& profile = player.animation;
    const bool firing = (profile.flags & ANIM_FIRING) != 0 ||
                        profile.weaponState == static_cast<uint8_t>(WEAPONSTATE_FIRING);
    const bool aiming = (profile.flags & ANIM_AIMING) != 0 || firing;
    visual.latestAimProfileValid = false;
    if (!aiming) {
        visual.lastAimTaskMs = 0;
        return;
    }

    if ((player.vehicleId != 0 && !ped->m_pVehicle) ||
        (player.vehicleId == 0 && ped->m_pVehicle)) return;

    visual.latestAimProfile = profile;
    visual.latestAimProfileValid = true;

    if (!ped->m_pVehicle) {
        constexpr uint64_t kAimTaskRefreshMs = 500;
        constexpr int kAimTaskDurationMs = 1000;
        const uint64_t now = NowSteadyMs();
        if (now - visual.lastAimTaskMs >= kAimTaskRefreshMs) {
            const float yaw = coop_aim::NormalizeYawRadians(profile.aimYaw);
            const float pitch = coop_aim::ClampPitch(profile.aimPitch, coop_aim::kMaxPlayerPitch);
            const CVector& position = ped->GetPosition();
            const coop_aim::Point3 target = coop_aim::FallbackAimTarget(
                coop_aim::Point3{position.x, position.y, position.z}, yaw, pitch);
            if (RunRemotePedTask(ped, &kScmTaskAimGunAtCoord,
                                 CVector(target.x, target.y, target.z), kAimTaskDurationMs))
                visual.lastAimTaskMs = now;
        }
    }

    ApplyRemoteAimHeadingContext(ped, profile, HeadingDegrees(ped));
}

void ReapplyCachedRemoteAim(CPed* ped, RemoteVisualState& visual)
{
    if (!ped || !visual.latestAimProfileValid) return;
    ApplyRemoteAimHeadingContext(ped, visual.latestAimProfile, HeadingDegrees(ped));
}

void HookGenericPedProcess(void* self)
{
    if (g_originalGenericPedProcess) g_originalGenericPedProcess(self);
    CPed* ped = static_cast<CPed*>(self);
    if (!ped) return;

    for (const auto& entry : g_remotePeds) {
        if (entry.second != ped) continue;
        if (g_heldRemoteCutscenePartners.find(entry.first) != g_heldRemoteCutscenePartners.end())
            return;
        auto visual = g_remoteVisuals.find(entry.first);
        if (visual != g_remoteVisuals.end()) ReapplyCachedRemoteAim(ped, visual->second);
        return;
    }
}

void ReapplyRemoteAimsForVehicle(CVehicle* vehicle)
{
    if (!vehicle) return;
    for (const auto& entry : g_remotePeds) {
        CPed* ped = entry.second;
        if (!ped || ped->m_pVehicle != vehicle) continue;
        auto visual = g_remoteVisuals.find(entry.first);
        if (visual != g_remoteVisuals.end()) ReapplyCachedRemoteAim(ped, visual->second);
    }
}


bool HasVerifiedCPedProcessControlPrologue(uintptr_t functionAddress)
{
    if (!functionAddress) return false;
    static const uint8_t kExpected[] = {
        0xF0, 0xB5, 0x03, 0xAF, 0x2D, 0xE9, 0x00, 0x0F,
        0x81, 0xB0, 0x2D, 0xED, 0x04, 0x8B, 0x8E, 0xB0
    };
    const auto* code = reinterpret_cast<const uint8_t*>(functionAddress & ~uintptr_t(1));
    return std::memcmp(code, kExpected, sizeof(kExpected)) == 0;
}

constexpr int kRemoteShootingFrames = 5;
constexpr int kRemoteFireAnimFrames = 14;
constexpr int kRemoteGunFlashLifetime = 100;
constexpr float kRemoteFireBlendRate = 8.0f;

bool CaptureFireAssociation(CPed* ped, uint16_t& group, uint16_t& animId)
{
    group = 0;
    animId = 0;
    if (!ped || !ped->m_pRwClump || !g_getFirstAssociation || !g_getNextAssociation) return false;
    for (void* a = g_getFirstAssociation(ped->m_pRwClump); a; a = g_getNextAssociation(a)) {
        AnimationAssociationState state{};
        ReadAssociation(a, state);
        if (!(state.flags & 1) || state.blend <= 0.001f || state.group == 0) continue;
        if (state.animationId == kAnimationFireId || state.animationId == kAnimationCrouchFireId) {
            group = state.group;
            animId = static_cast<uint16_t>(state.animationId);
            return true;
        }
    }
    return false;
}

void FadeRemoteFireAnimation(RemoteVisualState& visual, CPed* ped)
{
    if (visual.fireAnimGroup != 0 && ped && ped->m_pRwClump && g_getFirstAssociation && g_getNextAssociation) {
        for (void* a = g_getFirstAssociation(ped->m_pRwClump); a; a = g_getNextAssociation(a)) {
            AnimationAssociationState state{};
            ReadAssociation(a, state);
            if (state.group == visual.fireAnimGroup && state.animationId == visual.fireAnimId)
                *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(a) + kAssocBlendDelta) = -kRemoteFireBlendRate;
        }
    }
    visual.fireAnimGroup = 0;
    visual.fireAnimId = 0;
    visual.fireAnimFrames = 0;
}

bool IsSupportedNpcModel(int modelId)
{

    return modelId == 7;
}


bool WeaponModelResident(int modelId)
{
    if (modelId <= 0) return true;
    if (modelId >= 30000) return false;
    CBaseModelInfo** infos = CModelInfo::ms_modelInfoPtrs;
    return infos && infos[modelId] && infos[modelId]->m_pRwObject != nullptr;
}


uint64_t g_weaponStreamRetryAtMs[256]{};
bool g_weaponNotReadyLogged[256]{};






bool WeaponModelsReadyForHand(const CWeaponInfo* info, uint8_t weaponType)
{
    const int ids[2] = {info->m_nModelId, info->m_nModelId2};
    if (WeaponModelResident(ids[0]) && WeaponModelResident(ids[1])) {
        g_weaponNotReadyLogged[weaponType] = false;
        return true;
    }
    const uint64_t now = SteadyNowMs();
    if (now < g_weaponStreamRetryAtMs[weaponType]) return false;
    for (int id : ids) if (id > 0 && id < 30000) CStreaming::RequestModel(id, 2);
    CStreaming::LoadAllRequestedModels(false);
    if (WeaponModelResident(ids[0]) && WeaponModelResident(ids[1])) {
        g_weaponStreamRetryAtMs[weaponType] = 0;
        g_weaponNotReadyLogged[weaponType] = false;
        return true;
    }
    g_weaponStreamRetryAtMs[weaponType] = now + 250;
    if (!g_weaponNotReadyLogged[weaponType]) {
        g_weaponNotReadyLogged[weaponType] = true;
        WeaponFileLog("WEAPON_MODEL_NOT_READY weapon=%u model=%d loaded=%u model2=%d loaded2=%u (retrying)",
                      static_cast<unsigned>(weaponType), ids[0], WeaponModelResident(ids[0]) ? 1u : 0u,
                      ids[1], WeaponModelResident(ids[1]) ? 1u : 0u);
    }
    return false;
}

bool ApplyRemoteWeaponSaMpStyle(CPed* ped, uint8_t weaponType)
{





    if (!ped || !g_saUtils || weaponType == 0 || weaponType >= 47) return false;
    const int pedRef = CPools::GetPedRef(ped);
    if (pedRef <= 0 || CPools::GetPed(pedRef) != ped) return false;

    g_scmWeaponModelOutput = 0xFFFFFFFFu;
    g_saUtils->ScriptCommand(&kScmGetWeaponTypeModel,
                             static_cast<int>(weaponType), &g_scmWeaponModelOutput);
    if (g_scmWeaponModelOutput == 0xFFFFFFFFu) {
        WeaponFileLog("WEAPON_SAMP_MODEL_QUERY_FAIL weapon=%u pedRef=%d",
                      static_cast<unsigned>(weaponType), pedRef);
        return false;
    }
    const int weaponModel = static_cast<int>(g_scmWeaponModelOutput);
    if (weaponModel > 0 && weaponModel < 30000 &&
        !g_saUtils->ScriptCommand(&kScmHasModelLoaded, weaponModel)) {
        g_saUtils->ScriptCommand(&kScmRequestModel, weaponModel);
        g_saUtils->ScriptCommand(&kScmLoadAllRequestedModels);
    }
    if (weaponModel > 0 && weaponModel < 30000 &&
        !g_saUtils->ScriptCommand(&kScmHasModelLoaded, weaponModel)) {
        WeaponFileLog("WEAPON_SAMP_MODEL_WAIT weapon=%u model=%d; will retry",
                      static_cast<unsigned>(weaponType), weaponModel);
        return false;
    }

    g_saUtils->ScriptCommand(&kScmGiveWeaponToChar, pedRef,
                             static_cast<int>(weaponType), 9999);
    g_saUtils->ScriptCommand(&kScmSetCurrentCharWeapon, pedRef,
                             static_cast<int>(weaponType));

    const unsigned selectedSlot = static_cast<unsigned>(ped->m_nSelectedWepSlot);
    const unsigned selectedWeapon = selectedSlot < 13
        ? static_cast<unsigned>(ped->m_aWeapons[selectedSlot].m_eWeaponType) : 0xFFu;
    if (selectedWeapon != weaponType) {
        WeaponFileLog("WEAPON_SAMP_VERIFY_FAIL requested=%u pedRef=%d slot=%u actual=%u",
                      static_cast<unsigned>(weaponType), pedRef, selectedSlot, selectedWeapon);
        return false;
    }
    WeaponFileLog("WEAPON_SAMP_APPLY weapon=%u model=%d pedRef=%d",
                  static_cast<unsigned>(weaponType), weaponModel, pedRef);
    return true;
}

bool ApplyRemoteWeapon(CPed* ped, uint8_t weaponType)
{
    if (!ped || (weaponType > 0x2E && weaponType != 0x3A)) return false;
    if (weaponType == 0) {



        ped->ClearWeapons();
        ped->m_nSelectedWepSlot = 0;
        if (g_setCurrentWeapon)
            g_setCurrentWeapon(reinterpret_cast<void*>(ped), WEAPONTYPE_UNARMED);
        if (g_saUtils) {
            const int pedRef = CPools::GetPedRef(ped);
            if (pedRef > 0) g_saUtils->ScriptCommand(&kScmSetCurrentCharWeapon, pedRef, 0);
        }
        WeaponFileLog("WEAPON_PED_APPLY id=unarmed");
        logger->Info("WEAPON_PED_APPLY id=unarmed");
        return true;
    }




    if (weaponType < 47 && ApplyRemoteWeaponSaMpStyle(ped, weaponType)) {
        logger->Info("Remote weapon %u applied through SA-MP-compatible SCM path",
                     static_cast<unsigned>(weaponType));
        return true;
    }



    auto* info = CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(weaponType), 0);
    if (!info || info->m_nSlot >= 13) return false;
    if (!g_giveWeapon || !g_setCurrentWeapon) return false;
    if (!WeaponModelsReadyForHand(info, weaponType)) return false;
    g_giveWeapon(reinterpret_cast<void*>(ped), static_cast<eWeaponType>(weaponType), 9999, true);
    g_setCurrentWeapon(reinterpret_cast<void*>(ped), static_cast<eWeaponType>(weaponType));
    const unsigned selectedSlot = static_cast<unsigned>(ped->m_nSelectedWepSlot);
    const unsigned selectedWeapon = selectedSlot < 13
        ? static_cast<unsigned>(ped->m_aWeapons[selectedSlot].m_eWeaponType) : 0xFFu;
    if (selectedWeapon != weaponType) {
        WeaponFileLog("WEAPON_NATIVE_VERIFY_FAIL requested=%u slot=%u actual=%u",
                      static_cast<unsigned>(weaponType), selectedSlot, selectedWeapon);
        logger->Error("Remote weapon %u did not become active (slot %u contains %u); will retry",
                      static_cast<unsigned>(weaponType), selectedSlot, selectedWeapon);
        return false;
    }
    WeaponFileLog("WEAPON_NATIVE_FALLBACK_APPLY weapon=%u", static_cast<unsigned>(weaponType));
    logger->Info("Remote weapon %u applied through native fallback", static_cast<unsigned>(weaponType));
    return true;
}

void ApplyRemoteVitals(CPed* ped, uint16_t health, uint16_t armour)
{
    if (!ped) return;
    ped->SetHealthSafe(static_cast<float>(health));
    ped->m_fArmour = static_cast<float>(armour);
}

bool ExecuteRemoteShot(const RemoteShot& shot, CPed* ped)
{
    if (!ped) return false;

    const bool projectile = shot.kind == 1 || shot.kind == 2;
    void* creator = static_cast<void*>(ped);
    int projectileModel = -1;
    if (projectile) {
        if (!g_projectileReplayFn) {
            WeaponFileLog("REMOTE_PROJECTILE_REPLAY_UNAVAILABLE id=%u (native replay entry unresolved)", shot.id);
            return true;
        }
        if (shot.kind == 2) {
            CVehicle* firingVehicle = nullptr;
            if (shot.vehicleId) {
                auto vehicleIt = g_remoteVehicles.find(shot.vehicleId);
                if (vehicleIt != g_remoteVehicles.end() && vehicleIt->second) firingVehicle = vehicleIt->second;
                else return false;
            } else {
                firingVehicle = ped->m_pVehicle;
                bool expectsVehicle = false;
                if (g_client) {
                    for (const auto& state : g_client->GetRemotePlayers()) {
                        if (state.id != shot.id) continue;
                        expectsVehicle = state.vehicleId != 0;
                        if (state.vehicleId) {
                            auto vehicleIt = g_remoteVehicles.find(state.vehicleId);
                            if (vehicleIt != g_remoteVehicles.end() && vehicleIt->second) firingVehicle = vehicleIt->second;
                            else if (expectsVehicle) firingVehicle = nullptr;
                        }
                        break;
                    }
                }
                if (!firingVehicle && expectsVehicle) return false;
            }
            creator = firingVehicle ? static_cast<void*>(firingVehicle) : nullptr;
            if (!creator) return false;
        }
        projectileModel = ProjectileModelForWeapon(shot.weapon);
        if (projectileModel < 0) {
            WeaponFileLog("REMOTE_PROJECTILE_UNSUPPORTED id=%u weapon=%u kind=%u", shot.id, shot.weapon, shot.kind);
            return true;
        }
        if (!CStreaming::IsInitialised()) return false;
        if (!g_saUtils) return false;
        if (!g_saUtils->ScriptCommand(&kScmHasModelLoaded, projectileModel)) {
            if (CStreaming::IsObjectInCdImage(projectileModel)) {
                CStreaming::RequestModel(projectileModel, 2);
                CStreaming::LoadAllRequestedModels(false);
            }
            g_saUtils->ScriptCommand(&kScmRequestModel, projectileModel);
            g_saUtils->ScriptCommand(&kScmLoadAllRequestedModels);
            if (!g_saUtils->ScriptCommand(&kScmHasModelLoaded, projectileModel)) {
                static std::unordered_set<int> loggedProjectileStreamWait;
                if (loggedProjectileStreamWait.insert(projectileModel).second)
                    WeaponFileLog("REMOTE_PROJECTILE_WAIT_STREAM id=%u weapon=%u model=%d (retrying)", shot.id, shot.weapon, projectileModel);
                return false;
            }
        }
    }

    RemoteVisualState& visual = g_remoteVisuals[shot.id];
    if (g_doGunFlash) g_doGunFlash(reinterpret_cast<void*>(ped), kRemoteGunFlashLifetime, false);
    if (shot.animGroup != 0 && shot.animId != 0 && g_blendAnimation && ped->m_pRwClump) {
        g_blendAnimation(ped->m_pRwClump, shot.animGroup, shot.animId, kRemoteFireBlendRate);
        visual.fireAnimGroup = shot.animGroup;
        visual.fireAnimId = static_cast<int16_t>(shot.animId);
        visual.fireAnimFrames = kRemoteFireAnimFrames;
    } else if (!projectile && g_setMoveState && g_setMoveAnim) {
        g_setMoveState(ped, static_cast<int>(kMoveStill));
        g_setMoveAnim(ped);
    }
    visual.shootingFrames = kRemoteShootingFrames;

    if (shot.kind == 0 || shot.kind == 3) {
        if (!g_weaponFire && !g_originalFireInstantHit) {
            WeaponFileLog("REMOTE_BULLET_REPLAY_UNAVAILABLE id=%u", shot.id); return true;
        }
        ShotVec3 from{shot.x,shot.y,shot.z}, to{shot.dx,shot.dy,shot.dz};
        if (shot.kind == 3) {
            const float len=std::sqrt(to.x*to.x+to.y*to.y+to.z*to.z);
            if (!std::isfinite(len)||len<0.001f) return true;
            to={from.x+to.x/len*60.0f,from.y+to.y/len*60.0f,from.z+to.z/len*60.0f};
        }
        float vx=to.x-from.x,vy=to.y-from.y,vz=to.z-from.z;
        const float rayLen=std::sqrt(vx*vx+vy*vy+vz*vz);
        if(!std::isfinite(rayLen)||rayLen<0.05f) return true;
        to={to.x+vx/rayLen*0.5f,to.y+vy/rayLen*0.5f,to.z+vz/rayLen*0.5f};
        alignas(8) uint8_t wb[64]{}; int32_t* wi=reinterpret_cast<int32_t*>(wb);
        wi[0]=shot.weapon; wi[1]=0; wi[2]=50; wi[3]=500;
        ShotVec3 muzzle=from; g_replayingNativeShot=true; g_replayShooter=ped;
        const int replayPedRef = CPools::GetPedRef(ped);
        if (g_saUtils && replayPedRef) g_saUtils->ScriptCommand(&kScmSetCharAccuracy, replayPedRef, 100);
        if (g_weaponFire) g_weaponFire(wb,ped,&from,&muzzle,nullptr,&to,nullptr);
        else g_originalFireInstantHit(wb,ped,&from,&muzzle,nullptr,&to,nullptr,false,true);
        if (g_saUtils && replayPedRef) g_saUtils->ScriptCommand(&kScmSetCharAccuracy, replayPedRef, 0);
        g_replayShooter=nullptr; g_replayingNativeShot=false;
        WeaponFileLog("REMOTE_BULLET_REPLAY id=%u weapon=%u kind=%u",shot.id,shot.weapon,shot.kind);
        return true;
    }
    if (projectile) {
        ShotVec3 pos{shot.x,shot.y,shot.z},dir{shot.dx,shot.dy,shot.dz};
        const float force=std::isfinite(shot.force)?std::max(0.0f,std::min(5.0f,shot.force)):0.0f;
        g_replayingNativeShot = true;
        const bool created = g_projectileReplayFn(creator, shot.weapon, pos, force, &dir, nullptr);
        g_replayingNativeShot = false;
        if (!created) {
            static std::unordered_set<uint16_t> loggedCreateFailure;
            if (loggedCreateFailure.insert(shot.id).second)
                WeaponFileLog("REMOTE_PROJECTILE_CREATE_FAILED id=%u weapon=%u kind=%u vehicle=%u model=%d force=%.2f", shot.id, shot.weapon, shot.kind, static_cast<unsigned>(shot.vehicleId), projectileModel, force);
            return false;
        }
        WeaponFileLog("REMOTE_PROJECTILE_REPLAY id=%u weapon=%u kind=%u vehicle=%u model=%d", shot.id, shot.weapon, shot.kind, static_cast<unsigned>(shot.vehicleId), projectileModel);
        return true;
    }
    WeaponFileLog("REMOTE_SHOT_IGNORED id=%u kind=%u",shot.id,shot.kind);
    return true;
}


void CaptureAndSendLocalBones(CPed* ped)
{
    if(!g_client || !ped) return;
    static NetworkBone bones[19];
    uint8_t count=0;
    for(uint8_t i=0;i<19;++i){
        auto* b=ped->m_apBones[i];
        if(!b || !b->pStdKeyFrame) continue;
        auto q=b->pStdKeyFrame->q;
        if(!std::isfinite(q.imag.x)||!std::isfinite(q.imag.y)||!std::isfinite(q.imag.z)||!std::isfinite(q.real)) continue;
        bones[count].index=i; bones[count].qx=q.imag.x; bones[count].qy=q.imag.y; bones[count].qz=q.imag.z; bones[count].qw=q.real; ++count;
    }
    g_client->SendBoneSnapshot(bones,count);
}

void ApplyRemoteBones(CPed* ped, const RemotePlayer& player)
{
    if(!ped || player.boneCount==0) return;
    for(uint8_t i=0;i<player.boneCount;++i){
        const auto& n=player.bones[i]; if(n.index>=19) continue;
        auto* b=ped->m_apBones[n.index]; if(!b || !b->pStdKeyFrame) continue;
        b->pStdKeyFrame->q.imag.x=n.qx; b->pStdKeyFrame->q.imag.y=n.qy; b->pStdKeyFrame->q.imag.z=n.qz; b->pStdKeyFrame->q.real=n.qw;
    }
    ped->UpdateRpHAnim();
}









struct RetiredPedRegistry {
    std::unordered_map<uint32_t, bool> refs;
    void Add(uint32_t ref) { if (ref) refs[ref] = false; }
    bool Empty() const { return refs.empty(); }


    bool Check(uint32_t ref, bool* firstSkip) {
        auto it = refs.find(ref);
        if (it == refs.end()) return false;
        *firstSkip = !it->second;
        it->second = true;
        return true;
    }

    void Prune(const std::unordered_map<uint32_t, bool>& present) {
        for (auto it = refs.begin(); it != refs.end();) {
            if (present.count(it->first)) ++it;
            else it = refs.erase(it);
        }
    }
};
RetiredPedRegistry g_retiredPeds;



void RetireRemotePed(CPed* ped)
{
    if (!ped) return;
    ReleaseRemoteClothesForPed(ped);
    g_retiredPeds.Add(static_cast<uint32_t>(CPools::GetPedRef(ped)));
    g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(ped));
}



static const SCRIPT_COMMAND kScmDeleteChar = { 0x009B, "i" };










void DiscardRemotePed(CPed* ped, const char* why)
{
    if (!ped || !g_saUtils) return;
    ReleaseRemoteClothesForPed(ped);
    const int ref = CPools::GetPedRef(ped);
    if (!ref) { RetireRemotePed(ped); return; }
    g_retiredPeds.Add(static_cast<uint32_t>(ref));
    g_saUtils->ScriptCommand(&kScmDeleteChar, ref);


    if (CPed* stillAlive = CPools::GetPed(ref)) {
        logger->Info("DiscardRemotePed(%s) ref=%d: DELETE_CHAR had no effect, falling back to MarkEntityAsNotNeeded", why, ref);
        g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(stillAlive));
    } else {
        logger->Info("DiscardRemotePed(%s) ref=%d: seated ghost ped deleted", why, ref);
    }
}


void SyncLocalMissionMarkers()
{
    if (!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;




    static std::array<bool, 175> lastSeen{};
    std::array<bool, 175> seen{};

    for (uint16_t i = 0; i < 175; ++i) {
        const auto& trace = CRadar::ms_RadarTrace[i];
        if (!trace.m_bInUse ||
            (trace.m_nBlipDisplay != BLIP_DISPLAY_MARKER_ONLY &&
             trace.m_nBlipDisplay != BLIP_DISPLAY_BOTH))
            continue;

        switch (trace.m_nBlipType) {
            case BLIP_CHAR:
            case BLIP_OBJECT:
            case BLIP_COORD:
            case BLIP_CONTACTPOINT:
                break;
            default:
                continue;
        }

        if (!std::isfinite(trace.m_vecPos.x) ||
            !std::isfinite(trace.m_vecPos.y) ||
            !std::isfinite(trace.m_vecPos.z))
            continue;

        seen[i] = true;
        g_client->SendMissionMarker(
            i,
            static_cast<uint8_t>(trace.m_nBlipType),
            static_cast<uint8_t>(trace.m_nBlipDisplay),
            static_cast<uint8_t>(trace.m_nRadarSprite),
            trace.m_nBlipSize,
            trace.m_nColour,
            trace.m_vecPos.x, trace.m_vecPos.y, trace.m_vecPos.z,
            trace.m_fSphereRadius);
    }

    for (uint16_t i = 0; i < 175; ++i) {
        if (lastSeen[i] && !seen[i])
            g_client->SendMissionMarkerRemove(i);
    }
    lastSeen = seen;
}

void UpdateRemoteMissionMarkers()
{
    if (!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;

    for (const auto id : g_client->DrainRemovedMissionMarkers()) {
        auto it = g_remoteMissionMarkerBlips.find(id);
        if (it != g_remoteMissionMarkerBlips.end()) {
            if (it->second >= 0)
                CRadar::ClearBlip(it->second);
            g_remoteMissionMarkerBlips.erase(it);
        }
    }

    for (const auto& marker : g_client->GetRemoteMissionMarkers()) {
        if (marker.ownerId == g_client->LocalId()) continue;

        auto it = g_remoteMissionMarkerBlips.find(marker.id);
        int handle = (it == g_remoteMissionMarkerBlips.end()) ? -1 : it->second;

        if (handle < 0 || !CRadar::ValidBlipHandle(static_cast<uint32_t>(handle))) {
            const CVector pos(marker.x, marker.y, marker.z);
            handle = CRadar::SetCoordBlip(
                BLIP_COORD,
                pos,
                marker.color,
                BLIP_DISPLAY_MARKER_ONLY,
                nullptr);
            if (handle < 0) continue;
            g_remoteMissionMarkerBlips[marker.id] = handle;
            it = g_remoteMissionMarkerBlips.find(marker.id);
        }

        auto& trace = CRadar::GetBlipFromHandle(static_cast<uint32_t>(handle));
        trace.m_vecPos = CVector(marker.x, marker.y, marker.z);
        trace.m_nColour = marker.color;
        trace.m_bInUse = 1;
        trace.m_bBlipRemain = 1;
        trace.m_nBlipDisplay = BLIP_DISPLAY_MARKER_ONLY;
        trace.m_nBlipType = BLIP_COORD;
        trace.m_nRadarSprite = marker.sprite;
        trace.m_nBlipSize = marker.scale;
        trace.m_fSphereRadius = marker.radius;
    }
}

void TrackLocalVehicleEntity(CVehicle* vehicle);
uint16_t FindRemoteVehicleNetworkId(CVehicle* vehicle);

void SendPendingLocalCarjackEvent()
{
    auto& pending = g_pendingLocalCarjackEvent;
    if (!pending.vehicle) return;
    const uint64_t now = NowSteadyMs();
    if (now >= pending.expiresAtMs || pending.vehiclePoolRef <= 0 ||
        CPools::GetVehicle(pending.vehiclePoolRef) != pending.vehicle) {
        pending = PendingLocalCarjackEvent{};
        return;
    }

    uint16_t networkId = g_client ? g_client->LocalVehicleNetworkIdForKey(pending.vehicleKey) : 0;
    if (!networkId) networkId = FindRemoteVehicleNetworkId(pending.vehicle);
    if (!networkId) return;

    if (g_client) {
        g_client->SendVehicleEnter(networkId, 0);
        g_lastLocalCarjackVehicleKey = pending.vehicleKey;
        g_lastLocalCarjackSendMs = now;
        logger->Info("LOCAL_CARJACK_EVENT sent vehicle=%u key=%u driverRef=%d", networkId,
                     pending.vehicleKey, pending.vehicle->m_pDriver ? CPools::GetPedRef(pending.vehicle->m_pDriver) : 0);
    }
    pending = PendingLocalCarjackEvent{};
}

bool StartRemoteDriverEntryTask(CPed* entrant, CVehicle* vehicle)
{
    if (!entrant || !vehicle || !entrant->m_pIntelligence) return false;
    if (entrant->m_pVehicle || entrant->bInVehicle) return false;

    const int pedRef = CPools::GetPedRef(entrant);
    const int vehicleRef = CPools::GetVehicleRef(vehicle);
    if (pedRef <= 0 || vehicleRef <= 0 ||
        CPools::GetPed(pedRef) != entrant || CPools::GetVehicle(vehicleRef) != vehicle)
        return false;

    const auto ctor = g_originalEnterCarAsDriverCtor
        ? g_originalEnterCarAsDriverCtor : g_resolvedEnterCarAsDriverCtor;
    if (!ctor || !g_taskAllocate || !g_taskManagerSetTask) return false;

    auto* task = static_cast<CTask*>(g_taskAllocate(kRemoteEnterCarDriverTaskSize));
    if (!task) return false;

    ctor(task, vehicle);
    g_taskManagerSetTask(&entrant->m_pIntelligence->m_TaskMgr, task, TASK_PRIMARY_PRIMARY, false);
    return true;
}

void QueueLocalCarjackEvent(CVehicle* vehicle, CPlayerPed* localPlayer)
{
    if (!g_client || !vehicle || !localPlayer || localPlayer->m_pVehicle || localPlayer->bInVehicle ||
        !vehicle->m_pDriver || vehicle->m_pDriver == localPlayer) return;

    CPed* victim = vehicle->m_pDriver;
    const uint64_t now = NowSteadyMs();
    g_remoteCarjackVictimUntilMs[victim] = now + kRemoteCarjackVictimGraceMs;
    if (IsRemotePlayerPed(victim))
        g_remoteTaskPermitUntilMs[victim] = now + kRemoteCarjackTaskDurationMs;
    for (const auto& npc : g_remoteNpcs) {
        if (npc.second == victim) {
            g_remoteCarjackNpcRemovalUntilMs[npc.first] = now + kRemoteCarjackVictimGraceMs;
            break;
        }
    }

    TrackLocalVehicleEntity(vehicle);
    const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vehicle));
    const int vehicleRef = CPools::GetVehicleRef(vehicle);
    if (!key || vehicleRef <= 0 || CPools::GetVehicle(vehicleRef) != vehicle) return;
    if (key == g_lastLocalCarjackVehicleKey && now - g_lastLocalCarjackSendMs < 1200) {
        SendPendingLocalCarjackEvent();
        return;
    }
    if (g_pendingLocalCarjackEvent.vehicle != vehicle) {
        g_pendingLocalCarjackEvent.vehicle = vehicle;
        g_pendingLocalCarjackEvent.vehiclePoolRef = vehicleRef;
        g_pendingLocalCarjackEvent.vehicleKey = key;
        g_pendingLocalCarjackEvent.expiresAtMs = now + kRemoteCarjackEventLifetimeMs;
    }
    SendPendingLocalCarjackEvent();
}

void DetectLocalCarjackIntent(CPlayerPed* localPlayer)
{
    static CVehicle* lastTaskVehicle = nullptr;
    if (!g_client || !localPlayer || !localPlayer->m_pIntelligence ||
        localPlayer->m_pVehicle || localPlayer->bInVehicle) {
        lastTaskVehicle = nullptr;
        return;
    }
    CTask* task = localPlayer->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(TASK_COMPLEX_ENTER_CAR_AS_DRIVER);
    if (!task) {
        lastTaskVehicle = nullptr;
        return;
    }
    CVehicle* target = *reinterpret_cast<CVehicle**>(reinterpret_cast<uintptr_t>(task) + 0x0C);
    if (!target || !target->m_pDriver) return;
    if (target != lastTaskVehicle) {
        lastTaskVehicle = target;
        QueueLocalCarjackEvent(target, localPlayer);
    }
    SendPendingLocalCarjackEvent();
}

void HookEnterCarAsDriverConstructor(void* task, CVehicle* vehicle)
{
    if (g_originalEnterCarAsDriverCtor) g_originalEnterCarAsDriverCtor(task, vehicle);
}

void SyncLocalNpcs()
{
    if(!g_client || !g_saUtils || !g_saUtils->IsGameInitialised() || !CPools::ms_pPedPool) return;
    const uint64_t syncNowMs=SteadyNowMs();
    if(g_lastNpcSyncMs && syncNowMs-g_lastNpcSyncMs<50) return;
    g_lastNpcSyncMs=syncNowMs;
    static uint64_t lastNpcAnimationSendMs=0;
    const bool sendNpcAnimations=!lastNpcAnimationSendMs || syncNowMs-lastNpcAnimationSendMs>=66;
    if(sendNpcAnimations) lastNpcAnimationSendMs=syncNowMs;
    CPlayerPed* local=FindPlayerPed(-1); if(!local) return;
    const uint32_t localRef = static_cast<uint32_t>(CPools::GetPedRef(local));
    const CVector lp=local->GetPosition();
    std::unordered_map<uint32_t,bool> seen;
    struct LocalNpcState { CPed* ped=nullptr; uint32_t ref=0, vehicleKey=0; uint16_t model=0, health=100, armour=0, vehicleNetworkId=0; float x=0,y=0,z=0,heading=0,vx=0,vy=0,vz=0,currentRotation=0,aimingRotation=0,lookingDirection=0,aimX=0,aimY=0,aimZ=0; bool aiming=false,firing=false; uint8_t weapon=0,move=1,seat=0,fightingStyle=4; char specialName[16]{}; };
    std::vector<LocalNpcState> states;
    std::unordered_map<uint32_t,CVehicle*> occupiedVehicles;
    auto* pool=CPools::ms_pPedPool;
    const int max=std::min(pool->m_nSize, 256);
    const bool trackRetired=!g_retiredPeds.Empty();
    struct LocalNpcShotCapture { int lastClip=-1; uint8_t lastWeapon=0xFF; uint64_t lastShotMs=0; };
    static std::unordered_map<uint32_t,LocalNpcShotCapture> npcShotCapture;
    std::unordered_map<uint32_t,bool> presentPedRefs;
    for(int i=0;i<max;++i){
        CPed* ped=pool->GetAt(i); if(!ped) continue;
        if(trackRetired){
            const uint32_t poolRef=(uint32_t)CPools::GetPedRef(ped);
            presentPedRefs[poolRef]=true;
            bool firstSkip=false;
            if(g_retiredPeds.Check(poolRef,&firstSkip)){
                if(firstSkip) logger->Info("RETIRED_PED_NOT_SYNCED_AS_NPC ref=%u (abandoned remote-player ped still in the ped pool)", poolRef);
                continue;
            }
        }
        if(ped==local) continue;
        bool remote=false; for(auto& kv:g_remotePeds) if(kv.second==ped){remote=true;break;} if(remote) continue;
        for(auto& kv:g_remoteNpcs) if(kv.second==ped){remote=true;break;} if(remote) continue;
        bool test=false; for(auto* n:g_spawnedNpcs) if(n==ped){test=true;break;} if(test) continue;
        const CVector pos=ped->GetPosition(); const float dx=pos.x-lp.x,dy=pos.y-lp.y,dz=pos.z-lp.z; if(dx*dx+dy*dy+dz*dz>80.0f*80.0f) continue;
        const uint32_t ref=(uint32_t)CPools::GetPedRef(ped); if(!ref) continue; seen[ref]=true;
        uint8_t weapon=0; unsigned slot=(unsigned)ped->m_nSelectedWepSlot; if(slot<13) weapon=(uint8_t)ped->m_aWeapons[slot].m_eWeaponType;
        LocalNpcState state{}; state.ped=ped; state.ref=ref; state.model=(uint16_t)ped->m_nModelIndex; const std::string specialName=StableSpecialNameForPed(ped, state.model); std::snprintf(state.specialName,sizeof(state.specialName),"%s",specialName.c_str()); state.x=pos.x; state.y=pos.y; state.z=pos.z; state.heading=HeadingDegrees(ped); state.health=(uint16_t)std::max(0.f,std::min(100.f,ped->GetHealth())); state.armour=(uint16_t)std::max(0.f,std::min(100.f,ped->m_fArmour)); state.weapon=weapon; state.move=(uint8_t)std::max(1,std::min(7,(int)ped->m_nMoveState));
        const CVector& pedVelocity=reinterpret_cast<CPhysical*>(ped)->m_vecMoveSpeed;
        state.vx=pedVelocity.x; state.vy=pedVelocity.y; state.vz=pedVelocity.z;
        state.currentRotation=ped->m_fCurrentRotation; state.aimingRotation=ped->m_fAimingRotation;
        state.lookingDirection=ped->m_fLookingDirection; state.fightingStyle=static_cast<uint8_t>(ped->m_nFightingStyle);
        auto& shotCapture=npcShotCapture[ref];
        int currentClip=-1;
        if(slot<13) currentClip=static_cast<int>(ped->m_aWeapons[slot].m_nAmmoInClip);
        if(shotCapture.lastWeapon!=weapon){shotCapture.lastWeapon=weapon;shotCapture.lastClip=currentClip;}
        else {
            if(currentClip>=0 && shotCapture.lastClip>=0 && currentClip<shotCapture.lastClip) shotCapture.lastShotMs=syncNowMs;
            if(currentClip>=0) shotCapture.lastClip=currentClip;
        }
        const bool recentShot=shotCapture.lastShotMs && syncNowMs>=shotCapture.lastShotMs && syncNowMs-shotCapture.lastShotMs<=600;
        state.firing=ped->bFiringWeapon || recentShot;
        state.aiming=ped->bIsAimingGun || state.firing;
        if(state.aiming){
            const float aimYaw=coop_aim::NormalizeYawRadians(ped->m_fAimingRotation);
            const coop_aim::Point3 aim=coop_aim::FallbackAimTarget(
                coop_aim::Point3{pos.x,pos.y,pos.z},aimYaw,0.0f);
            state.aimX=aim.x; state.aimY=aim.y; state.aimZ=aim.z;
        }
        if(CVehicle* vehicle=ped->m_pVehicle){
            int actualSeat=-1;
            if(vehicle->m_pDriver==ped) actualSeat=0;
            else for(uint8_t si=0;si<8;++si) if(vehicle->m_apPassengers[si]==ped){actualSeat=static_cast<int>(si)+1;break;}
            if(actualSeat>=0){
                TrackLocalVehicleEntity(vehicle);
                uint16_t remoteVehicleId=0; for(const auto& remote:g_remoteVehicles) if(remote.second==vehicle){remoteVehicleId=remote.first;break;}
                if(remoteVehicleId) state.vehicleNetworkId=remoteVehicleId;
                else {state.vehicleKey=(uint32_t)reinterpret_cast<uintptr_t>(vehicle);occupiedVehicles[state.vehicleKey]=vehicle;}
                state.seat=static_cast<uint8_t>(actualSeat);
            }
        }
        states.push_back(state);
    }
    if(trackRetired && pool->m_nSize<=256) g_retiredPeds.Prune(presentPedRefs);
    for(auto it=npcShotCapture.begin();it!=npcShotCapture.end();) {
        if(!seen.count(it->first)) it=npcShotCapture.erase(it); else ++it;
    }



    for (auto it = g_localSpecialIdentities.begin(); it != g_localSpecialIdentities.end();) {
        if (it->first != localRef && !seen.count(it->first)) it = g_localSpecialIdentities.erase(it);
        else ++it;
    }

    std::unordered_map<uint32_t,bool> currentVehicleKeys;
    for(const auto& entry:occupiedVehicles){
        CVehicle* vehicle=entry.second; if(!vehicle) continue;
        currentVehicleKeys[entry.first]=true;
        const CVector& pos=vehicle->GetPosition(); const CVector& speed=reinterpret_cast<CPhysical*>(vehicle)->m_vecMoveSpeed;
        RemoteVehicle state{}; state.modelId=static_cast<uint16_t>(vehicle->m_nModelIndex); state.x=pos.x; state.y=pos.y; state.z=pos.z; state.heading=HeadingDegrees(vehicle); state.vx=speed.x; state.vy=speed.y; state.vz=speed.z; state.health=vehicle->m_fHealth;
        const CVector& turnSpeed=reinterpret_cast<CPhysical*>(vehicle)->m_vecTurnSpeed; state.turnVx=turnSpeed.x; state.turnVy=turnSpeed.y; state.turnVz=turnSpeed.z;
        state.steering=vehicle->m_fSteerAngle; state.gasPedal=vehicle->m_fGasPedal; state.brakePedal=vehicle->m_fBreakPedal;
        if(vehicle->IsAutomobile()) {
            state.miscComponentAngle=static_cast<uint16_t>(std::max(0,static_cast<int>(vehicle->AsAutomobile()->m_wMiscComponentAngle)));
            state.turretVertical=vehicle->AsAutomobile()->m_fDoomVerticalRotation;
            state.turretHorizontal=vehicle->AsAutomobile()->m_fDoomHorizontalRotation;
        }
        state.extended=CaptureVehicleExtendedState(vehicle);
        state.primaryColor=vehicle->m_nPrimaryColor; state.secondaryColor=vehicle->m_nSecondaryColor; state.engineOn=vehicle->bEngineOn?1:0; state.lightsOn=vehicle->bLightsOn?1:0; state.sirenOn=vehicle->bSirenOrAlarm?1:0; state.doorLock=static_cast<uint8_t>(vehicle->m_eDoorLock); state.dirt=std::max(0.0f,std::min(15.0f,vehicle->m_fDirtLevel));
        int npcVehiclePaintjob=-1; const int npcVehicleRef=CPools::GetVehicleRef(vehicle);
        if(npcVehicleRef>0 && CPools::GetVehicle(npcVehicleRef)==vehicle)
            g_saUtils->ScriptCommand(&kScmGetCurrentVehiclePaintjob,npcVehicleRef,&npcVehiclePaintjob);
        state.appearance.paintjob=static_cast<int8_t>(npcVehiclePaintjob);
        for(size_t modSlot=0;modSlot<state.appearance.upgrades.size();++modSlot)
            state.appearance.upgrades[modSlot]=static_cast<int16_t>(vehicle->m_anUpgrades[modSlot]);
        state.appearance.valid=true;
        if(vehicle->GetMatrix()){const CMatrix* m=vehicle->GetMatrix();state.rightX=m->right.x;state.rightY=m->right.y;state.rightZ=m->right.z;state.upX=m->up.x;state.upY=m->up.y;state.upZ=m->up.z;state.atX=m->at.x;state.atY=m->at.y;state.atZ=m->at.z;state.orientationValid=true;}
        if(vehicle->IsAutomobile()){auto& d=vehicle->AsAutomobile()->m_damageManager;for(int i=0;i<4;++i)state.tyreStatus|=static_cast<uint8_t>((d.m_anWheelsStatus[i]&3u)<<(i*2));for(int i=0;i<6;++i)state.doorStatus|=static_cast<uint32_t>(d.m_anDoorsStatus[i])<<(i*4);state.lightStatus=d.m_nLightsStatus;state.panelStatus=d.m_nPanelsStatus;}
        g_client->SendNpcVehicleState(entry.first,state);
    }
    for(const auto& state:states) {
        g_client->SendNpcState(state.ref,state.model,state.x,state.y,state.z,state.heading,state.health,state.armour,state.weapon,state.move,state.vehicleKey,state.seat,state.specialName,state.vehicleNetworkId);
        g_client->SendNpcMotion(state.ref,state.vx,state.vy,state.vz,state.currentRotation,state.aimingRotation,state.lookingDirection,state.move,state.fightingStyle);
        g_client->SendNpcAim(state.ref,state.aiming,state.firing,state.aimX,state.aimY,state.aimZ);
        if(sendNpcAnimations && state.ped) {
            std::array<AnimationAssociationState,64> associations{};
            const uint8_t count=static_cast<uint8_t>(std::min<unsigned>(32,CaptureActiveAssociations(state.ped,associations.data())));
            g_client->SendNpcAnimationAssociations(state.ref,associations.data(),count);
        }
    }
    static std::unordered_map<uint32_t,uint64_t> lastRefs;
    for(auto& kv:lastRefs) if(!seen[kv.first]) g_client->SendNpcRemove(kv.first);
    lastRefs.clear(); for(auto& kv:seen) lastRefs[kv.first]=1;
    static std::unordered_map<uint32_t,bool> lastVehicleKeys;
    for(const auto& old:lastVehicleKeys) if(!currentVehicleKeys.count(old.first)) g_client->SendNpcVehicleRemove(old.first);
    lastVehicleKeys=std::move(currentVehicleKeys);
}





bool SeatIndexFor(CVehicle* vehicle, uint8_t wireSeat, int& outIndex)
{
    if (wireSeat == 0) { outIndex = -1; return true; }
    const int index = static_cast<int>(wireSeat) - 1;
    const int maxPassengers = std::min<int>(static_cast<int>(vehicle->m_nMaxPassengers), 8);
    if (index >= maxPassengers) return false;
    outIndex = index;
    return true;
}

bool RemotePedOccupiesSeat(CPed* ped, CVehicle* vehicle, int seatIndex)
{
    if (!ped || !vehicle || ped->m_pVehicle != vehicle) return false;
    if (seatIndex < 0) return vehicle->m_pDriver == ped;
    return seatIndex < 8 && vehicle->m_apPassengers[seatIndex] == ped;
}

bool WarpRemotePedIntoVehicle(CPed* ped, CVehicle* vehicle, int seatIndex)
{
    if (!g_saUtils || !ped || !vehicle || seatIndex < -1 || seatIndex > 7) return false;
    if (RemotePedOccupiesSeat(ped, vehicle, seatIndex)) return true;

    const int pedRef = CPools::GetPedRef(ped);
    const int vehicleRef = CPools::GetVehicleRef(vehicle);
    if (pedRef <= 0 || vehicleRef <= 0 ||
        CPools::GetPed(pedRef) != ped || CPools::GetVehicle(vehicleRef) != vehicle) {
        WeaponFileLog("REMOTE_VEHICLE_WARP_INVALID ped=%p pedRef=%d vehicle=%p vehicleRef=%d seat=%d",
                      static_cast<void*>(ped), pedRef, static_cast<void*>(vehicle), vehicleRef, seatIndex);
        return false;
    }

    CVehicle* oldVehicle = ped->m_pVehicle;
    if (oldVehicle && !RemotePedOccupiesSeat(ped, vehicle, seatIndex)) {
        const int oldVehicleRef = CPools::GetVehicleRef(oldVehicle);
        const bool oldVehicleValid = oldVehicleRef > 0 && CPools::GetVehicle(oldVehicleRef) == oldVehicle;
        g_saUtils->ScriptCommand(&kScmRemoveCharFromCarMaintainPosition, pedRef);
        if (oldVehicleValid) {
            if (oldVehicle->m_pDriver == ped) oldVehicle->m_pDriver = nullptr;
            for (unsigned i = 0; i < 8; ++i)
                if (oldVehicle->m_apPassengers[i] == ped) oldVehicle->m_apPassengers[i] = nullptr;
        }
        if (ped->m_pVehicle == oldVehicle) ped->m_pVehicle = nullptr;
        ped->bInVehicle = 0;
    }

    if (seatIndex < 0)
        g_saUtils->ScriptCommand(&kScmWarpCharIntoCar, pedRef, vehicleRef);
    else
        g_saUtils->ScriptCommand(&kScmWarpCharIntoCarAsPassenger, pedRef, vehicleRef, seatIndex);

    const bool seated = RemotePedOccupiesSeat(ped, vehicle, seatIndex);
    if (!seated) {
        WeaponFileLog("REMOTE_VEHICLE_WARP_PENDING pedRef=%d vehicleRef=%d seat=%d pedVehicle=%p driver=%p",
                      pedRef, vehicleRef, seatIndex, static_cast<void*>(ped->m_pVehicle),
                      static_cast<void*>(vehicle->m_pDriver));
    }
    return seated;
}

void UpdateRemoteNpcAim(CPed* ped, const RemoteNpc& npc, RemoteNpcVisualState& visual, bool seated)
{
    if(!ped || !g_saUtils) return;
    const bool aiming=npc.aiming || npc.firing;
    const uint64_t now=SteadyNowMs();
    if(!aiming || npc.weapon==0){
        if(visual.driveByTaskActive){
            const int ref=CPools::GetPedRef(ped);
            if(ref>0 && CPools::GetPed(ref)==ped) g_saUtils->ScriptCommand(&kScmClearCharTasks,ref);
            g_remoteTaskPermitUntilMs.erase(ped);
            visual.driveByTaskActive=false;
        }
        visual.lastAimTaskMs=0;
        visual.lastAimFiring=false;
        return;
    }
    const CVector target(npc.aimX,npc.aimY,npc.aimZ);
    if(seated){
        const float dx=npc.aimX-visual.lastAimX,dy=npc.aimY-visual.lastAimY,dz=npc.aimZ-visual.lastAimZ;
        const bool targetMoved=dx*dx+dy*dy+dz*dz>2.25f;
        const bool refresh=!visual.driveByTaskActive || targetMoved ||
                           visual.lastAimFiring!=npc.firing || now-visual.lastAimTaskMs>=1500;
        if(!refresh) return;
        const int ref=CPools::GetPedRef(ped);
        if(ref<=0 || CPools::GetPed(ref)!=ped) return;
        if(visual.driveByTaskActive) g_saUtils->ScriptCommand(&kScmClearCharTasks,ref);
        const int rate=npc.firing?90:0;
        const int issued=g_saUtils->ScriptCommand(&kScmTaskDriveBy,ref,-1,-1,
            npc.aimX,npc.aimY,npc.aimZ,1000.0f,8,0,rate);
        if(issued){
            visual.lastAimTaskMs=now; visual.lastAimX=npc.aimX; visual.lastAimY=npc.aimY; visual.lastAimZ=npc.aimZ;
            visual.lastAimFiring=npc.firing;
            visual.driveByTaskActive=true; g_remoteTaskPermitUntilMs[ped]=now+1720;
        }
        return;
    }
    if(visual.driveByTaskActive){
        const int ref=CPools::GetPedRef(ped);
        if(ref>0 && CPools::GetPed(ref)==ped) g_saUtils->ScriptCommand(&kScmClearCharTasks,ref);
        g_remoteTaskPermitUntilMs.erase(ped);
        visual.driveByTaskActive=false;
        visual.lastAimTaskMs=0;
    }
    constexpr uint64_t kNpcAimRefreshMs=450;
    constexpr int kNpcAimTaskDurationMs=1000;
    if(now-visual.lastAimTaskMs>=kNpcAimRefreshMs){
        const SCRIPT_COMMAND* command=npc.firing?&kScmTaskShootAtCoord:&kScmTaskAimGunAtCoord;
        if(RunRemotePedTask(ped,command,target,kNpcAimTaskDurationMs)) visual.lastAimTaskMs=now;
    }
}

void UpdateRemoteNpcs()
{
    if(!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;
    const uint64_t npcNowMs = SteadyNowMs();
    auto finalizeRemoteNpcRemoval = [&](uint32_t id) {
        auto it=g_remoteNpcs.find(id);
        if(it!=g_remoteNpcs.end()){
            if(it->second){
                g_remoteCarjackVictimUntilMs.erase(it->second);
                g_remoteTaskPermitUntilMs.erase(it->second);
                g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(it->second));
            }
            g_remoteNpcs.erase(it);
        }
        g_remoteNpcVisuals.erase(id);
        g_remoteNpcModelWait.erase(id);
        g_remoteCarjackNpcRemovalUntilMs.erase(id);
        ReleaseRemoteSpecialLease(RemoteNpcSpecialLeaseKey(id));
    };
    for(auto id:g_client->DrainRemovedNpcs()){
        auto protection=g_remoteCarjackNpcRemovalUntilMs.find(id);
        if(protection!=g_remoteCarjackNpcRemovalUntilMs.end() && npcNowMs<protection->second){
            g_deferredRemovedCarjackNpcs.insert(id);
            continue;
        }
        finalizeRemoteNpcRemoval(id);
    }
    for(auto it=g_deferredRemovedCarjackNpcs.begin();it!=g_deferredRemovedCarjackNpcs.end();){
        auto protection=g_remoteCarjackNpcRemovalUntilMs.find(*it);
        if(protection!=g_remoteCarjackNpcRemovalUntilMs.end() && npcNowMs<protection->second){
            ++it;
            continue;
        }
        const uint32_t id=*it;
        it=g_deferredRemovedCarjackNpcs.erase(it);
        finalizeRemoteNpcRemoval(id);
    }
    for(auto it=g_remoteCarjackNpcRemovalUntilMs.begin();it!=g_remoteCarjackNpcRemovalUntilMs.end();){
        if(npcNowMs>=it->second && !g_deferredRemovedCarjackNpcs.count(it->first))
            it=g_remoteCarjackNpcRemovalUntilMs.erase(it);
        else ++it;
    }
    for(const auto& n:g_client->GetRemoteNpcs()){
        if(n.ownerId==g_client->LocalId()) continue;
        const uint64_t specialLeaseKey=RemoteNpcSpecialLeaseKey(n.id);
        char normalizedSpecialName[coop_special::kNameCapacity]{};
        const bool hasSpecialName=coop_special::NormalizeName(n.specialModelName,normalizedSpecialName,sizeof(normalizedSpecialName));
        std::string targetSpecialName=hasSpecialName?normalizedSpecialName:"";
        bool specialModel=!targetSpecialName.empty();
        uint16_t remoteModelId=n.modelId;
        int specialSlot=0;
        auto existingPed=g_remoteNpcs.find(n.id);


        if(specialModel){
            specialSlot=EnsureRemoteSpecialSlot(specialLeaseKey,targetSpecialName);
            if(!specialSlot){if(RemoteNpcLogOnce(n.id,1,0))WeaponFileLog("REMOTE_NPC_SPECIAL_SLOT_UNAVAILABLE id=%u model=%u name=%s",n.id,n.modelId,targetSpecialName.c_str());continue;}
            if(!RemoteSpecialCharacterLoadedForName(specialSlot, targetSpecialName)){
                RequestRemoteSpecialCharacter(specialSlot,targetSpecialName);
                CStreaming::LoadAllRequestedModels(false);
            }
            remoteModelId=coop_special::ModelIndexFromSlot(specialSlot);
            if(!RemoteSpecialCharacterLoadedForName(specialSlot, targetSpecialName)){
                if(RemoteNpcNoteModelFailure(n.id,kSpecialLoadWaitKey))
                    if(RemoteNpcLogOnce(n.id,8,specialSlot))WeaponFileLog("REMOTE_NPC_SPECIAL_LOADING id=%u slot=%d name=%s loaded=0 (retrying)",n.id,specialSlot,targetSpecialName.c_str());
                continue;
            }
        }else if(remoteModelId>=coop_special::kFirstModelIndex&&remoteModelId<=coop_special::kLastModelIndex){



            if(!g_remoteNpcs.count(n.id) && RemoteNpcNoteModelFailure(n.id,kUnnamedSpecialWaitKey)) continue;
            remoteModelId=0;
        }
        if(!specialModel&&!IsSafeNetworkPedModel(remoteModelId)){
            if(RemoteNpcLogOnce(n.id,2,remoteModelId)){
                const bool infoOk=remoteModelId<kModelInfoCapacity&&CStreaming::IsInitialised()&&HasPedModelInfo(remoteModelId);
                WeaponFileLog("REMOTE_NPC_MODEL_UNSAFE id=%u model=%u model_info_ok=%u in_cd_image=%u (NPC skipped)",n.id,remoteModelId,infoOk?1u:0u,(infoOk&&CStreaming::IsObjectInCdImage(static_cast<int>(remoteModelId)))?1u:0u);
            }
            continue;
        }
        CVehicle* seatedVehicle=nullptr;
        if(n.vehicleId && n.health > 0){auto vehicleIt=g_remoteVehicles.find(n.vehicleId);if(vehicleIt==g_remoteVehicles.end()||!vehicleIt->second){if(RemoteNpcLogOnce(n.id,3,n.vehicleId))WeaponFileLog("REMOTE_NPC_WAITING_FOR_VEHICLE id=%u vehicle=%u model=%u",n.id,static_cast<unsigned>(n.vehicleId),n.modelId);continue;}seatedVehicle=vehicleIt->second;}
        auto it=g_remoteNpcs.find(n.id);
        if(it==g_remoteNpcs.end()){
            if(!specialModel&&!RemoteNpcMayTryModel(n.id,remoteModelId)) continue;
            if(!specialModel)LoadNetworkPedModel(remoteModelId);
            CPed* ped=CreateRemotePedForModel(24,remoteModelId,n.x,n.y,n.z,specialModel);
            if(!ped&&specialModel){ReleasePendingRemoteSpecialSlot(specialLeaseKey);continue;}
            bool fellBackToCj=false;
            if(!ped&&remoteModelId!=0){
                if(RemoteNpcLogOnce(n.id,4,remoteModelId))WeaponFileLog("REMOTE_NPC_MODEL_CREATE_FAIL id=%u model=%u model_ready=%u path=spawn (will retry)",n.id,remoteModelId,HasReadyPedModel(remoteModelId)?1u:0u);

                if(RemoteNpcNoteModelFailure(n.id,remoteModelId)) continue;
                if(RemoteNpcLogOnce(n.id,5,remoteModelId))WeaponFileLog("REMOTE_NPC_MODEL_GIVEUP id=%u model=%u fallback=0",n.id,remoteModelId);
                g_saUtils->MarkModelAsNotNeeded(remoteModelId);remoteModelId=0;fellBackToCj=true;
                LoadNetworkPedModel(remoteModelId);ped=g_saUtils->CreatePed(4,remoteModelId,n.x,n.y,n.z);
            }
            if(!ped){ReleaseRemoteSpecialLease(specialLeaseKey);continue;}
            if(!fellBackToCj)g_remoteNpcModelWait.erase(n.id);
            g_remoteNpcs[n.id]=ped; it=g_remoteNpcs.find(n.id);
            if(specialModel)CommitRemoteSpecialSlot(specialLeaseKey,specialSlot,targetSpecialName);else ReleaseRemoteSpecialLease(specialLeaseKey);
            if(!specialModel)g_saUtils->MarkModelAsNotNeeded(remoteModelId);
            if (n.health > 0) ApplyRemoteVitals(ped,n.health,n.armour);
            auto& visual=g_remoteNpcVisuals[n.id]; visual.x=n.x; visual.y=n.y; visual.z=n.z; visual.lastUpdateMs=npcNowMs; visual.initialized=true;
        }else{
            const uint16_t currentModel=*reinterpret_cast<const uint16_t*>(reinterpret_cast<uintptr_t>(it->second)+gtasa_v200_armv7::kCPedModelIndexOffset);
            auto leaseIt=g_remoteSpecialLeases.find(specialLeaseKey);
            const std::string appliedSpecialName=leaseIt!=g_remoteSpecialLeases.end()?leaseIt->second.activeName:"";
            if((currentModel!=remoteModelId||appliedSpecialName!=targetSpecialName)&&(specialModel||RemoteNpcMayTryModel(n.id,remoteModelId))){
                if(!specialModel)LoadNetworkPedModel(remoteModelId);
                CPed* replacement=CreateRemotePedForModel(24,remoteModelId,n.x,n.y,n.z,specialModel);
                if(!replacement&&specialModel){ReleasePendingRemoteSpecialSlot(specialLeaseKey);continue;}
                if(!replacement&&remoteModelId!=0){


                    if(RemoteNpcLogOnce(n.id,6,remoteModelId))WeaponFileLog("REMOTE_NPC_MODEL_CREATE_FAIL id=%u model=%u model_ready=%u path=replace (keeping current ped, will retry)",n.id,remoteModelId,HasReadyPedModel(remoteModelId)?1u:0u);
                    RemoteNpcNoteModelFailure(n.id,remoteModelId);
                }
                if(replacement){
                    g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(it->second));it->second=replacement;
                    if(specialModel)CommitRemoteSpecialSlot(specialLeaseKey,specialSlot,targetSpecialName);else ReleaseRemoteSpecialLease(specialLeaseKey);
                    if(!specialModel)g_saUtils->MarkModelAsNotNeeded(remoteModelId);
                    g_remoteNpcModelWait.erase(n.id);
                    g_remoteNpcVisuals.erase(n.id);
                    auto& visual=g_remoteNpcVisuals[n.id];visual.x=n.x;visual.y=n.y;visual.z=n.z;visual.lastUpdateMs=npcNowMs;visual.initialized=true;
                }
            }
        }
        CPed* ped=it->second; if(!ped) continue;
        auto& visual=g_remoteNpcVisuals[n.id];
        if (n.health == 0) {
            const int pedRef = CPools::GetPedRef(ped);
            if (!visual.deathApplied && pedRef > 0 && CPools::GetPed(pedRef) == ped) {
                if (ped->m_pVehicle && !n.vehicleId)
                    g_saUtils->ScriptCommand(&kScmWarpCharFromCarToCoord, pedRef,
                                             n.x, n.y, n.z - 1.0f);
                g_saUtils->ScriptCommand(&kScmSetCharProofs, pedRef, 0, 0, 0, 0, 0);
                g_saUtils->ScriptCommand(&kScmTaskDie, pedRef);
                visual.deathApplied = true;
                visual.animationInitialized = false;
                visual.animationAssociationCount = 0;
                WeaponFileLog("REMOTE_NPC_TASK_DIE id=%u pedRef=%d vehicleId=%u", n.id, pedRef, n.vehicleId);
            }
            if (ped->m_pVehicle && !n.vehicleId && pedRef > 0 && CPools::GetPed(pedRef) == ped)
                g_saUtils->ScriptCommand(&kScmWarpCharFromCarToCoord, pedRef,
                                         n.x, n.y, n.z - 1.0f);
            if (!ped->m_pVehicle) {
                g_saUtils->SetPosition(reinterpret_cast<CPhysical*>(ped), n.x, n.y, n.z);
                g_saUtils->SetAngle(reinterpret_cast<CPhysical*>(ped), static_cast<unsigned char>(2), n.heading);
            }
            visual.x=n.x; visual.y=n.y; visual.z=n.z; visual.lastUpdateMs=npcNowMs; visual.initialized=true;
            continue;
        }
        if (!n.vehicleId || n.vehicleSeat != 0)
            g_remoteCarjackVictimUntilMs.erase(ped);
        if (IsRemoteCarjackVictimProtected(ped)) {
            continue;
        }

        {
            auto& weaponVisual=g_remoteNpcVisuals[n.id];
            const uint64_t weaponNowMs=SteadyNowMs();
            if(weaponVisual.weapon!=n.weapon && weaponNowMs>=weaponVisual.nextWeaponTryMs){
                if(ApplyRemoteWeapon(ped,n.weapon)) weaponVisual.weapon=n.weapon;
                else weaponVisual.nextWeaponTryMs=weaponNowMs+250;
            }
        }
        if(seatedVehicle){
            int npcSeatIndex=-1;
            if(!SeatIndexFor(seatedVehicle,n.vehicleSeat,npcSeatIndex)) continue;
            const bool inCorrectSeat=n.vehicleSeat==0?seatedVehicle->m_pDriver==ped:seatedVehicle->m_apPassengers[npcSeatIndex]==ped;
            if(ped->m_pVehicle!=seatedVehicle||!inCorrectSeat) g_saUtils->PutPedInVehicle(ped,seatedVehicle,npcSeatIndex);
            auto& visual=g_remoteNpcVisuals[n.id]; visual.x=n.x; visual.y=n.y; visual.z=n.z; visual.lastUpdateMs=npcNowMs; visual.initialized=true;
            if(n.animationAssociationCount || visual.animationAssociationCount)
                ApplyAssociationStates(ped,n.id,n.animationAssociations.data(),n.animationAssociationCount,
                                       visual.animationAssociationCount,visual.animationAssociations,
                                       visual.animationInitialized);
            UpdateRemoteNpcAim(ped,n,visual,true);
            if(npcNowMs-visual.lastHealthReportMs>=100){const uint16_t health=static_cast<uint16_t>(std::max(0.0f,std::min(100.0f,ped->GetHealth())));const uint16_t armour=static_cast<uint16_t>(std::max(0.0f,std::min(100.0f,ped->m_fArmour)));g_client->SendNpcHealth(n.id,health,armour);visual.lastHealthReportMs=npcNowMs;}
            continue;
        }
        if(ped->m_pVehicle){
            CVehicle* oldVehicle = ped->m_pVehicle;
            const int pedRef = CPools::GetPedRef(ped);
            if (pedRef > 0 && CPools::GetPed(pedRef) == ped) {
                g_saUtils->ScriptCommand(&kScmWarpCharFromCarToCoord, pedRef,
                                         n.x, n.y, n.z - 1.0f);
            }
            if (oldVehicle) {
                if (oldVehicle->m_pDriver == ped) oldVehicle->m_pDriver = nullptr;
                for (auto& passenger : oldVehicle->m_apPassengers)
                    if (passenger == ped) passenger = nullptr;
                if (ped->m_pVehicle == oldVehicle) ped->m_pVehicle = nullptr;
                ped->bInVehicle = false;
            }
            g_saUtils->SetPosition(reinterpret_cast<CPhysical*>(ped), n.x, n.y, n.z);
            g_saUtils->SetAngle(reinterpret_cast<CPhysical*>(ped), static_cast<unsigned char>(2), n.heading);
        }
        auto* physical=reinterpret_cast<CPhysical*>(ped);
        if(!visual.initialized){ visual.x=n.x; visual.y=n.y; visual.z=n.z; visual.lastUpdateMs=npcNowMs; visual.initialized=true; }
        const uint64_t npcFrameElapsedMs=coopfusion_sync::ElapsedMs(npcNowMs,visual.lastUpdateMs);
        const float oldX=visual.x, oldY=visual.y, oldZ=visual.z;
        const float targetDx=n.x-visual.x, targetDy=n.y-visual.y, targetDz=n.z-visual.z;
        const float targetD2=targetDx*targetDx+targetDy*targetDy+targetDz*targetDz;
        const float npcPositionBlend=coopfusion_sync::BlendForElapsedMs(
            npcFrameElapsedMs, targetD2>25.0f ? 22.0f : 42.0f);
        const bool snap=targetD2>900.0f;
        if(snap){ visual.x=n.x; visual.y=n.y; visual.z=n.z; }
        else if(targetD2>0.000001f){
            visual.x=SmoothValue(visual.x,n.x,npcPositionBlend);
            visual.y=SmoothValue(visual.y,n.y,npcPositionBlend);
            visual.z=SmoothValue(visual.z,n.z,npcPositionBlend);
            if(std::fabs(n.x-visual.x)<0.015f&&std::fabs(n.y-visual.y)<0.015f&&std::fabs(n.z-visual.z)<0.015f){ visual.x=n.x; visual.y=n.y; visual.z=n.z; }
        }
        const float stepX=visual.x-oldX, stepY=visual.y-oldY, stepZ=visual.z-oldZ;
        physical->m_vecMoveSpeed=snap?CVector(0.0f,0.0f,0.0f):CVector(n.vx,n.vy,n.vz);
        g_saUtils->SetPosition(physical,visual.x,visual.y,visual.z);
        g_saUtils->SetAngle(physical,(unsigned char)2,n.heading);
        ped->m_fCurrentRotation=n.currentRotation;
        ped->m_fAimingRotation=n.aimingRotation;
        ped->m_fLookingDirection=n.lookingDirection;
        ped->m_nFightingStyle=static_cast<char>(n.fightingStyle);
        if(npcNowMs-visual.lastHealthReportMs>=100){
            const uint16_t health=static_cast<uint16_t>(std::max(0.0f,std::min(100.0f,ped->GetHealth())));
            const uint16_t armour=static_cast<uint16_t>(std::max(0.0f,std::min(100.0f,ped->m_fArmour)));
            g_client->SendNpcHealth(n.id,health,armour); visual.lastHealthReportMs=npcNowMs;
        }
        if(n.animationAssociationCount || visual.animationAssociationCount)
            ApplyAssociationStates(ped,n.id,n.animationAssociations.data(),n.animationAssociationCount,
                                   visual.animationAssociationCount,visual.animationAssociations,
                                   visual.animationInitialized);
        else if(g_setMoveState&&g_setMoveAnim){
            const int moveState=std::max(1,std::min(7,static_cast<int>(n.moveState)));
            g_setMoveState(ped,moveState);
            g_setMoveAnim(ped);
        }
        UpdateRemoteNpcAim(ped,n,visual,false);
    }
}

void SpawnTestNpc(const NpcSpawnTest& test)
{
    logger->Info("NPC_TEST received: model=%d position=(%0.1f,%0.1f,%0.1f)", test.modelId, test.x, test.y, test.z);
    if (!g_spawnedNpcs.empty() && g_spawnedNpcs.front()) {
        if (ApplyRemoteWeapon(g_spawnedNpcs.front(), test.weapon))
            logger->Info("NPC test switched to room leader weapon ID %u", test.weapon);
        else
            logger->Error("NPC test could not switch to room leader weapon ID %u", test.weapon);
        return;
    }
    if (!g_saUtils) {
        logger->Error("NPC test ignored: SAUtils 1.6 is unavailable");
        return;
    }
    if (!IsSupportedNpcModel(test.modelId)) {
        logger->Error("NPC test ignored: unsupported ped model %d", test.modelId);
        return;
    }
    if (!g_saUtils->IsGameInitialised()) {
        logger->Info("NPC test delayed: GTA world is not initialized yet");
        return;
    }


    g_saUtils->LoadModelId(test.modelId);
    CStreaming::RequestModel(356, 2);
    CStreaming::LoadAllRequestedModels(false);
    CPed* ped = g_saUtils->CreatePed(4, test.modelId, test.x, test.y, test.z);
    if (!ped) {
        logger->Error("NPC test CreatePed returned null for model %d", test.modelId);
        return;
    }

    if (ApplyRemoteWeapon(ped, test.weapon))
        logger->Info("NPC test weapon %u equipped through CPed::GiveWeapon/SetCurrentWeapon", test.weapon);
    else
        logger->Error("NPC test weapon %u could not be equipped; verified weapon symbols unavailable", test.weapon);
    g_spawnedNpcs.push_back(ped);
    g_saUtils->MarkModelAsNotNeeded(test.modelId);
    logger->Info("NPC model %d spawned at (%0.1f, %0.1f, %0.1f)",
                 test.modelId,
                 test.x, test.y, test.z);
}

bool IsLiveVehicleBinding(const VehicleEntityBinding& binding)
{
    return binding.vehicle && binding.poolRef > 0 && CPools::GetVehicle(binding.poolRef) == binding.vehicle;
}

void TrackLocalVehicleEntity(CVehicle* vehicle)
{
    if (!vehicle) return;
    const int ref = CPools::GetVehicleRef(vehicle);
    if (ref <= 0 || CPools::GetVehicle(ref) != vehicle) return;
    const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vehicle));
    if (!key) return;
    VehicleEntityBinding binding{vehicle, ref, false};
    auto prior = g_localVehicleEntities.find(key);
    if (prior != g_localVehicleEntities.end() && prior->second.vehicle == vehicle &&
        prior->second.poolRef == ref) binding.createdByFusion = prior->second.createdByFusion;
    for (const auto& entry : g_vehicleEntityByNetworkId) {
        if (entry.second.vehicle == vehicle && entry.second.poolRef == ref) {
            binding.createdByFusion = entry.second.createdByFusion;
            break;
        }
    }
    g_localVehicleEntities[key] = binding;
}

void RefreshLocalVehicleIdentityBindings()
{
    if (!g_client) return;
    for (auto it = g_localVehicleEntities.begin(); it != g_localVehicleEntities.end();) {
        if (!IsLiveVehicleBinding(it->second)) {
            it = g_localVehicleEntities.erase(it);
            continue;
        }
        const uint16_t networkId = g_client->LocalVehicleNetworkIdForKey(it->first);
        if (networkId) {
            auto known = g_vehicleEntityByNetworkId.find(networkId);
            if (known == g_vehicleEntityByNetworkId.end() || !IsLiveVehicleBinding(known->second) ||
                known->second.vehicle == it->second.vehicle) {
                VehicleEntityBinding binding = it->second;
                g_vehicleEntityByNetworkId[networkId] = binding;
            }
        }
        ++it;
    }
    for (auto it = g_vehicleEntityByNetworkId.begin(); it != g_vehicleEntityByNetworkId.end();) {
        if (!IsLiveVehicleBinding(it->second)) it = g_vehicleEntityByNetworkId.erase(it);
        else ++it;
    }
}

static VehicleExtendedState CaptureVehicleExtendedState(CVehicle* vehicle) {
    VehicleExtendedState e{};
    if (!vehicle) return e;
    e.valid = true;
    e.tertiaryColor = vehicle->m_nTertiaryColor;
    e.quaternaryColor = vehicle->m_nQuaternaryColor;
    e.extraA = static_cast<int8_t>(vehicle->m_anExtras[0]);
    e.extraB = static_cast<int8_t>(vehicle->m_anExtras[1]);
    e.handbrake = vehicle->bIsHandbrakeOn ? 1 : 0;
    if (vehicle->IsPlane()) {
        e.landingGearValid = true;
        e.landingGear = vehicle->AsPlane()->m_fLandingGearStatus;
    }
    if (vehicle->IsHeli()) {
        const CHeli* heli = vehicle->AsHeli();
        e.searchLightValid = true;
        e.searchLightX = heli->m_vecSearchLightTarget.x;
        e.searchLightY = heli->m_vecSearchLightTarget.y;
        e.searchLightZ = heli->m_vecSearchLightTarget.z;
        e.searchLightIntensity = heli->m_fSearchLightIntensity;
        e.searchLightEnabled = heli->m_bSearchLightEnabled ? 1 : 0;
    }
    if (vehicle->IsBike() || vehicle->IsBMX()) {
        const CBike* bike = vehicle->AsBike();
        e.bikeLeanValid = true;
        e.bikeLean = bike->m_rideAnimData.m_fAnimLean;
        e.bikeLeanForward = bike->m_rideAnimData.m_fAnimLeanFwd;
    }
    if (vehicle->IsBMX()) {
        const CBmx* bmx = vehicle->AsBmx();
        e.bmxValid = true;
        e.bmxSprintLean = bmx->m_fSprintLeanAngle;
        e.bmxPedallingCharge = bmx->m_fPedallingCharge;
    }
    return e;
}

static void CaptureLocalVehicleOrientation(CVehicle* vehicle);
static void CaptureLocalVehicleDetails(CVehicle* vehicle) {
    if (!vehicle || !g_client) return;
    TrackLocalVehicleEntity(vehicle);
    int paintjob = -1;
    const int vehicleRef = CPools::GetVehicleRef(vehicle);
    if (g_saUtils && vehicleRef > 0 && CPools::GetVehicle(vehicleRef) == vehicle)
        g_saUtils->ScriptCommand(&kScmGetCurrentVehiclePaintjob, vehicleRef, &paintjob);
    g_client->SetLocalVehicleAppearance(static_cast<int8_t>(paintjob), vehicle->m_anUpgrades);
    uint8_t tyres=0; uint32_t doors=0,lights=0,panels=0;
    if (vehicle->IsAutomobile()) { auto* car=vehicle->AsAutomobile(); auto& d=car->m_damageManager;
        for(int i=0;i<4;++i) tyres|=static_cast<uint8_t>((d.m_anWheelsStatus[i]&3u)<<(i*2));
        for(int i=0;i<6;++i) doors|=static_cast<uint32_t>(d.m_anDoorsStatus[i])<<(i*4);
        lights=d.m_nLightsStatus; panels=d.m_nPanelsStatus; }
    g_client->SetLocalVehicleDetails(vehicle->m_nPrimaryColor,vehicle->m_nSecondaryColor,vehicle->bEngineOn?1:0,vehicle->bLightsOn?1:0,vehicle->bSirenOrAlarm?1:0,static_cast<uint8_t>(vehicle->m_eDoorLock),std::max(0.0f,std::min(15.0f,vehicle->m_fDirtLevel)),tyres,doors,lights,panels,FindRemoteVehicleNetworkId(vehicle->m_pTrailer));
    uint16_t misc=vehicle->IsAutomobile()?static_cast<uint16_t>(std::max(0,static_cast<int>(vehicle->AsAutomobile()->m_wMiscComponentAngle))):0;
    float turretV=0.0f, turretH=0.0f;
    if (vehicle->IsAutomobile()) {
        turretV=vehicle->AsAutomobile()->m_fDoomVerticalRotation;
        turretH=vehicle->AsAutomobile()->m_fDoomHorizontalRotation;
    }
    g_client->SetLocalVehicleControls(vehicle->m_fSteerAngle,vehicle->m_fGasPedal,vehicle->m_fBreakPedal,misc,turretV,turretH);
    g_client->SetLocalVehicleExtended(CaptureVehicleExtendedState(vehicle));
    const CVector& turnSpeed=reinterpret_cast<CPhysical*>(vehicle)->m_vecTurnSpeed;
    g_client->SetLocalVehicleTurnSpeed(turnSpeed.x,turnSpeed.y,turnSpeed.z);
    CaptureLocalVehicleOrientation(vehicle);
}
static void CaptureLocalVehicleOrientation(CVehicle* vehicle) {
    if (!vehicle || !g_client || !vehicle->GetMatrix()) return;
    const CMatrix* m = vehicle->GetMatrix();
    const CVector& r=m->right; const CVector& u=m->up; const CVector& a=m->at;
    g_client->SetLocalVehicleOrientation(r.x,r.y,r.z,u.x,u.y,u.z,a.x,a.y,a.z);
}
static void ApplyRemoteVehicleOrientation(CVehicle* vehicle,const RemoteVehicle& state, float blend = 0.62f) {
    if (!vehicle || !state.orientationValid || !vehicle->GetMatrix()) return;
    CMatrix* m=vehicle->GetMatrix();
    blend = std::max(0.0f, std::min(1.0f, blend));
    m->right += (CVector(state.rightX,state.rightY,state.rightZ) - m->right) * blend;
    m->up += (CVector(state.upX,state.upY,state.upZ) - m->up) * blend;
    m->at += (CVector(state.atX,state.atY,state.atZ) - m->at) * blend;
    m->Reorthogonalise(); if (m->m_pAttachMatrix) m->UpdateRwMatrix(m->m_pAttachMatrix);
}
static void ApplyRemoteVehicleDetails(CVehicle* vehicle,const RemoteVehicle& state) {
    if(!vehicle)return;
    vehicle->m_nPrimaryColor=state.primaryColor; vehicle->m_nSecondaryColor=state.secondaryColor;
    vehicle->bEngineOn=state.engineOn!=0; vehicle->bLightsOn=state.lightsOn!=0;
    vehicle->bSirenOrAlarm=state.sirenOn!=0; vehicle->m_eDoorLock=static_cast<eDoorLock>(state.doorLock);
    vehicle->m_fDirtLevel=std::max(0.0f,std::min(15.0f,state.dirt));
    vehicle->m_fSteerAngle=state.steering; vehicle->m_fGasPedal=state.gasPedal; vehicle->m_fBreakPedal=state.brakePedal;
    if (state.extended.valid) {
        vehicle->m_nTertiaryColor=state.extended.tertiaryColor;
        vehicle->m_nQuaternaryColor=state.extended.quaternaryColor;
        vehicle->m_anExtras[0]=static_cast<char>(state.extended.extraA);
        vehicle->m_anExtras[1]=static_cast<char>(state.extended.extraB);
        vehicle->bIsHandbrakeOn=state.extended.handbrake!=0;
        if (state.extended.landingGearValid && vehicle->IsPlane())
            vehicle->AsPlane()->m_fLandingGearStatus=state.extended.landingGear;
        if (state.extended.searchLightValid && vehicle->IsHeli()) {
            CHeli* heli=vehicle->AsHeli();
            heli->m_vecSearchLightTarget=CVector(state.extended.searchLightX,state.extended.searchLightY,state.extended.searchLightZ);
            heli->m_fSearchLightIntensity=state.extended.searchLightIntensity;
            heli->m_bSearchLightEnabled=state.extended.searchLightEnabled!=0;
        }
        if (state.extended.bikeLeanValid && (vehicle->IsBike() || vehicle->IsBMX())) {
            CBike* bike=vehicle->AsBike();
            bike->m_rideAnimData.m_fAnimLean=state.extended.bikeLean;
            bike->m_rideAnimData.m_fDesiredAnimLean=state.extended.bikeLean;
            bike->m_rideAnimData.m_fAnimLeanFwd=state.extended.bikeLeanForward;
        }
        if (state.extended.bmxValid && vehicle->IsBMX()) {
            CBmx* bmx=vehicle->AsBmx();
            bmx->m_fSprintLeanAngle=state.extended.bmxSprintLean;
            bmx->m_fPedallingCharge=state.extended.bmxPedallingCharge;
        }
    }
    if(!vehicle->IsAutomobile())return;
    auto* car=vehicle->AsAutomobile();
    car->m_wMiscComponentAngle=static_cast<short>(state.miscComponentAngle);
    car->m_fDoomVerticalRotation=state.turretVertical;
    car->m_fDoomHorizontalRotation=state.turretHorizontal;
    auto& d=car->m_damageManager; for(int i=0;i<4;++i)d.m_anWheelsStatus[i]=static_cast<uint8_t>((state.tyreStatus>>(i*2))&3u);
    for(int i=0;i<6;++i){d.m_anDoorsStatus[i]=static_cast<uint8_t>((state.doorStatus>>(i*4))&0xFu);car->m_doors[i].m_nDoorState=static_cast<eDoorState>(d.m_anDoorsStatus[i]);}
    d.m_nLightsStatus=state.lightStatus; d.m_nPanelsStatus=state.panelStatus;
}
static std::unordered_map<uint16_t, uint64_t> g_remoteVehicleLookRetryMs;

static void ApplyRemoteVehicleAppearance(CVehicle* vehicle, uint16_t networkId, const RemoteVehicle& state)
{
    if (!vehicle || !g_saUtils || !state.appearance.valid) return;
    const uint64_t nowMs = SteadyNowMs();
    auto& lastTry = g_remoteVehicleLookRetryMs[networkId];
    if (lastTry && nowMs >= lastTry && nowMs - lastTry < 100) return;
    lastTry = nowMs;

    const int handle = CPools::GetVehicleRef(vehicle);
    if (handle <= 0 || CPools::GetVehicle(handle) != vehicle) return;

    CBaseModelInfo** modelInfos = CModelInfo::ms_modelInfoPtrs;
    bool requestedAny = false;
    for (size_t slot = 0; slot < state.appearance.upgrades.size(); ++slot) {
        const int want = state.appearance.upgrades[slot];
        if (want < 0 || want >= static_cast<int>(kModelInfoCapacity) ||
            vehicle->m_anUpgrades[slot] == want || !modelInfos || !modelInfos[want] ||
            modelInfos[want]->GetModelType() != MODEL_INFO_ATOMIC ||
            !CStreaming::IsObjectInCdImage(want)) continue;
        g_saUtils->ScriptCommand(&kScmRequestVehicleMod, want);
        requestedAny = true;
    }
    if (requestedAny) g_saUtils->ScriptCommand(&kScmLoadAllRequestedModels);

    for (size_t slot = 0; slot < state.appearance.upgrades.size(); ++slot) {
        const int have = vehicle->m_anUpgrades[slot];
        const int want = state.appearance.upgrades[slot];
        if (want == have) continue;
        if (want < 0) {
            if (have >= 0) g_saUtils->ScriptCommand(&kScmRemoveVehicleMod, handle, have);
            continue;
        }
        if (want >= static_cast<int>(kModelInfoCapacity) || !modelInfos || !modelInfos[want] ||
            modelInfos[want]->GetModelType() != MODEL_INFO_ATOMIC || !CStreaming::IsObjectInCdImage(want))
            continue;
        if (!g_saUtils->ScriptCommand(&kScmHasVehicleModLoaded, want)) continue;
        if (have >= 0) g_saUtils->ScriptCommand(&kScmRemoveVehicleMod, handle, have);
        int dummy = 0;
        g_saUtils->ScriptCommand(&kScmAddVehicleMod, handle, want, &dummy);
    }

    const int paintjob = state.appearance.paintjob;
    if (paintjob >= 0 && paintjob < 4) {
        int currentPaintjob = -1;
        g_saUtils->ScriptCommand(&kScmGetCurrentVehiclePaintjob, handle, &currentPaintjob);
        if (currentPaintjob != paintjob)
            g_saUtils->ScriptCommand(&kScmGiveVehiclePaintjob, handle, paintjob);
    }
}

void UpdateRemoteVehicles()
{
    if (!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;

    RefreshLocalVehicleIdentityBindings();
    for (const auto& dead : g_client->DrainRemovedVehicles()) {
        auto remoteIt = g_remoteVehicles.find(dead);
        auto bindingIt = g_vehicleEntityByNetworkId.find(dead);
        CVehicle* oldVehicle = remoteIt != g_remoteVehicles.end() ? remoteIt->second :
            (bindingIt != g_vehicleEntityByNetworkId.end() ? bindingIt->second.vehicle : nullptr);
        const bool createdByFusion = bindingIt != g_vehicleEntityByNetworkId.end()
            ? bindingIt->second.createdByFusion : (remoteIt != g_remoteVehicles.end());
        if (createdByFusion && oldVehicle && IsLiveVehicleBinding(bindingIt != g_vehicleEntityByNetworkId.end()
                ? bindingIt->second : VehicleEntityBinding{oldVehicle, CPools::GetVehicleRef(oldVehicle), true}))
            g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(oldVehicle));
        if (remoteIt != g_remoteVehicles.end()) g_remoteVehicles.erase(remoteIt);
        g_remoteVehicleVisuals.erase(dead);
        g_remoteVehicleLookRetryMs.erase(dead);
        g_vehicleEntityByNetworkId.erase(dead);
        for (auto localIt = g_localVehicleEntities.begin(); localIt != g_localVehicleEntities.end();) {
            if (!IsLiveVehicleBinding(localIt->second)) localIt = g_localVehicleEntities.erase(localIt);
            else ++localIt;
        }
        logger->Info("Remote vehicle %u released by network owner (preserve-native=%d)", dead, createdByFusion ? 0 : 1);
    }

    const uint64_t vehicleUpdateNowMs = SteadyNowMs();
    for (const auto& v : g_client->GetRemoteVehicles()) {
        if (v.ownerId == g_client->LocalId()) {
            auto localIt = g_remoteVehicles.find(v.id);
            if (localIt != g_remoteVehicles.end()) {
                CVehicle* physical = localIt->second;
                int ref = physical ? CPools::GetVehicleRef(physical) : 0;
                if (physical && ref > 0 && CPools::GetVehicle(ref) == physical) {
                    bool createdByFusion = true;
                    auto known = g_vehicleEntityByNetworkId.find(v.id);
                    if (known != g_vehicleEntityByNetworkId.end() && known->second.vehicle == physical &&
                        known->second.poolRef == ref) createdByFusion = known->second.createdByFusion;
                    g_vehicleEntityByNetworkId[v.id] = VehicleEntityBinding{physical, ref, createdByFusion};
                }
                g_remoteVehicles.erase(localIt);
                g_remoteVehicleVisuals.erase(v.id);
                TrackLocalVehicleEntity(physical);
                logger->Info("Remote vehicle %u promoted to local authority (entity retained)", v.id);
            }
            continue;
        }
        auto it = g_remoteVehicles.find(v.id);
        if (it != g_remoteVehicles.end() && it->second) {
            auto known = g_vehicleEntityByNetworkId.find(v.id);
            if (known == g_vehicleEntityByNetworkId.end() || known->second.vehicle != it->second) {
                const int ref = CPools::GetVehicleRef(it->second);
                if (ref > 0 && CPools::GetVehicle(ref) == it->second)
                    g_vehicleEntityByNetworkId[v.id] = VehicleEntityBinding{it->second, ref, true};
            }
        }
        if (it == g_remoteVehicles.end() || !it->second) {
            auto known = g_vehicleEntityByNetworkId.find(v.id);
            CVehicle* vehicle = nullptr;
            if (known != g_vehicleEntityByNetworkId.end() && IsLiveVehicleBinding(known->second))
                vehicle = known->second.vehicle;
            if (!vehicle) {
                if (!IsSafeNetworkVehicleModel(v.modelId)) continue;
                g_saUtils->LoadModelId(v.modelId);
                vehicle = g_saUtils->CreateVehicle(v.modelId, v.x, v.y, v.z);
                if (!vehicle) {
                    logger->Error("Remote vehicle %u CreateVehicle failed model=%u", v.id, v.modelId);
                    continue;
                }
                const int ref = CPools::GetVehicleRef(vehicle);
                if (ref > 0 && CPools::GetVehicle(ref) == vehicle)
                    g_vehicleEntityByNetworkId[v.id] = VehicleEntityBinding{vehicle, ref, true};
                g_saUtils->MarkModelAsNotNeeded(v.modelId);
                logger->Info("Remote vehicle %u spawned model=%u at (%.1f,%.1f,%.1f)",
                             v.id, v.modelId, v.x, v.y, v.z);
            } else {
                logger->Info("Remote vehicle %u reused existing local-world vehicle ref=%d", v.id,
                             CPools::GetVehicleRef(vehicle));
            }
            g_remoteVehicles[v.id] = vehicle;
            RemoteVehicleVisualState initial{};
            initial.x = v.x; initial.y = v.y; initial.z = v.z; initial.heading = v.heading;
            initial.rightX = v.rightX; initial.rightY = v.rightY; initial.rightZ = v.rightZ;
            initial.upX = v.upX; initial.upY = v.upY; initial.upZ = v.upZ;
            initial.atX = v.atX; initial.atY = v.atY; initial.atZ = v.atZ;
            initial.lastUpdateMs = SteadyNowMs();
            initial.initialized = true;
            g_remoteVehicleVisuals[v.id] = initial;
            it = g_remoteVehicles.find(v.id);
        }

        CVehicle* vehicle = it->second;
        if (!vehicle) continue;
        auto& visual = g_remoteVehicleVisuals[v.id];
        if (!visual.initialized) {
            visual.x = v.x; visual.y = v.y; visual.z = v.z; visual.heading = v.heading;
            visual.rightX = v.rightX; visual.rightY = v.rightY; visual.rightZ = v.rightZ;
            visual.upX = v.upX; visual.upY = v.upY; visual.upZ = v.upZ;
            visual.atX = v.atX; visual.atY = v.atY; visual.atZ = v.atZ;
            visual.lastUpdateMs = SteadyNowMs();
            visual.initialized = true;
        } else {
            const uint64_t frameElapsedMs = coopfusion_sync::ElapsedMs(vehicleUpdateNowMs, visual.lastUpdateMs);
            const float positionBlend = coopfusion_sync::BlendForElapsedMs(frameElapsedMs, 31.0f);
            const float orientationBlend = coopfusion_sync::BlendForElapsedMs(frameElapsedMs, 20.0f);
            const float dx = v.x - visual.x;
            const float dy = v.y - visual.y;
            const float dz = v.z - visual.z;
            if (dx * dx + dy * dy + dz * dz > 900.0f) {
                visual.x = v.x; visual.y = v.y; visual.z = v.z; visual.heading = v.heading;
            } else {
                visual.x = SmoothValue(visual.x, v.x, positionBlend);
                visual.y = SmoothValue(visual.y, v.y, positionBlend);
                visual.z = SmoothValue(visual.z, v.z, positionBlend);
                visual.heading += AngleDelta(v.heading, visual.heading) * positionBlend;
            }
            if (v.orientationValid) {
                const auto currentRotation = coopfusion_sync::Quaternion::FromBasis(
                    visual.rightX, visual.rightY, visual.rightZ,
                    visual.upX, visual.upY, visual.upZ,
                    visual.atX, visual.atY, visual.atZ);
                const auto targetRotation = coopfusion_sync::Quaternion::FromBasis(
                    v.rightX, v.rightY, v.rightZ,
                    v.upX, v.upY, v.upZ,
                    v.atX, v.atY, v.atZ);
                const auto blendedRotation = coopfusion_sync::Quaternion::Slerp(
                    currentRotation, targetRotation, orientationBlend);
                blendedRotation.ToBasis(
                    visual.rightX, visual.rightY, visual.rightZ,
                    visual.upX, visual.upY, visual.upZ,
                    visual.atX, visual.atY, visual.atZ);
            }
        }
        auto* physical = reinterpret_cast<CPhysical*>(vehicle);
        g_saUtils->SetPosition(physical, visual.x, visual.y, visual.z);
        RemoteVehicle smoothed = v;
        smoothed.rightX = visual.rightX; smoothed.rightY = visual.rightY; smoothed.rightZ = visual.rightZ;
        smoothed.upX = visual.upX; smoothed.upY = visual.upY; smoothed.upZ = visual.upZ;
        smoothed.atX = visual.atX; smoothed.atY = visual.atY; smoothed.atZ = visual.atZ;
        smoothed.orientationValid = v.orientationValid;
        ApplyRemoteVehicleOrientation(vehicle, smoothed, 1.0f);
        physical->m_vecMoveSpeed = CVector(v.vx, v.vy, v.vz);
        physical->m_vecTurnSpeed = CVector(v.turnVx,v.turnVy,v.turnVz);
        vehicle->m_fHealth = std::max(0.0f, std::min(1000.0f, v.health));
        ApplyRemoteVehicleDetails(vehicle, v);
        ApplyRemoteVehicleAppearance(vehicle, v.id, v);
    }
}

uint16_t FindRemoteVehicleNetworkId(CVehicle* vehicle)
{
    if(!vehicle) return 0;
    for(const auto& kv : g_remoteVehicles) if(kv.second == vehicle) return kv.first;
    return 0;
}

void ReapplyRemoteVehicleHeading(CVehicle* vehicle)
{
    if(!vehicle || !g_client) return;
    const uint16_t id=FindRemoteVehicleNetworkId(vehicle); if(!id) return;
    for(const auto& state:g_client->GetRemoteVehicles()) if(state.id==id){
        auto visual = g_remoteVehicleVisuals.find(id);
        if (visual != g_remoteVehicleVisuals.end() && visual->second.initialized) {
            RemoteVehicle smoothed = state;
            smoothed.rightX = visual->second.rightX; smoothed.rightY = visual->second.rightY; smoothed.rightZ = visual->second.rightZ;
            smoothed.upX = visual->second.upX; smoothed.upY = visual->second.upY; smoothed.upZ = visual->second.upZ;
            smoothed.atX = visual->second.atX; smoothed.atY = visual->second.atY; smoothed.atZ = visual->second.atZ;
            ApplyRemoteVehicleOrientation(vehicle,smoothed);
        } else {
            ApplyRemoteVehicleOrientation(vehicle,state);
        }
        return;
    }
}

CPed* LiveRemotePed(uint16_t id)
{
    auto it = g_remotePeds.find(id);
    if (it == g_remotePeds.end() || !it->second) return nullptr;
    CPed* ped = it->second;
    auto refIt = g_remotePedRefs.find(id);
    const bool handleOk = refIt != g_remotePedRefs.end() && CPools::GetPed(refIt->second) == ped;
    if (handleOk && ped->m_pRwClump) return ped;


    ReleaseRemoteCutscenePartner(id, "remote ped became invalid");
    g_remoteTaskPermitUntilMs.erase(it->second);
    g_remoteCarjackVictimUntilMs.erase(it->second);
    g_remotePeds.erase(it);
    g_remotePedRefs.erase(id);
    g_remoteVisuals.erase(id);
    if (handleOk) DiscardRemotePed(ped, "body gone");
    else ReleaseRemoteClothesForPed(ped);
    WeaponFileLog("REMOTE_PED_RECREATE id=%u handleOk=%d", static_cast<unsigned>(id), handleOk ? 1 : 0);
    return nullptr;
}

void TickCutsceneMocap()
{
    if (!g_cutsceneMocapBuildStarted) {
        g_cutsceneMocapBuildStarted = true;
        coop_cutscene_mocap::StartBuildAsync();
        logger->Info("CUTSCENE_MOCAP_BUILD_STARTED source=anim/cuts.img");
        CutsceneAnimLog("CUTSCENE_MOCAP_BUILD_STARTED source=anim/cuts.img");
    }

    const coop_cutscene_mocap::State state = coop_cutscene_mocap::GetState();
    if (state == coop_cutscene_mocap::State::Failed) {
        if (!g_cutsceneMocapFailureLogged) {
            g_cutsceneMocapFailureLogged = true;
            logger->Error("CUTSCENE_MOCAP_UNAVAILABLE: %s", coop_cutscene_mocap::GetLastError());
            CutsceneAnimLog("CUTSCENE_MOCAP_UNAVAILABLE error=%s", coop_cutscene_mocap::GetLastError());
        }
        return;
    }
    if (!g_getAnimationBlockIndex) return;

    const int currentBlockIndex = g_getAnimationBlockIndex("COOPCS");
    if (state == coop_cutscene_mocap::State::Loaded && currentBlockIndex < 0) {
        coop_cutscene_mocap::MarkBlockMissing();
        logger->Print(LogP_Warn, "CUTSCENE_MOCAP_BLOCK_REMOVED; queued cached COOPCS pack for reload");
        CutsceneAnimLog("CUTSCENE_MOCAP_BLOCK_REMOVED reload=queued");
    } else if (state == coop_cutscene_mocap::State::Ready && currentBlockIndex >= 0) {
        coop_cutscene_mocap::MarkLoaded();
        return;
    }

    if (coop_cutscene_mocap::GetState() != coop_cutscene_mocap::State::Ready) return;
    if (!g_cutsceneMocapReadyLogged) {
        g_cutsceneMocapReadyLogged = true;
        logger->Info("CUTSCENE_MOCAP_PACK_READY source=%s standing=%u walking=%u",
                     coop_cutscene_mocap::GetSourcePath(),
                     static_cast<unsigned>(coop_cutscene_mocap::GetStandingClipCount()),
                     static_cast<unsigned>(coop_cutscene_mocap::GetWalkClipCount()));
        CutsceneAnimLog("CUTSCENE_MOCAP_PACK_READY source=%s standing=%u walking=%u",
                        coop_cutscene_mocap::GetSourcePath(),
                        static_cast<unsigned>(coop_cutscene_mocap::GetStandingClipCount()),
                        static_cast<unsigned>(coop_cutscene_mocap::GetWalkClipCount()));
    }
    if (!g_rwStreamOpen || !g_rwStreamClose || !g_loadAnimFile ||
        !g_getAnimationBlock || !g_animManagerNumBlocks || !g_animManagerNumAnimations ||
        !g_cutsceneLoadStatus || !g_cutsceneAnimLoaded || !g_cutsceneManagerRunning) {
        if (!g_cutsceneMocapFailureLogged) {
            logger->Error("CUTSCENE_MOCAP_GAME_API_MISSING streamOpen=%d streamClose=%d loadAnim=%d getBlock=%d blockCount=%d animCount=%d cutsceneState=%d",
                g_rwStreamOpen != nullptr, g_rwStreamClose != nullptr, g_loadAnimFile != nullptr,
                g_getAnimationBlock != nullptr, g_animManagerNumBlocks != nullptr,
                g_animManagerNumAnimations != nullptr, g_cutsceneLoadStatus != nullptr &&
                g_cutsceneAnimLoaded != nullptr && g_cutsceneManagerRunning != nullptr);
            g_cutsceneMocapFailureLogged = true;
            CutsceneAnimLog("CUTSCENE_MOCAP_GAME_API_MISSING streamOpen=%d streamClose=%d loadAnim=%d getBlock=%d blockCount=%d animCount=%d",
                g_rwStreamOpen != nullptr, g_rwStreamClose != nullptr, g_loadAnimFile != nullptr,
                g_getAnimationBlock != nullptr, g_animManagerNumBlocks != nullptr,
                g_animManagerNumAnimations != nullptr);
        }
        return;
    }

    if (*g_cutsceneLoadStatus != 0 || *g_cutsceneAnimLoaded || *g_cutsceneManagerRunning)
        return;

    const uint64_t nowMs = SteadyNowMs();
    if (nowMs < g_cutsceneMocapNextLoadAttemptMs) return;
    g_cutsceneMocapNextLoadAttemptMs = nowMs + 1000ULL;

    const size_t totalAnimations = coop_cutscene_mocap::GetStandingClipCount() +
                                   coop_cutscene_mocap::GetWalkClipCount();
    if (*g_animManagerNumBlocks + 2 >= 180 ||
        *g_animManagerNumAnimations + static_cast<int32_t>(totalAnimations) >= 2500) {
        coop_cutscene_mocap::MarkLoadFailed("CAnimManager animation or block tables are full; COOPCS was not loaded");
        logger->Error("CUTSCENE_MOCAP_CAPACITY_REACHED blocks=%d animations=%d adding=%u",
                      *g_animManagerNumBlocks, *g_animManagerNumAnimations,
                      static_cast<unsigned>(totalAnimations));
        CutsceneAnimLog("CUTSCENE_MOCAP_CAPACITY_REACHED blocks=%d animations=%d adding=%u",
                        *g_animManagerNumBlocks, *g_animManagerNumAnimations,
                        static_cast<unsigned>(totalAnimations));
        return;
    }

    size_t packSize = 0;
    const uint8_t* packData = coop_cutscene_mocap::GetAnimPackData(&packSize);
    if (!packData || packSize < 16 || packSize > UINT32_MAX) return;

    RwStreamMemory memory{};
    memory.position = 0;
    memory.nSize = static_cast<RwUInt32>(packSize);
    memory.memBlock = const_cast<RwUInt8*>(reinterpret_cast<const RwUInt8*>(packData));
    RwStream* stream = g_rwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &memory);
    if (!stream) {
        logger->Print(LogP_Warn, "CUTSCENE_MOCAP_STREAM_OPEN_FAILED; will retry");
        CutsceneAnimLog("CUTSCENE_MOCAP_STREAM_OPEN_FAILED retry=1");
        return;
    }
    static const char noUncompressedAnimNames[1][32] = {{0}};
    g_loadAnimFile(stream, true, noUncompressedAnimNames);
    g_rwStreamClose(stream, &memory);

    const int loadedIndex = g_getAnimationBlockIndex("COOPCS");
    if (loadedIndex < 0) {
        coop_cutscene_mocap::MarkLoadFailed("CAnimManager::LoadAnimFile did not register COOPCS");
        logger->Error("CUTSCENE_MOCAP_LOAD_REJECTED source=%s bytes=%u",
                      coop_cutscene_mocap::GetSourcePath(), static_cast<unsigned>(packSize));
        CutsceneAnimLog("CUTSCENE_MOCAP_LOAD_REJECTED source=%s bytes=%u",
                        coop_cutscene_mocap::GetSourcePath(), static_cast<unsigned>(packSize));
        return;
    }

    void* block = g_getAnimationBlock("COOPCS");
    const bool loaded = block &&
        (*reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(block) + kAnimationBlockLoadedOffset) != 0);
    if (!loaded) {
        coop_cutscene_mocap::MarkLoadFailed("COOPCS was registered but its animation block is not marked loaded");
        logger->Error("CUTSCENE_MOCAP_NOT_READY block=%p index=%d", block, loadedIndex);
        CutsceneAnimLog("CUTSCENE_MOCAP_NOT_READY block=%p index=%d", block, loadedIndex);
        return;
    }

    coop_cutscene_mocap::MarkLoaded();
    logger->Info("CUTSCENE_MOCAP_LOADED source=%s standing=%u walking=%u bytes=%u block=%d",
                 coop_cutscene_mocap::GetSourcePath(),
                 static_cast<unsigned>(coop_cutscene_mocap::GetStandingClipCount()),
                 static_cast<unsigned>(coop_cutscene_mocap::GetWalkClipCount()),
                 static_cast<unsigned>(packSize), loadedIndex);
    CutsceneAnimLog("CUTSCENE_MOCAP_LOADED source=%s standing=%u walking=%u bytes=%u block=%d",
                    coop_cutscene_mocap::GetSourcePath(),
                    static_cast<unsigned>(coop_cutscene_mocap::GetStandingClipCount()),
                    static_cast<unsigned>(coop_cutscene_mocap::GetWalkClipCount()),
                    static_cast<unsigned>(packSize), loadedIndex);
}

bool IsLocalCutsceneRunning()
{
    return g_cutsceneManagerSymbolsReady && g_cutsceneManagerRunning && *g_cutsceneManagerRunning;
}

CEntity* FindLocalCutsceneCj()
{
    if (!IsLocalCutsceneRunning() || !g_cutsceneManagerObjects || !g_cutsceneManagerObjectCount)
        return nullptr;

    const int32_t count = std::max<int32_t>(0, std::min<int32_t>(*g_cutsceneManagerObjectCount, 50));
    for (int32_t i = 0; i < count; ++i) {
        CEntity* object = g_cutsceneManagerObjects[i];
        if (object && object->m_nModelIndex == 1)
            return object;
    }
    return nullptr;
}

void ReleaseRemoteCutscenePartner(uint16_t id, const char* reason)
{
    auto held = g_heldRemoteCutscenePartners.find(id);
    if (held == g_heldRemoteCutscenePartners.end()) return;

    auto pedIt = g_remotePeds.find(id);
    auto refIt = g_remotePedRefs.find(id);
    CPed* ped = pedIt != g_remotePeds.end() ? pedIt->second : nullptr;
    const int pedRef = refIt != g_remotePedRefs.end() ? static_cast<int>(refIt->second) : 0;
    const bool handleValid = ped && pedRef > 0 && CPools::GetPed(pedRef) == ped;
    if (handleValid && g_saUtils) {
        const auto runtime = g_cutscenePartnerRuntime.find(id);
        if (runtime != g_cutscenePartnerRuntime.end() && runtime->second.animationStage > 0)
            g_saUtils->ScriptCommand(&kScmClearCharTasksImmediately, pedRef);
        g_saUtils->ScriptCommand(&kScmFreezeCharPosition, pedRef, 0);
        ped->bIsVisible = true;
    }
    g_cutscenePartnerRuntime.erase(id);

    auto visualIt = g_remoteVisuals.find(id);
    if (visualIt != g_remoteVisuals.end()) {
        visualIt->second.animationInitialized = false;
        visualIt->second.animationAssociationCount = 0;
        visualIt->second.weapon = 0xFF;
        visualIt->second.latestAimProfileValid = false;
        visualIt->second.lastAimTaskMs = 0;
        visualIt->second.shootingFrames = 0;
        visualIt->second.fireAnimFrames = 0;
    }

    g_heldRemoteCutscenePartners.erase(held);
    logger->Info("CUTSCENE_PARTNER_RELEASE id=%u valid=%d reason=%s",
                 static_cast<unsigned>(id), handleValid ? 1 : 0, reason ? reason : "unspecified");
}

bool EnsureCutsceneAnimationBlockReady(const char* blockName);

bool ClearCutscenePartnerInheritedAnimation(uint16_t id, CPed* ped)
{
    bool associationsIsolated = false;
    bool baseAssociationInstalled = false;
    unsigned inheritedAssociationsFaded = 0;

    if (ped && ped->m_pRwClump && g_getAnimationBlock && g_getAnimation &&
        g_blendAnimationByHierarchy && g_getFirstAssociation && g_getNextAssociation &&
        EnsureCutsceneAnimationBlockReady("DEALER")) {
        void* dealerBlock = g_getAnimationBlock("DEALER");
        void* idleHierarchy = dealerBlock ? g_getAnimation("DEALER_IDLE", dealerBlock) : nullptr;
        if (idleHierarchy) {
            void* idleAssociation = g_blendAnimationByHierarchy(
                ped->m_pRwClump, idleHierarchy, static_cast<int>(kAnimAssocFlagsStartedLooped), kAssocFadeRate);
            if (idleAssociation) {
                const uintptr_t idleAddress = reinterpret_cast<uintptr_t>(idleAssociation);
                *reinterpret_cast<uint16_t*>(idleAddress + kAssocFlags) |= kAnimAssocFlagsStartedLooped;
                *reinterpret_cast<float*>(idleAddress + kAssocCurrentTime) = 0.0f;
                *reinterpret_cast<float*>(idleAddress + kAssocBlend) = 1.0f;
                *reinterpret_cast<float*>(idleAddress + kAssocBlendDelta) = kAssocFadeRate;

                for (void* association = g_getFirstAssociation(ped->m_pRwClump);
                     association;) {
                    void* next = g_getNextAssociation(association);
                    if (association != idleAssociation) {
                        const uintptr_t address = reinterpret_cast<uintptr_t>(association);
                        *reinterpret_cast<float*>(address + kAssocBlend) = 0.0f;
                        *reinterpret_cast<float*>(address + kAssocBlendDelta) = -1000.0f;
                        *reinterpret_cast<uint16_t*>(address + kAssocFlags) |= kAnimAssocFlagRetireWhenFaded;
                        ++inheritedAssociationsFaded;
                    }
                    association = next;
                }
                baseAssociationInstalled = true;
                associationsIsolated = true;
            }
        }
    }

    auto visual = g_remoteVisuals.find(id);
    if (visual != g_remoteVisuals.end()) {
        visual->second.animationInitialized = false;
        visual->second.animationAssociationCount = 0;
        visual->second.latestAimProfileValid = false;
        visual->second.lastAimTaskMs = 0;
        visual->second.shootingFrames = 0;
        visual->second.fireAnimFrames = 0;
    }

    if (baseAssociationInstalled) {
        logger->Info("CUTSCENE_PARTNER_ANIM_ISOLATE id=%u base=DEALER/DEALER_IDLE isolated=%d installed=1 faded=%u clump=%p",
                     static_cast<unsigned>(id), associationsIsolated ? 1 : 0,
                     inheritedAssociationsFaded,
                     ped ? static_cast<void*>(ped->m_pRwClump) : nullptr);
        CutsceneAnimLog("CUTSCENE_PARTNER_ANIM_ISOLATE id=%u base=DEALER/DEALER_IDLE isolated=%d installed=1 faded=%u",
                        static_cast<unsigned>(id), associationsIsolated ? 1 : 0,
                        inheritedAssociationsFaded);
    }
    return baseAssociationInstalled;
}

void ReleaseAllRemoteCutscenePartners(const char* reason)
{
    while (!g_heldRemoteCutscenePartners.empty())
        ReleaseRemoteCutscenePartner(*g_heldRemoteCutscenePartners.begin(), reason);
}

bool ResolveCutscenePartnerGround(float x, float y, float actorZ, float& outGroundZ)
{
    if (!g_findGroundZFor3DCoord) return false;
    bool found = false;
    CEntity* groundEntity = nullptr;
    const float groundZ = g_findGroundZFor3DCoord(x, y, actorZ + 6.0f, &found, &groundEntity);
    if (!found || !std::isfinite(groundZ) || std::fabs(groundZ - actorZ) > 12.0f)
        return false;
    outGroundZ = groundZ;
    return true;
}

bool CutscenePartnerSpotChanged(const CutscenePartnerRuntime& previous,
                                const coop_cutscene::PartnerSpot& next)
{
    return coop_cutscene::NeedsReposition(previous.x, previous.y, previous.z, previous.heading,
                                          next.x, next.y, next.z, next.heading,
                                          previous.hasPlacement);
}

bool UpdateCutsceneShotState(CEntity* cutsceneCj, uint64_t nowMs)
{
    if (!cutsceneCj) return false;
    char currentName[9]{};
    if (g_cutsceneManagerName) std::memcpy(currentName, g_cutsceneManagerName, 8);
    if (std::strncmp(currentName, g_cutsceneShotName, 8) != 0) {
        std::memcpy(g_cutsceneShotName, currentName, sizeof(g_cutsceneShotName));
        g_cutsceneShotTracker = coop_cutscene::ShotTracker{};
        g_cutsceneActorMotion = coop_cutscene::ActorMotionTracker{};
        g_cutsceneSceneStartMs = nowMs;
    }

    const CCam& activeCam = TheCamera.GetActiveCam();
    const CVector& source = activeCam.m_vecSource;
    const CVector& forward = activeCam.m_vecFront;
    const CVector& cj = cutsceneCj->GetPosition();
    const bool changed = coop_cutscene::UpdateShotTracker(
        g_cutsceneShotTracker,
        {source.x, source.y, source.z},
        {forward.x, forward.y, forward.z},
        {cj.x, cj.y, cj.z}, nowMs);
    if (changed) {
        const CVector& actorRight = cutsceneCj->GetRightDirection();
        const coop_cutscene::Vec3 cameraRight = coop_cutscene::CameraScreenRight(
            {forward.x, forward.y, forward.z},
            {actorRight.x, actorRight.y, actorRight.z});
        g_cutsceneShotRight = CVector(cameraRight.x, cameraRight.y, cameraRight.z);
        logger->Info("CUTSCENE_PARTNER_SHOT scene=%s shot=%u camera=(%.1f,%.1f,%.1f) cj=(%.1f,%.1f,%.1f) screenRight=(%.2f,%.2f)",
                     currentName[0] ? currentName : "unknown", g_cutsceneShotTracker.shotIndex,
                     source.x, source.y, source.z, cj.x, cj.y, cj.z,
                     g_cutsceneShotRight.x, g_cutsceneShotRight.y);
        CutsceneAnimLog("CUTSCENE_PARTNER_SHOT scene=%s shot=%u cj=(%.1f,%.1f,%.1f)",
                        currentName[0] ? currentName : "unknown", g_cutsceneShotTracker.shotIndex,
                        cj.x, cj.y, cj.z);
    }
    return changed;
}

CEntity* FindNearestCutsceneActor(CEntity* cutsceneCj, const CVector& from)
{
    if (!g_cutsceneManagerObjects || !g_cutsceneManagerObjectCount) return nullptr;
    const int32_t count = std::max<int32_t>(0, std::min<int32_t>(*g_cutsceneManagerObjectCount, 50));
    CEntity* best = nullptr;
    float bestDistance2 = 36.0f;
    for (int32_t i = 0; i < count; ++i) {
        CEntity* object = g_cutsceneManagerObjects[i];
        if (!object || object == cutsceneCj || !object->IsPed()) continue;
        const CVector& pos = object->GetPosition();
        const float dx = pos.x - from.x;
        const float dy = pos.y - from.y;
        const float dz = pos.z - from.z;
        const float distance2 = dx * dx + dy * dy + dz * dz;
        if (std::isfinite(distance2) && distance2 < bestDistance2) {
            bestDistance2 = distance2;
            best = object;
        }
    }
    return best;
}

bool EnsureCutsceneAnimationBlockReady(const char* blockName)
{
    if (!blockName || !blockName[0] || std::strcmp(blockName, "PED") == 0) return true;
    if (!g_getAnimationBlockIndex || !g_getAnimationBlock || !CStreaming::IsInitialised()) return false;

    const int blockIndex = g_getAnimationBlockIndex(blockName);
    if (blockIndex < 0 || blockIndex > 1024) return false;
    void* block = g_getAnimationBlock(blockName);
    const bool loaded = block &&
        (*reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(block) + kAnimationBlockLoadedOffset) != 0);
    if (loaded) {
        g_animationBlockNextRequestMs.erase(blockIndex);
        return true;
    }

    if (g_cutsceneManagerProcessing && *g_cutsceneManagerProcessing) return false;

    const uint64_t nowMs = SteadyNowMs();
    uint64_t& nextRequest = g_animationBlockNextRequestMs[blockIndex];
    if (nowMs >= nextRequest) {
        const int streamingModel = kAnimationStreamingModelBase + blockIndex;
        if (streamingModel >= kAnimationStreamingModelBase && streamingModel < 30000) {
            CStreaming::RequestModel(streamingModel, 2);
            nextRequest = nowMs + 500;
            static std::unordered_set<int> loggedRequests;
            if (loggedRequests.insert(blockIndex).second) {
                logger->Info("CUTSCENE_PARTNER_ANIM_REQUEST block=%s index=%d model=%d",
                             blockName, blockIndex, streamingModel);
                CutsceneAnimLog("CUTSCENE_PARTNER_ANIM_REQUEST block=%s index=%d model=%d",
                                blockName, blockIndex, streamingModel);
            }
        }
    }
    return false;
}

bool StartCutscenePartnerAnimation(uint16_t playerId, int pedRef,
                                   CutscenePartnerRuntime& runtime,
                                   const char* animation, const char* block,
                                   bool loop, bool fidget, uint64_t nowMs,
                                   uint32_t durationMs = 0, float animationSpeed = 7.0f,
                                   bool holdLastFrame = false)
{
    if (!EnsureCutsceneAnimationBlockReady(block)) {
        static std::unordered_set<std::string> loggedWaits;
        const std::string key = std::string(block ? block : "?") + "/" + (animation ? animation : "?");
        if (loggedWaits.insert(key).second) {
            logger->Print(LogP_Warn, "CUTSCENE_PARTNER_ANIM_WAIT id=%u anim=%s/%s (block not ready)",
                          static_cast<unsigned>(playerId), block ? block : "?", animation ? animation : "?");
            CutsceneAnimLog("CUTSCENE_PARTNER_ANIM_WAIT id=%u anim=%s/%s reason=block-not-ready",
                            static_cast<unsigned>(playerId), block ? block : "?", animation ? animation : "?");
        }
        return false;
    }
    const int commandResult = g_saUtils->ScriptCommand(&kScmTaskPlayAnim, pedRef,
        animation, block, animationSpeed, loop ? 1 : 0, 0, 0, holdLastFrame ? 1 : 0, -1);

    runtime.activeAnimation = animation;
    runtime.activeAnimationConfirmed = false;
    logger->Info("CUTSCENE_PARTNER_ANIM_TASK_ISSUED id=%u anim=%s/%s loop=%d result=%d",
                 static_cast<unsigned>(playerId), block, animation, loop ? 1 : 0, commandResult);
    CutsceneAnimLog("CUTSCENE_PARTNER_ANIM_TASK_ISSUED id=%u anim=%s/%s loop=%d result=%d",
                    static_cast<unsigned>(playerId), block, animation, loop ? 1 : 0, commandResult);
    runtime.activeAnimationBlock = block;
    runtime.animationStage = 2;
    runtime.fidgetActive = fidget;
    runtime.lastAnimationCheckMs = nowMs;
    if (fidget) {
        runtime.nextFidgetAtMs = nowMs + durationMs + 3500ULL;
        logger->Info("CUTSCENE_PARTNER_REACTION id=%u anim=%s/%s duration=%u",
                     static_cast<unsigned>(playerId), block, animation, durationMs);
        CutsceneAnimLog("CUTSCENE_PARTNER_REACTION id=%u anim=%s/%s duration=%u",
                        static_cast<unsigned>(playerId), block, animation, durationMs);
    } else if (runtime.lastSceneAction.empty() && std::strcmp(animation, "WALK_player") != 0 &&
               runtime.nextFidgetAtMs == 0) {
        runtime.nextFidgetAtMs = nowMs + 4200ULL + (playerId % 7u) * 280ULL;
        logger->Info("CUTSCENE_PARTNER_IDLE id=%u anim=%s/%s",
                     static_cast<unsigned>(playerId), block, animation);
        CutsceneAnimLog("CUTSCENE_PARTNER_IDLE id=%u anim=%s/%s",
                        static_cast<unsigned>(playerId), block, animation);
    }
    return true;
}

uint32_t CutscenePartnerSeed(uint16_t playerId)
{
    uint32_t seed = 2166136261u ^ (static_cast<uint32_t>(playerId) * 0x9E3779B9u);
    for (unsigned i = 0; i < 8 && g_cutsceneShotName[i]; ++i) {
        seed ^= static_cast<uint8_t>(g_cutsceneShotName[i]);
        seed *= 16777619u;
    }
    return seed;
}

bool StartCutscenePartnerMocapClip(uint16_t playerId, int pedRef,
                                   CutscenePartnerRuntime& runtime, uint64_t nowMs,
                                   uint32_t clipDurationMs = 4200)
{
    const size_t count = coop_cutscene_mocap::GetStandingClipCount();
    if (count < 2 || !EnsureCutsceneAnimationBlockReady("COOPCS")) return false;

    const uint32_t sequence = ++runtime.mocapClipSequence;
    const uint32_t random = CutscenePartnerSeed(playerId) ^ (sequence * 0x9E3779B9u) ^
                            (runtime.lastShotIndex * 0x85EBCA6Bu);
    size_t index = static_cast<size_t>(random % count);
    if (count > 1 && index == runtime.lastMocapClipIndex) index = (index + 1u) % count;
    const char* animation = coop_cutscene_mocap::GetStandingClip(index);
    if (!animation || !StartCutscenePartnerAnimation(playerId, pedRef, runtime,
            animation, "COOPCS", false, false, nowMs, 0, 1.25f, true))
        return false;

    runtime.lastMocapClipIndex = index;
    runtime.nextFidgetAtMs = nowMs + clipDurationMs;
    logger->Info("CUTSCENE_PARTNER_MOCAP_CLIP id=%u anim=%s/COOPCS index=%u/%u",
                 static_cast<unsigned>(playerId), animation,
                 static_cast<unsigned>(index), static_cast<unsigned>(count));
    CutsceneAnimLog("CUTSCENE_PARTNER_MOCAP_CLIP id=%u anim=%s/COOPCS index=%u/%u",
                    static_cast<unsigned>(playerId), animation,
                    static_cast<unsigned>(index), static_cast<unsigned>(count));
    return true;
}

bool StartCutscenePartnerIdle(uint16_t playerId, int pedRef,
                              CutscenePartnerRuntime& runtime, uint64_t nowMs)
{
    if (!EnsureCutsceneAnimationBlockReady("DEALER") || !g_getAnimationBlock || !g_getAnimation)
        return false;
    void* dealerBlock = g_getAnimationBlock("DEALER");
    if (!dealerBlock || !g_getAnimation("DEALER_IDLE", dealerBlock)) {
        static bool loggedMissingDealerIdle = false;
        if (!loggedMissingDealerIdle) {
            loggedMissingDealerIdle = true;
            logger->Print(LogP_Warn, "CUTSCENE_PARTNER_DEALER_IDLE_MISSING block=DEALER anim=DEALER_IDLE");
            CutsceneAnimLog("CUTSCENE_PARTNER_DEALER_IDLE_MISSING block=DEALER anim=DEALER_IDLE");
        }
        return false;
    }
    return StartCutscenePartnerAnimation(playerId, pedRef, runtime,
        "DEALER_IDLE", "DEALER", true, false, nowMs, 0, 4.1f);
}

bool StartCutscenePartnerFidget(uint16_t playerId, int pedRef,
                                CutscenePartnerRuntime& runtime, uint64_t nowMs,
                                bool hasNearbyActor, unsigned int shotIndex)
{
    if (StartCutscenePartnerMocapClip(playerId, pedRef, runtime, nowMs, 4200)) return true;

    struct Fidget {
        const char* animation;
        const char* block;
        uint32_t durationMs;
    };
    static constexpr Fidget dealerFidgets[] = {
        {"DEALER_IDLE_01", "DEALER", 2400},
        {"DEALER_IDLE_02", "DEALER", 2400},
        {"DEALER_IDLE_03", "DEALER", 2400},
        {"Coplook_nod", "COP_AMBIENT", 1900},
        {"shift", "PLAYIDLES", 2100},
        {"XPRESSscratch", "PED", 2500}
    };
    static constexpr Fidget listenFidgets[] = {
        {"Coplook_nod", "COP_AMBIENT", 1900},
        {"Coplook_think", "COP_AMBIENT", 2400},
        {"shift", "PLAYIDLES", 2100},
        {"shldr", "PLAYIDLES", 1900},
        {"Coplook_nod", "COP_AMBIENT", 1900},
        {"strleg", "PLAYIDLES", 2300},
        {"XPRESSscratch", "PED", 2500},
        {"time", "PLAYIDLES", 2600},
        {"Coplook_shake", "COP_AMBIENT", 2000},
        {"roadcross_gang", "PED", 2600}
    };

    const bool dealerProfile = runtime.idleProfile == 0;
    const Fidget* fidgets = dealerProfile ? dealerFidgets : listenFidgets;
    const unsigned count = dealerProfile
        ? static_cast<unsigned>(sizeof(dealerFidgets) / sizeof(dealerFidgets[0]))
        : static_cast<unsigned>(sizeof(listenFidgets) / sizeof(listenFidgets[0]));

    const unsigned int seed = static_cast<unsigned int>(playerId) * 0x9E3779B9u ^
                              shotIndex * 0x85EBCA6Bu ^
                              static_cast<unsigned int>(runtime.fidgetSequence) * 0xC2B2AE35u;
    unsigned int start = seed % count;
    if (hasNearbyActor && !dealerProfile) start = seed % 2u;
    if (!hasNearbyActor && !dealerProfile) start = 2u + (seed % (count - 2u));
    for (unsigned int attempt = 0; attempt < count; ++attempt) {
        const unsigned int index = (start + attempt) % count;
        const Fidget& fidget = fidgets[index];
        if (StartCutscenePartnerAnimation(playerId, pedRef, runtime,
                fidget.animation, fidget.block, false, true, nowMs,
                fidget.durationMs, 4.0f)) {
            ++runtime.fidgetSequence;
            return true;
        }
    }
    return false;
}

struct CutsceneSceneAction {
    const char* scene;
    float fromSeconds;
    float toSeconds;
    const char* animation;
    const char* block;
    bool loop;
};

static const CutsceneSceneAction kCutsceneSceneActions[] = {
    {"CAT_3", 11.8f, 14.2f, "handscower", "PED", false},
    {"W2_ALT", 0.0f, 1000000.0f, "Coplook_loop", "COP_AMBIENT", true}
};

const CutsceneSceneAction* FindCutsceneSceneAction(float sceneSeconds)
{
    for (const CutsceneSceneAction& action : kCutsceneSceneActions) {
        if (g_cutsceneManagerName && std::strncmp(g_cutsceneManagerName, action.scene, 8) == 0 &&
            sceneSeconds >= action.fromSeconds && sceneSeconds < action.toSeconds)
            return &action;
    }
    return nullptr;
}

void UpdateCutscenePartners(CEntity* cutsceneCj)
{
    if (!cutsceneCj || !g_client || !g_saUtils) return;

    struct Candidate {
        uint16_t id;
        CPed* ped;
        uint32_t pedRef;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(g_client->GetRemotePlayers().size());

    for (const auto& player : g_client->GetRemotePlayers()) {
        if (player.health == 0 || (player.animation.flags & ANIM_DEAD) || player.vehicleId != 0) {
            ReleaseRemoteCutscenePartner(player.id, player.vehicleId ? "remote player entered vehicle" : "remote player unavailable");
            continue;
        }

        CPed* ped = LiveRemotePed(player.id);
        if (!ped || ped->m_pVehicle) {
            ReleaseRemoteCutscenePartner(player.id, "remote ped not on foot");
            continue;
        }
        auto refIt = g_remotePedRefs.find(player.id);
        if (refIt == g_remotePedRefs.end() || !refIt->second || CPools::GetPed(refIt->second) != ped) {
            ReleaseRemoteCutscenePartner(player.id, "remote ped handle invalid");
            continue;
        }
        candidates.push_back({player.id, ped, static_cast<uint32_t>(refIt->second)});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.id < b.id;
    });

    const CVector origin = cutsceneCj->GetPosition();
    const CVector actorRight = cutsceneCj->GetRightDirection();
    const CVector actorForward = cutsceneCj->GetTopDirection();
    const uint64_t nowMs = SteadyNowMs();
    UpdateCutsceneShotState(cutsceneCj, nowMs);
    const bool actorMoving = coop_cutscene::UpdateActorMotion(g_cutsceneActorMotion,
        {origin.x, origin.y, origin.z}, nowMs, false);
    const unsigned int shotIndex = g_cutsceneShotTracker.shotIndex;
    const int cutsceneTimeMs = g_getCutsceneTimeMs ? g_getCutsceneTimeMs() :
        (g_cutsceneSceneStartMs && nowMs >= g_cutsceneSceneStartMs
            ? static_cast<int>(nowMs - g_cutsceneSceneStartMs) : 0);
    const float sceneSeconds = std::max(0, cutsceneTimeMs) / 1000.0f;
    const CutsceneSceneAction* sceneAction = actorMoving ? nullptr : FindCutsceneSceneAction(sceneSeconds);

    CVector placementRight = actorRight;
    const float shotRightLength2 = g_cutsceneShotRight.x * g_cutsceneShotRight.x +
                                   g_cutsceneShotRight.y * g_cutsceneShotRight.y;
    if (std::isfinite(shotRightLength2) && shotRightLength2 > 0.5f)
        placementRight = g_cutsceneShotRight;

    const coop_cutscene::Vec3 walkingForward = actorMoving
        ? g_cutsceneActorMotion.direction
        : coop_cutscene::Vec3{actorForward.x, actorForward.y, 0.0f};

    for (size_t rank = 0; rank < candidates.size(); ++rank) {
        const Candidate& candidate = candidates[rank];
        CPed* ped = candidate.ped;
        const int pedRef = static_cast<int>(candidate.pedRef);
        auto runtimeIt = g_cutscenePartnerRuntime.find(candidate.id);
        if (runtimeIt == g_cutscenePartnerRuntime.end())
            runtimeIt = g_cutscenePartnerRuntime.emplace(candidate.id, CutscenePartnerRuntime{}).first;
        CutscenePartnerRuntime& runtime = runtimeIt->second;
        const bool partnerAlreadyHeld =
            g_heldRemoteCutscenePartners.find(candidate.id) != g_heldRemoteCutscenePartners.end();

        if (runtime.sceneName != g_cutsceneShotName) {
            if (runtime.animationStage > 0)
                g_saUtils->ScriptCommand(&kScmClearCharTasksImmediately, pedRef);
            runtime.baseAnimationInstalled = false;
            runtime.sceneName = g_cutsceneShotName;
            runtime.hasPlacement = false;
            runtime.hasShot = false;
            runtime.hasFacingHeading = false;
            runtime.lastShotIndex = 0;
            runtime.shotReactionPending = false;
            runtime.fidgetActive = false;
            runtime.sceneActionPlayed = false;
            runtime.activeAnimationConfirmed = false;
            runtime.lastSceneAction.clear();
            runtime.idleProfile = 0xFF;
            runtime.animationStage = 0;
            runtime.activeAnimation.clear();
            runtime.activeAnimationBlock.clear();
            runtime.lastAnimationCheckMs = 0;
            runtime.nextFidgetAtMs = 0;
            runtime.nextWalkSpeedAtMs = 0;
            runtime.activeWalkBaseSpeed = 0.0f;
            runtime.lastMocapClipIndex = static_cast<size_t>(-1);
            runtime.activeWalkClipIndex = static_cast<size_t>(-1);
            runtime.mocapClipSequence = 0;
        }

        if (!partnerAlreadyHeld) {
            g_saUtils->ScriptCommand(&kScmClearCharTasksImmediately, pedRef);
            runtime.baseAnimationInstalled = false;
            g_saUtils->ScriptCommand(&kScmSetCurrentCharWeapon, pedRef, static_cast<int>(WEAPONTYPE_UNARMED));
            if (g_setCurrentWeapon) g_setCurrentWeapon(ped, WEAPONTYPE_UNARMED);
            g_saUtils->ScriptCommand(&kScmFreezeCharPosition, pedRef, 1);

            auto visual = g_remoteVisuals.find(candidate.id);
            if (visual != g_remoteVisuals.end()) {
                visual->second.weapon = static_cast<uint8_t>(WEAPONTYPE_UNARMED);
                visual->second.animationAssociationCount = 0;
                visual->second.latestAimProfileValid = false;
                visual->second.lastAimTaskMs = 0;
            }
            g_heldRemoteCutscenePartners.insert(candidate.id);
            logger->Info("CUTSCENE_PARTNER_HOLD id=%u rank=%u model=%u",
                         static_cast<unsigned>(candidate.id), static_cast<unsigned>(rank),
                         static_cast<unsigned>(ped->m_nModelIndex));
        }

        if (!runtime.baseAnimationInstalled)
            runtime.baseAnimationInstalled = ClearCutscenePartnerInheritedAnimation(candidate.id, ped);

        coop_cutscene::PartnerSpot spot{};
        if (actorMoving) {
            spot = coop_cutscene::MakeWalkingPartnerSpot(
                {origin.x, origin.y, origin.z}, walkingForward, rank);
        } else {
            spot = coop_cutscene::MakePartnerSpot(
                {origin.x, origin.y, origin.z},
                {placementRight.x, placementRight.y, placementRight.z},
                {actorForward.x, actorForward.y, actorForward.z}, rank);
        }

        float floorZ = origin.z - 1.0f;
        if (ResolveCutscenePartnerGround(spot.x, spot.y, origin.z, floorZ)) {
            spot.z = floorZ;
        } else {
            spot.z = floorZ;
            static bool loggedGroundFallback = false;
            if (!loggedGroundFallback) {
                loggedGroundFallback = true;
                logger->Print(LogP_Warn, "CUTSCENE_PARTNER_GROUND_QUERY_UNAVAILABLE; using actorZ-1 fallback");
            }
        }

        CEntity* nearbyActor = FindNearestCutsceneActor(cutsceneCj,
            CVector(spot.x, spot.y, spot.z));
        const CVector& lookTarget = nearbyActor ? nearbyActor->GetPosition() : origin;
        const bool firstShotForPed = !runtime.hasShot;
        const bool newShotForPed = firstShotForPed || runtime.lastShotIndex != shotIndex;
        if (newShotForPed) {
            const bool sceneAlreadyPlaying = runtime.hasShot;
            runtime.lastShotIndex = shotIndex;
            runtime.hasShot = true;
            runtime.facingHeading = actorMoving ? spot.heading : coop_cutscene::HeadingToward(
                {spot.x, spot.y, spot.z},
                {lookTarget.x, lookTarget.y, lookTarget.z}, spot.heading);
            runtime.hasFacingHeading = true;
            if (sceneAlreadyPlaying && !actorMoving) {
                if (runtime.animationStage > 0)
                    g_saUtils->ScriptCommand(&kScmClearCharTasksImmediately, pedRef);
                runtime.animationStage = 0;
                runtime.activeAnimation.clear();
                runtime.activeAnimationBlock.clear();
                runtime.fidgetActive = false;
                runtime.lastSceneAction.clear();
                runtime.sceneActionPlayed = false;
                runtime.shotReactionPending = true;
            } else if (sceneAlreadyPlaying && actorMoving) {
                runtime.shotReactionPending = false;
            }
            if (!actorMoving) {
                float shotHeadingDegrees = runtime.facingHeading * 57.29577951308232f;
                if (shotHeadingDegrees < 0.0f) shotHeadingDegrees += 360.0f;
                g_saUtils->ScriptCommand(&kScmSetCharHeading, pedRef, shotHeadingDegrees);
            }
            logger->Info("CUTSCENE_PARTNER_LOOK id=%u shot=%u target=%s heading=%.1f",
                         static_cast<unsigned>(candidate.id), shotIndex,
                         actorMoving ? "walking" : (nearbyActor ? "story-actor" : "CJ"),
                         runtime.facingHeading * 57.29577951308232f);
        }

        ped->m_nAreaCode = cutsceneCj->m_nAreaCode;
        ped->bIsVisible = true;
        auto* physical = reinterpret_cast<CPhysical*>(ped);
        physical->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
        physical->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);

        if (actorMoving) {
            if (!runtime.lastSceneAction.empty()) {
                if (runtime.animationStage > 0)
                    g_saUtils->ScriptCommand(&kScmClearCharTasksImmediately, pedRef);
                runtime.lastSceneAction.clear();
                runtime.sceneActionPlayed = false;
                runtime.animationStage = 0;
                runtime.activeAnimation.clear();
                runtime.activeAnimationBlock.clear();
            }
            g_saUtils->SetPosition(physical, spot.x, spot.y, spot.z);
            g_saUtils->SetAngle(physical, static_cast<unsigned char>(2), spot.heading);
            ped->m_fCurrentRotation = spot.heading;
            ped->m_fAimingRotation = spot.heading;
            runtime.x = spot.x;
            runtime.y = spot.y;
            runtime.z = spot.z;
            runtime.heading = spot.heading;
            runtime.hasPlacement = true;
            runtime.facingHeading = spot.heading;
            runtime.hasFacingHeading = true;

            const size_t mocapWalkCount = coop_cutscene_mocap::GetWalkClipCount();
            const size_t mocapGait = mocapWalkCount ? (2u % mocapWalkCount) : 0u;
            const char* walkAnimation = mocapWalkCount
                ? coop_cutscene_mocap::GetWalkClip(mocapGait) : "WALK_player";
            const char* walkBlock = mocapWalkCount ? "COOPCS" : "PED";
            const float walkBaseSpeed = mocapWalkCount
                ? coop_cutscene_mocap::GetWalkSpeed(mocapGait) : 0.0f;
            const bool wantMocapWalk = mocapWalkCount > 0 && coop_cutscene_mocap::GetStandingClipCount() > 1;
            if (!wantMocapWalk) {
                walkAnimation = "WALK_player";
                walkBlock = "PED";
            }
            if (runtime.baseAnimationInstalled &&
                (runtime.animationStage != 4 || runtime.activeAnimation != walkAnimation ||
                 runtime.activeAnimationBlock != walkBlock)) {
                if (runtime.animationStage > 0)
                    g_saUtils->ScriptCommand(&kScmClearCharTasksImmediately, pedRef);
                runtime.animationStage = 0;
                runtime.activeAnimation.clear();
                runtime.activeAnimationBlock.clear();
                runtime.activeWalkBaseSpeed = walkBaseSpeed;
                runtime.activeWalkClipIndex = wantMocapWalk ? mocapGait : static_cast<size_t>(-1);
                runtime.nextWalkSpeedAtMs = 0;
                if (StartCutscenePartnerAnimation(candidate.id, pedRef, runtime,
                        walkAnimation, walkBlock, true, false, nowMs, 0, 4.0f)) {
                    runtime.animationStage = 4;
                    logger->Info("CUTSCENE_PARTNER_WALK id=%u anim=%s/%s actorSpeed=%.2f clipSpeed=%.2f direction=(%.2f,%.2f)",
                                 static_cast<unsigned>(candidate.id), walkBlock, walkAnimation,
                                 g_cutsceneActorMotion.speed, walkBaseSpeed,
                                 walkingForward.x, walkingForward.y);
                    CutsceneAnimLog("CUTSCENE_PARTNER_WALK id=%u anim=%s/%s actorSpeed=%.2f clipSpeed=%.2f",
                                    static_cast<unsigned>(candidate.id), walkBlock, walkAnimation,
                                    g_cutsceneActorMotion.speed, walkBaseSpeed);
                }
            }
            if (runtime.animationStage == 4 && runtime.activeWalkBaseSpeed > 0.01f &&
                nowMs >= runtime.nextWalkSpeedAtMs) {
                const float playbackRate = std::clamp(
                    g_cutsceneActorMotion.speed / runtime.activeWalkBaseSpeed, 0.2f, 1.8f);
                g_saUtils->ScriptCommand(&kScmSetCharAnimSpeed, pedRef,
                                         runtime.activeAnimation.c_str(), playbackRate);
                runtime.nextWalkSpeedAtMs = nowMs + 100ULL;
            }
        } else {
            if (runtime.animationStage == 4) {
                g_saUtils->ScriptCommand(&kScmClearCharTasksImmediately, pedRef);
                runtime.animationStage = 0;
                runtime.activeAnimation.clear();
                runtime.activeAnimationBlock.clear();
                runtime.activeWalkBaseSpeed = 0.0f;
                runtime.activeWalkClipIndex = static_cast<size_t>(-1);
                runtime.nextWalkSpeedAtMs = 0;
                runtime.lastAnimationCheckMs = 0;
                runtime.fidgetActive = false;
                runtime.nextFidgetAtMs = nowMs + 900ULL;
            }

            const float facingHeading = runtime.hasFacingHeading ? runtime.facingHeading : spot.heading;
            ped->m_fCurrentRotation = facingHeading;
            ped->m_fAimingRotation = facingHeading;
            float headingDegrees = facingHeading * 57.29577951308232f;
            if (headingDegrees < 0.0f) headingDegrees += 360.0f;

            const bool moved = CutscenePartnerSpotChanged(runtime, spot);
            if (moved) {
                if (runtime.animationStage > 0)
                    g_saUtils->ScriptCommand(&kScmClearCharTasksImmediately, pedRef);
                g_saUtils->ScriptCommand(&kScmSetCharCoordinates, pedRef, spot.x, spot.y, spot.z);
                g_saUtils->ScriptCommand(&kScmSetCharHeading, pedRef, headingDegrees);
                g_saUtils->ScriptCommand(&kScmFreezeCharPosition, pedRef, 1);
                runtime.x = spot.x;
                runtime.y = spot.y;
                runtime.z = spot.z;
                runtime.heading = spot.heading;
                runtime.hasPlacement = true;
                runtime.animationStage = 0;
                runtime.lastAnimationCheckMs = 0;
                continue;
            }

            if (sceneAction) {
                const std::string actionKey = std::string(sceneAction->scene) + "/" + sceneAction->block + "/" + sceneAction->animation;
                const bool actionBlockReady = runtime.baseAnimationInstalled &&
                    EnsureCutsceneAnimationBlockReady(sceneAction->block);
                if (actionBlockReady && runtime.lastSceneAction != actionKey) {
                    if (runtime.animationStage > 0)
                        g_saUtils->ScriptCommand(&kScmClearCharTasksImmediately, pedRef);
                    runtime.animationStage = 0;
                    runtime.activeAnimation.clear();
                    runtime.activeAnimationBlock.clear();
                    runtime.activeAnimationConfirmed = false;
                    runtime.fidgetActive = false;
                    runtime.sceneActionPlayed = false;
                    runtime.lastSceneAction.clear();
                }
                if (actionBlockReady && runtime.animationStage == 0 &&
                    (sceneAction->loop || !runtime.sceneActionPlayed)) {
                    if (StartCutscenePartnerAnimation(candidate.id, pedRef, runtime,
                            sceneAction->animation, sceneAction->block,
                            sceneAction->loop, false, nowMs, 0, 4.0f)) {
                        runtime.lastSceneAction = actionKey;
                        runtime.sceneActionPlayed = !sceneAction->loop;
                        logger->Info("CUTSCENE_PARTNER_SCENE_ACTION id=%u scene=%s anim=%s/%s time=%.2f",
                                     static_cast<unsigned>(candidate.id), sceneAction->scene,
                                     sceneAction->block, sceneAction->animation, sceneSeconds);
                        CutsceneAnimLog("CUTSCENE_PARTNER_SCENE_ACTION id=%u scene=%s anim=%s/%s time=%.2f",
                                        static_cast<unsigned>(candidate.id), sceneAction->scene,
                                        sceneAction->block, sceneAction->animation, sceneSeconds);
                    }
                }
            } else if (!runtime.lastSceneAction.empty()) {
                if (runtime.animationStage > 0)
                    g_saUtils->ScriptCommand(&kScmClearCharTasksImmediately, pedRef);
                runtime.lastSceneAction.clear();
                runtime.sceneActionPlayed = false;
                runtime.animationStage = 0;
                runtime.activeAnimation.clear();
                runtime.activeAnimationBlock.clear();
                runtime.activeAnimationConfirmed = false;
                runtime.fidgetActive = false;
            }

            const bool sceneActionPending = sceneAction && runtime.lastSceneAction.empty();
            const bool sceneActionOneShotFinished = sceneAction &&
                runtime.lastSceneAction == (std::string(sceneAction->scene) + "/" + sceneAction->block + "/" + sceneAction->animation) &&
                !sceneAction->loop && runtime.sceneActionPlayed;
            if (runtime.baseAnimationInstalled && runtime.animationStage == 0 &&
                (runtime.lastSceneAction.empty() || sceneActionPending || sceneActionOneShotFinished)) {
                const bool shouldReact = runtime.shotReactionPending ||
                    (runtime.nextFidgetAtMs != 0 && nowMs >= runtime.nextFidgetAtMs);
                if (shouldReact) {
                    if (StartCutscenePartnerFidget(candidate.id, pedRef, runtime, nowMs,
                                                   nearbyActor != nullptr, shotIndex)) {
                        runtime.shotReactionPending = false;
                    } else {
                        runtime.shotReactionPending = false;
                        runtime.nextFidgetAtMs = nowMs + 1400ULL;
                        StartCutscenePartnerIdle(candidate.id, pedRef, runtime, nowMs);
                    }
                } else {
                    StartCutscenePartnerIdle(candidate.id, pedRef, runtime, nowMs);
                }
            }
        }

        if ((runtime.animationStage == 2 || runtime.animationStage == 4) &&
            nowMs - runtime.lastAnimationCheckMs >= 650) {
            runtime.lastAnimationCheckMs = nowMs;
            const bool animationPlaying = !runtime.activeAnimation.empty() &&
                g_saUtils->ScriptCommand(&kScmIsCharPlayingAnim, pedRef,
                    runtime.activeAnimation.c_str(), runtime.activeAnimationBlock.c_str());
            if (animationPlaying && !runtime.activeAnimationConfirmed) {
                runtime.activeAnimationConfirmed = true;
                logger->Info("CUTSCENE_PARTNER_ANIM_PLAYING id=%u anim=%s/%s",
                             static_cast<unsigned>(candidate.id),
                             runtime.activeAnimationBlock.c_str(), runtime.activeAnimation.c_str());
                CutsceneAnimLog("CUTSCENE_PARTNER_ANIM_PLAYING id=%u anim=%s/%s",
                                static_cast<unsigned>(candidate.id),
                                runtime.activeAnimationBlock.c_str(), runtime.activeAnimation.c_str());
            }
            if (!animationPlaying) {
                logger->Print(LogP_Warn, "CUTSCENE_PARTNER_ANIM_FINISHED id=%u anim=%s/%s",
                             static_cast<unsigned>(candidate.id),
                             runtime.activeAnimation.c_str(), runtime.activeAnimationBlock.c_str());
                CutsceneAnimLog("CUTSCENE_PARTNER_ANIM_FINISHED id=%u anim=%s/%s",
                                static_cast<unsigned>(candidate.id),
                                runtime.activeAnimationBlock.c_str(), runtime.activeAnimation.c_str());
                runtime.animationStage = 0;
                runtime.fidgetActive = false;
                runtime.activeAnimation.clear();
                runtime.activeAnimationBlock.clear();
                runtime.activeAnimationConfirmed = false;
                if (runtime.nextFidgetAtMs < nowMs + 1200ULL)
                    runtime.nextFidgetAtMs = nowMs + 3500ULL;
            } else if (runtime.animationStage == 2 && !runtime.fidgetActive &&
                       runtime.lastSceneAction.empty() && runtime.nextFidgetAtMs != 0 &&
                       nowMs >= runtime.nextFidgetAtMs) {
                g_saUtils->ScriptCommand(&kScmClearCharTasksImmediately, pedRef);
                runtime.animationStage = 0;
                runtime.activeAnimation.clear();
                runtime.activeAnimationBlock.clear();
                runtime.shotReactionPending = true;
            }
        }
    }
}

void UpdateRemotePlayers()
{
    if (!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;

    const bool cutsceneRunning = IsLocalCutsceneRunning();
    const bool cutsceneProcessing = g_cutsceneManagerProcessing
        ? *g_cutsceneManagerProcessing : cutsceneRunning;
    if (!cutsceneRunning && !cutsceneProcessing)
        EnsureCutsceneAnimationBlockReady("DEALER");
    CEntity* cutsceneCj = cutsceneRunning ? FindLocalCutsceneCj() : nullptr;
    const bool cutsceneSceneActive = cutsceneRunning && cutsceneCj != nullptr;
    if (!cutsceneSceneActive && !g_heldRemoteCutscenePartners.empty())
        ReleaseAllRemoteCutscenePartners(cutsceneRunning
            ? "cutscene actor unavailable" : "local cutscene ended");
    if (!cutsceneSceneActive) {
        g_cutsceneShotTracker = coop_cutscene::ShotTracker{};
        g_cutsceneActorMotion = coop_cutscene::ActorMotionTracker{};
        g_cutsceneSceneStartMs = 0;
        g_cutsceneShotRight = CVector(0.0f, 0.0f, 0.0f);
        g_cutsceneShotName[0] = '\0';
    }
    if (cutsceneRunning != g_cutsceneWasActive) {
        char cutsceneName[9]{};
        if (g_cutsceneManagerName) std::memcpy(cutsceneName, g_cutsceneManagerName, 8);
        logger->Info("CUTSCENE_PARTNER_STATE active=%d actor=%s cutscene=%s",
                     cutsceneRunning ? 1 : 0, cutsceneCj ? "csplay" : "pending",
                     cutsceneName[0] ? cutsceneName : "unknown");
        CutsceneAnimLog("CUTSCENE_PARTNER_STATE active=%d actor=%s cutscene=%s",
                        cutsceneRunning ? 1 : 0, cutsceneCj ? "csplay" : "pending",
                        cutsceneName[0] ? cutsceneName : "unknown");
        g_cutsceneWasActive = cutsceneRunning;
    }

    static uint64_t clothesResumeAtMs = 0;
    const bool cutsceneLoading = g_cutsceneLoadStatus && *g_cutsceneLoadStatus != 0;
    const bool cutsceneBusy = cutsceneRunning || cutsceneProcessing || cutsceneLoading || g_cutsceneWasActive;
    {
        const uint64_t clothesNowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if (cutsceneBusy) clothesResumeAtMs = clothesNowMs + 2000;
    }
    static bool localClothesRefreshedForCutscene = false;
    if (cutsceneBusy) {
        if (!localClothesRefreshedForCutscene) {
            RebuildLocalClothesFromOwnDesc("cutscene start");
            localClothesRefreshedForCutscene = true;
        }
    } else {
        localClothesRefreshedForCutscene = false;
    }
    const bool clothesRebuildBlocked = cutsceneBusy ||
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()) < clothesResumeAtMs;

    static uint64_t lastPlayerHealthReportMs = 0;
    const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    const bool reportPlayerHealth = nowMs - lastPlayerHealthReportMs >= 300;
    if (reportPlayerHealth) lastPlayerHealthReportMs = nowMs;
    CPlayerPed* localAreaPlayer = FindPlayerPed(-1);
    const unsigned localAreaCodeForLog = localAreaPlayer ? localAreaPlayer->m_nAreaCode : 0;
    static bool havePreviousLocalAreaCode = false;
    static uint8_t previousLocalAreaCode = 0;
    const bool localAreaTransition = localAreaPlayer && havePreviousLocalAreaCode &&
        localAreaCodeForLog != previousLocalAreaCode;
    if (localAreaPlayer) {
        previousLocalAreaCode = static_cast<uint8_t>(localAreaCodeForLog);
        havePreviousLocalAreaCode = true;
    }

    UpdateRemoteVehicles();
    UpdateRemoteNpcs();

    for(const auto& vitals:g_client->DrainNpcVitals()){
        if(vitals.ownerId==g_client->LocalId()){
            auto* pool=CPools::ms_pPedPool;
            if(!pool||!vitals.localRef) continue;
            CPlayerPed* localPlayer=FindPlayerPed(-1);
            const int maxPeds=std::min(pool->m_nSize,256);
            for(int i=0;i<maxPeds;++i){
                CPed* ped=pool->GetAt(i);
                if(!ped||ped==localPlayer||
                   static_cast<uint32_t>(CPools::GetPedRef(ped))!=vitals.localRef) continue;
                ApplyRemoteVitals(ped,vitals.health,vitals.armour);
                break;
            }
        }else{
            auto it=g_remoteNpcs.find(vitals.id);
            if(it!=g_remoteNpcs.end()&&it->second && vitals.health > 0)
                ApplyRemoteVitals(it->second,vitals.health,vitals.armour);
        }
    }

    for (const auto& removed : g_client->DrainRemovedPlayers()) {
        ReleaseRemoteCutscenePartner(removed.id, "remote player disconnected");
        auto it = g_remotePeds.find(removed.id);
        if (it != g_remotePeds.end()) {


            if (it->second && it->second->m_pVehicle)
                DiscardRemotePed(it->second, "player removed while seated");
            else
                RetireRemotePed(it->second);
            g_remoteTaskPermitUntilMs.erase(it->second);
            g_remoteCarjackVictimUntilMs.erase(it->second);
    g_remoteCarjackVictimUntilMs.erase(it->second);
            g_remotePeds.erase(it);
            g_remoteVisuals.erase(removed.id);
            logger->Info("Remote player %u despawned", removed.id);
        }
        g_lastRemotePlayerHealth.erase(removed.id);
        g_remoteCarjackAttempts.erase(removed.id);
        g_pendingRemoteVehicleEnters.erase(
            std::remove_if(g_pendingRemoteVehicleEnters.begin(), g_pendingRemoteVehicleEnters.end(),
                [&](const QueuedRemoteVehicleEnter& item) { return item.event.id == removed.id; }),
            g_pendingRemoteVehicleEnters.end());
        auto clothesCache = g_remoteClothesCache.find(removed.id);
        if (clothesCache != g_remoteClothesCache.end()) ReleaseRemoteClothesCacheEntry(clothesCache);
        ReleaseRemoteSpecialLease(removed.id);
    }

    for (const auto& npcDamage : g_client->DrainNpcDamages()) {
        auto* pool = CPools::ms_pPedPool;
        if (!pool || !npcDamage.localRef) continue;
        CPlayerPed* localPlayer = FindPlayerPed(-1);
        const int maxPeds = std::min(pool->m_nSize, 256);
        for (int i = 0; i < maxPeds; ++i) {
            CPed* ped = pool->GetAt(i);
            if (!ped || ped == localPlayer ||
                static_cast<uint32_t>(CPools::GetPedRef(ped)) != npcDamage.localRef)
                continue;
            bool excluded = false;
            for (const auto& remote : g_remotePeds) if (remote.second == ped) { excluded = true; break; }
            if (!excluded) for (const auto& remote : g_remoteNpcs) if (remote.second == ped) { excluded = true; break; }
            if (!excluded) for (CPed* testNpc : g_spawnedNpcs) if (testNpc == ped) { excluded = true; break; }
            if (excluded) continue;
            ApplyRemoteVitals(ped, npcDamage.health, npcDamage.armour);
            WeaponFileLog("NPC_DAMAGE_OWNER_APPLY id=%u ref=%u health=%u armour=%u weapon=%u",
                          npcDamage.id, npcDamage.localRef, npcDamage.health, npcDamage.armour, npcDamage.weapon);
            break;
        }
    }

    for (const auto& damage : g_client->DrainDamages()) {
        if (damage.targetId == g_client->LocalId()) {
            if (auto* local = FindPlayerPed(-1)) {
                ApplyRemoteVitals(local, damage.health, damage.armour);
                logger->Info("DAMAGE_LOCAL weapon=%u health=%u armour=%u", damage.weapon, damage.health, damage.armour);
            }
        } else {
            auto target = g_remotePeds.find(damage.targetId);
            if (target != g_remotePeds.end() && !LiveRemotePed(damage.targetId)) target = g_remotePeds.end();
            if (target != g_remotePeds.end()) ApplyRemoteVitals(target->second, damage.health, damage.armour);
        }
    }
    for (const auto& vitals : g_client->DrainPlayerVitals()) {
        CPed* target = vitals.id == g_client->LocalId()
            ? static_cast<CPed*>(FindPlayerPed(-1))
            : LiveRemotePed(vitals.id);
        if (target) ApplyRemoteVitals(target, vitals.health, vitals.armour);
    }
    for (const auto& explosion : g_client->DrainExplosions()) {
        if (!g_saUtils || explosion.id == g_client->LocalId()) continue;
        g_replayingNativeExplosion = true;
        g_saUtils->ScriptCommand(&kScmAddExplosion, explosion.x, explosion.y, explosion.z,
                                 static_cast<int>(explosion.type));
        g_replayingNativeExplosion = false;
        WeaponFileLog("REMOTE_EXPLOSION_REPLAY id=%u type=%u pos=(%.2f,%.2f,%.2f)",
                      explosion.id, static_cast<unsigned>(explosion.type), explosion.x, explosion.y, explosion.z);
    }
    const auto newShots = g_client->DrainShots();
    for (const auto& shot : newShots) {
        if (g_pendingRemoteShots.size() >= kMaxPendingRemoteShots) g_pendingRemoteShots.pop_front();
        g_pendingRemoteShots.push_back(PendingRemoteShot{shot, SteadyNowMs() + kRemoteShotRetryLifetimeMs});
    }
    for (const auto& enter : g_client->DrainVehicleEnters()) {
        if (!enter.id || enter.id == g_client->LocalId() || !enter.vehicleId || enter.seat > 8) continue;
        auto queued = std::find_if(g_pendingRemoteVehicleEnters.begin(), g_pendingRemoteVehicleEnters.end(),
            [&](const QueuedRemoteVehicleEnter& item) { return item.event.id == enter.id; });
        if (queued == g_pendingRemoteVehicleEnters.end())
            g_pendingRemoteVehicleEnters.push_back(QueuedRemoteVehicleEnter{enter, NowSteadyMs()});
        else {
            queued->event = enter;
            queued->receivedAtMs = NowSteadyMs();
        }
    }

    for (const auto& player : g_client->GetRemotePlayers()) {
        uint16_t remoteModelId = player.modelId;
        char normalizedSpecialName[coop_special::kNameCapacity]{};
        const bool hasSpecialName = coop_special::NormalizeName(
            player.specialModelName, normalizedSpecialName, sizeof(normalizedSpecialName));
        std::string targetSpecialName = hasSpecialName ? normalizedSpecialName : "";
        bool specialModel = !targetSpecialName.empty();
        int specialSlot = 0;
        if (player.health == 0 || (player.animation.flags & ANIM_DEAD) || player.vehicleId != 0)
            ReleaseRemoteCutscenePartner(player.id, player.vehicleId ? "remote player entered vehicle" : "remote player died");
        auto it = g_remotePeds.find(player.id);
        if (it != g_remotePeds.end() && !LiveRemotePed(player.id)) it = g_remotePeds.end();


        if (it == g_remotePeds.end() && !specialModel && remoteModelId != 0 &&
            !WeaponModelResident(static_cast<int>(remoteModelId))) {
            CStreaming::RequestModel(remoteModelId, 2);
            CStreaming::LoadAllRequestedModels(false);
            if (!WeaponModelResident(static_cast<int>(remoteModelId))) continue;
        }
        bool remoteVehicleExit = false;
        float spawnX = player.x, spawnY = player.y, spawnZ = player.z;
        if (it != g_remotePeds.end()) {
            auto previousVisual = g_remoteVisuals.find(player.id);
            remoteVehicleExit = previousVisual != g_remoteVisuals.end() &&
                                previousVisual->second.wasInVehicle && !player.vehicleId;
        }

        const auto lastHealth = g_lastRemotePlayerHealth.find(player.id);
        const bool respawned = lastHealth != g_lastRemotePlayerHealth.end() &&
                               lastHealth->second == 0 && player.health > 0;
        g_lastRemotePlayerHealth[player.id] = player.health;
        if (respawned && it != g_remotePeds.end() && it->second &&
            (it->second->m_ePedState == PEDSTATE_DIE || it->second->m_ePedState == PEDSTATE_DEAD)) {
            ReleaseRemoteCutscenePartner(player.id, "remote player respawned");
            RetireRemotePed(it->second);
                    g_remotePeds.erase(it);
            g_remoteVisuals.erase(player.id);
            it = g_remotePeds.end();
            logger->Info("Remote player %u respawned; recreated stale death-state ped", player.id);
        }




        if (it != g_remotePeds.end() && !player.vehicleId && it->second && it->second->m_pVehicle) {
            CVehicle* oldVehicle = it->second->m_pVehicle;
            const CVector vehiclePosition = oldVehicle->GetPosition();
            const float dx = spawnX - vehiclePosition.x;
            const float dy = spawnY - vehiclePosition.y;



            if (dx * dx + dy * dy < 6.25f) {
                float sideX = 1.0f, sideY = 0.0f;
                if (const CMatrix* matrix = oldVehicle->GetMatrix()) {
                    sideX = matrix->right.x;
                    sideY = matrix->right.y;
                }
                const auto previousVisual = g_remoteVisuals.find(player.id);
                const uint8_t previousSeat = previousVisual != g_remoteVisuals.end()
                    ? previousVisual->second.lastVehicleSeat : 0;
                const float side = previousSeat == 0 ? -1.0f : 1.0f;
                spawnX = vehiclePosition.x + sideX * side * 1.6f;
                spawnY = vehiclePosition.y + sideY * side * 1.6f;
                spawnZ = vehiclePosition.z + 0.25f;
            }
            ReleaseRemoteCutscenePartner(player.id, "remote player exited vehicle");
            DiscardRemotePed(it->second, "vehicle exit");
            g_remotePeds.erase(player.id);
            g_remoteVisuals.erase(player.id);
            it = g_remotePeds.end();
            remoteVehicleExit = true;
            logger->Info("Remote player %u vehicle exit: replaced stale seated ped with clean on-foot ped", player.id);
        }






        if (player.vehicleId && it != g_remotePeds.end() && it->second && g_remoteVehicles.count(player.vehicleId)) {
            CVehicle* targetVehicle = g_remoteVehicles[player.vehicleId];
            if (it->second->m_pVehicle && it->second->m_pVehicle != targetVehicle) {
                ReleaseRemoteCutscenePartner(player.id, "remote player switched vehicle");
                DiscardRemotePed(it->second, "vehicle switch");
                g_remotePeds.erase(player.id);
                g_remoteVisuals.erase(player.id);
                it = g_remotePeds.end();
                logger->Info("Remote player %u vehicle switch: replaced stale attached ped", player.id);
            }
        }

        if (specialModel) {
            specialSlot = EnsureRemoteSpecialSlot(player.id, targetSpecialName);
            if (!specialSlot) {
                auto rejected = g_lastRejectedRemoteModels.find(player.id);
                if (rejected == g_lastRejectedRemoteModels.end() || rejected->second != player.modelId) {
                    WeaponFileLog("REMOTE_SPECIAL_SLOT_UNAVAILABLE id=%u model=%u name=%s fallback=keep_or_cj",
                                  player.id, player.modelId, targetSpecialName.c_str());
                    g_lastRejectedRemoteModels[player.id] = player.modelId;
                }
                continue;
            } else {
                if (!RemoteSpecialCharacterLoadedForName(specialSlot, targetSpecialName)) {
                    RequestRemoteSpecialCharacter(specialSlot, targetSpecialName);
                    CStreaming::LoadAllRequestedModels(false);
                    if (!RemoteSpecialCharacterLoadedForName(specialSlot, targetSpecialName)) continue;
                }
                remoteModelId = coop_special::ModelIndexFromSlot(specialSlot);
            }
        } else {
            const bool isUnidentifiedSpecial = remoteModelId >= coop_special::kFirstModelIndex &&
                                               remoteModelId <= coop_special::kLastModelIndex;
            const bool streamingReady = CStreaming::IsInitialised();
            const bool modelInfoOk = remoteModelId == 0 ||
                                     (remoteModelId < kModelInfoCapacity && streamingReady && HasPedModelInfo(remoteModelId));
            const bool inCdImage = remoteModelId > 0 && !isUnidentifiedSpecial && modelInfoOk &&
                                   CStreaming::IsObjectInCdImage(static_cast<int>(remoteModelId));
            const bool safeModel = remoteModelId == 0 || (!isUnidentifiedSpecial && modelInfoOk && inCdImage);
            if (!safeModel) {
                auto rejected = g_lastRejectedRemoteModels.find(player.id);
                if (rejected == g_lastRejectedRemoteModels.end() || rejected->second != remoteModelId) {
                    if (isUnidentifiedSpecial)
                        WeaponFileLog("REMOTE_SPECIAL_NAME_MISSING id=%u model=%u fallback=0", player.id, remoteModelId);
                    else
                        WeaponFileLog("REMOTE_MODEL_FALLBACK id=%u model=%u range_ok=%u stream_ready=%u model_info_ok=%u in_cd_image=%u",
                                      player.id, remoteModelId, remoteModelId < kModelInfoCapacity, streamingReady, modelInfoOk, inCdImage);
                    g_lastRejectedRemoteModels[player.id] = remoteModelId;
                }
                remoteModelId = 0;
            } else {
                g_lastRejectedRemoteModels.erase(player.id);
            }
        }

        if (it != g_remotePeds.end()) {
            const auto currentModel = *reinterpret_cast<const uint16_t*>(
                reinterpret_cast<uintptr_t>(it->second) + gtasa_v200_armv7::kCPedModelIndexOffset);
            auto leaseIt = g_remoteSpecialLeases.find(player.id);
            const std::string appliedSpecialName = leaseIt != g_remoteSpecialLeases.end()
                ? leaseIt->second.activeName : "";
            if (currentModel != remoteModelId || appliedSpecialName != targetSpecialName) {
                if (!specialModel) LoadNetworkPedModel(remoteModelId);


                const int pedType = specialModel ? 24 : 4;
                CPed* replacement = CreateRemotePedForModel(pedType, remoteModelId, player.x, player.y, player.z, specialModel);
                if (!replacement && specialModel) {
                    ReleasePendingRemoteSpecialSlot(player.id);
                    continue;
                }
                if (!replacement && remoteModelId != 0) {
                    WeaponFileLog("REMOTE_MODEL_CREATE_FAIL id=%u model=%u special=%s path=change",
                                  player.id, remoteModelId, specialModel ? targetSpecialName.c_str() : "");
                    if (!specialModel) {  }
                    remoteModelId = 0;
                    specialModel = false;
                    targetSpecialName.clear();
                    specialSlot = 0;
                    LoadNetworkPedModel(remoteModelId);
                    replacement = g_saUtils->CreatePed(24, remoteModelId, player.x, player.y, player.z);
                }
                if (!replacement) {
                    logger->Error("Remote player %u skin change failed; keeping existing ped", player.id);
                    ReleasePendingRemoteSpecialSlot(player.id);
                    continue;
                }
                replacement->m_nAreaCode = player.areaCode;
                ReleaseRemoteCutscenePartner(player.id, "remote player model changed");
                RetireRemotePed(it->second);
                it->second = replacement;
                g_remotePedRefs[player.id] = CPools::GetPedRef(replacement);
                if (specialModel) CommitRemoteSpecialSlot(player.id, specialSlot, targetSpecialName);
                else ReleaseRemoteSpecialLease(player.id);
                if (!specialModel) {  }
                g_remoteVisuals.erase(player.id);
                logger->Info("Remote player %u model changed %u -> %u special=%s",
                             player.id, currentModel, remoteModelId,
                             specialModel ? targetSpecialName.c_str() : "");
            }
        }
        if (it == g_remotePeds.end()) {
            if (!specialModel) LoadNetworkPedModel(remoteModelId);

            const int pedType = specialModel ? 24 : 4;
            CPed* ped = CreateRemotePedForModel(pedType, remoteModelId, spawnX, spawnY, spawnZ, specialModel);
            if (!ped && specialModel) {
                ReleasePendingRemoteSpecialSlot(player.id);
                continue;
            }
            if (!ped && remoteModelId != 0) {
                WeaponFileLog("REMOTE_MODEL_CREATE_FAIL id=%u model=%u special=%s path=spawn",
                              player.id, remoteModelId, specialModel ? targetSpecialName.c_str() : "");
                logger->Error("Remote player %u model %u could not be created; falling back to CJ",
                              player.id, remoteModelId);
                if (!specialModel) {  }
                remoteModelId = 0;
                specialModel = false;
                targetSpecialName.clear();
                specialSlot = 0;
                LoadNetworkPedModel(remoteModelId);
                ped = g_saUtils->CreatePed(4, remoteModelId, spawnX, spawnY, spawnZ);
            }
            if (!ped) {
                logger->Error("Remote player %u ped creation failed (requested model %u)",
                              player.id, remoteModelId);
                ReleaseRemoteSpecialLease(player.id);
                continue;
            }
            ped->m_nAreaCode = player.areaCode;
            g_remotePeds.emplace(player.id, ped);
            g_remotePedRefs[player.id] = CPools::GetPedRef(ped);
            g_remoteVisuals[player.id] = MakeInitialRemoteVisualState(player);
            g_remoteVisuals[player.id].lastVehicleSeat = player.vehicleSeat;
            if (specialModel) CommitRemoteSpecialSlot(player.id, specialSlot, targetSpecialName);
            else ReleaseRemoteSpecialLease(player.id);
            if (!specialModel) {  }
            auto* physical = reinterpret_cast<CPhysical*>(ped);
            g_saUtils->SetAngle(physical, static_cast<unsigned char>(2), player.heading);
            if (!cutsceneSceneActive && !player.vehicleId && !remoteVehicleExit) {
                ApplyRemoteAnimationProfile(ped, player.animation, false, false);
            } else if (!cutsceneSceneActive && remoteVehicleExit && g_setMoveState && g_setMoveAnim) {



                g_setMoveState(ped, static_cast<int>(kMoveStill));
                g_setMoveAnim(ped);
            }
            if (!cutsceneSceneActive) {
                if (ApplyRemoteWeapon(ped, player.weapon)) {
                    g_remoteVisuals[player.id].weapon = player.weapon;
                    ApplyRemoteVitals(ped, player.health, player.armour);
                    logger->Info("Remote player %u native weapon apply complete: weapon=%u", player.id, player.weapon);
                } else {
                    logger->Error("Remote player %u weapon %u application deferred for retry", player.id, player.weapon);
                }
            }
            logger->Info("Remote player %u spawned at (%0.1f, %0.1f, %0.1f)",
                         player.id, player.x, player.y, player.z);
        } else {
            if (player.health == 0 || (player.animation.flags & ANIM_DEAD) || player.vehicleId != 0 || it->second->m_pVehicle)
                ReleaseRemoteCutscenePartner(player.id, "remote player no longer on foot");
            const bool cutsceneSuppressed = (cutsceneSceneActive ||
                g_heldRemoteCutscenePartners.find(player.id) != g_heldRemoteCutscenePartners.end()) &&
                player.vehicleId == 0 && it->second->m_pVehicle == nullptr && player.health > 0 &&
                !(player.animation.flags & ANIM_DEAD);
            auto& visual = g_remoteVisuals[player.id];
            if (!visual.initialized) {
                visual = MakeInitialRemoteVisualState(player);
            } else {
                const uint64_t playerFrameElapsedMs=coopfusion_sync::ElapsedMs(nowMs,visual.lastUpdateMs);
                const float playerPositionBlend=coopfusion_sync::BlendForElapsedMs(playerFrameElapsedMs, 48.0f);
                visual.lastTargetX = player.x;
                visual.lastTargetY = player.y;
                visual.x = SmoothValue(visual.x, player.x, playerPositionBlend);
                visual.y = SmoothValue(visual.y, player.y, playerPositionBlend);
                visual.z = SmoothValue(visual.z, player.z, playerPositionBlend);



                visual.heading += AngleDelta(player.heading, visual.heading) * playerPositionBlend;
            }
            if (!player.vehicleId) g_remoteCarjackVictimUntilMs.erase(it->second);
            const bool carjackVictimActive = IsRemoteCarjackVictimProtected(it->second);
            const bool refreshForLocalInterior = !carjackVictimActive && localAreaTransition &&
                !player.vehicleId && player.health > 0 &&
                !(player.animation.flags & ANIM_DEAD) &&
                player.areaCode == localAreaCodeForLog && !it->second->m_pVehicle;
            if (refreshForLocalInterior) {
                visual.x = player.x;
                visual.y = player.y;
                visual.z = player.z;
                visual.lastTargetX = player.x;
                visual.lastTargetY = player.y;
                visual.heading = player.heading;
                visual.animationInitialized = false;
                visual.animationSequence = 0;
                visual.animationLocomotion = 0;
                visual.animationAction = 0;
                visual.animationFlags = 0;
                visual.animationAssociationCount = 0;
                if (it->second->m_ePedState == PEDSTATE_DIE ||
                    it->second->m_ePedState == PEDSTATE_DEAD)
                    it->second->m_ePedState = PEDSTATE_IDLE;
                WeaponFileLog("REMOTE_INTERIOR_RESYNC id=%u area=%u position=(%.2f,%.2f,%.2f)",
                              static_cast<unsigned>(player.id), localAreaCodeForLog,
                              player.x, player.y, player.z);
            }
            const bool nativeCarjackActive = g_remoteCarjackAttempts.find(player.id) != g_remoteCarjackAttempts.end();
            const bool preserveNativeAnimation = carjackVictimActive || (!refreshForLocalInterior &&
                (nativeCarjackActive || !it->second->IsPedInControl() ||
                 it->second->m_ePedState == PEDSTATE_DIE ||
                 it->second->m_ePedState == PEDSTATE_DEAD));
            const bool animationChanged = !visual.animationInitialized ||
                visual.animationSequence != player.animation.sequence ||
                visual.animationLocomotion != player.animation.locomotion ||
                visual.animationAction != static_cast<uint8_t>(player.animation.action) ||
                visual.animationFlags != player.animation.flags;
            const auto position = reinterpret_cast<CPhysical*>(it->second);

            const auto networkVehicleIt = player.vehicleId ? g_remoteVehicles.find(player.vehicleId) : g_remoteVehicles.end();
            const bool seatedOnNetwork = networkVehicleIt != g_remoteVehicles.end() && networkVehicleIt->second != nullptr;
            const bool areaChanged = it->second->m_nAreaCode != player.areaCode;
            const CVector networkTarget(player.x, player.y, player.z);
            const CVector current = it->second->GetPosition();
            const bool largePositionJump = !carjackVictimActive && !seatedOnNetwork &&
                DistanceBetweenPoints(current, networkTarget) > kRemoteTeleportDistance;
            if (!seatedOnNetwork && !preserveNativeAnimation && !cutsceneSuppressed) {
                position->m_vecMoveSpeed = largePositionJump ? CVector(0.0f,0.0f,0.0f) :
                    CVector(player.vx,player.vy,player.vz);
            }
            if (cutsceneSuppressed) {
            } else if (seatedOnNetwork && !carjackVictimActive) {
                it->second->m_nAreaCode = player.areaCode;
                networkVehicleIt->second->m_nAreaCode = player.areaCode;
                visual.x = player.x; visual.y = player.y; visual.z = player.z;
                visual.lastTargetX = player.x; visual.lastTargetY = player.y;
            } else if (!carjackVictimActive && (areaChanged || largePositionJump || refreshForLocalInterior)) {


                if (g_entityTeleport) {
                    gtasa_v200_armv7::CVectorAbi target{player.x, player.y, player.z};
                    g_entityTeleport(reinterpret_cast<void*>(it->second), target, 1);
                } else {
                    g_saUtils->SetPosition(position, player.x, player.y, player.z);
                }
                it->second->m_nAreaCode = player.areaCode;
                visual.x = player.x; visual.y = player.y; visual.z = player.z;
                visual.lastTargetX = player.x; visual.lastTargetY = player.y;
                visual.heading = player.heading;
                g_saUtils->SetAngle(position, static_cast<unsigned char>(2), player.heading);
                if (refreshForLocalInterior) {
                    position->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
                    position->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);
                }
                logger->Info("REMOTE_HARD_RESYNC id=%u localArea=%u remoteArea=%u jump=%d interiorRefresh=%d net=(%.1f,%.1f,%.1f)",
                              static_cast<unsigned>(player.id), localAreaCodeForLog,
                              static_cast<unsigned>(player.areaCode), largePositionJump ? 1 : 0,
                              refreshForLocalInterior ? 1 : 0, player.x, player.y, player.z);
            } else if (!preserveNativeAnimation) {
                g_saUtils->SetPosition(position, visual.x, visual.y, visual.z);
                g_saUtils->SetAngle(position, static_cast<unsigned char>(2), visual.heading);
            } else {





                static std::unordered_map<unsigned, uint64_t> lastSkipLogMs;
                const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                uint64_t& lastMs = lastSkipLogMs[static_cast<unsigned>(player.id)];
                if (nowMs - lastMs > 1000) {
                    lastMs = nowMs;
                    const CVector pedPos = it->second->GetPosition();
                    logger->Info("REMOTE_POS_SKIPPED id=%u pedState=%d bInVehicle=%d m_pVehicle=%p netVehicleId=%u net=(%.1f,%.1f,%.1f) ped=(%.1f,%.1f,%.1f)",
                                 static_cast<unsigned>(player.id), static_cast<int>(it->second->m_ePedState),
                                 it->second->bInVehicle ? 1 : 0, static_cast<void*>(it->second->m_pVehicle),
                                 static_cast<unsigned>(player.vehicleId),
                                 visual.x, visual.y, visual.z, pedPos.x, pedPos.y, pedPos.z);
                }
            }
            if (reportPlayerHealth) {
                const float observedHealth = it->second->GetHealth();
                const float observedArmour = it->second->m_fArmour;
                if (std::isfinite(observedHealth) && std::isfinite(observedArmour)) {
                    const auto health = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, observedHealth)));
                    const auto armour = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, observedArmour)));
                    g_client->SendPlayerHealth(player.id, health, armour);
                }
            }
            ApplyRemoteVitals(it->second, player.health, player.armour);
            {
            }
            if (!preserveNativeAnimation && !cutsceneSuppressed) {
                const bool inVehicle = it->second->m_pVehicle != nullptr;
                const bool networkInVehicle = player.vehicleId != 0;

                const bool shooting = (player.animation.flags & ANIM_FIRING) != 0 ||
                                      (visual.shootingFrames != 0 && visual.fireAnimFrames == 0);
                const bool justExitedVehicle = visual.wasInVehicle && !inVehicle && !networkInVehicle;
                if ((justExitedVehicle || remoteVehicleExit) && g_setMoveState && g_setMoveAnim) {
                    g_setMoveState(it->second, static_cast<int>(kMoveStill));
                    g_setMoveAnim(it->second);
                    visual.animationInitialized = false;
                    visual.animationAssociationCount = 0;
                }






                if (inVehicle || player.vehicleId != 0) {
                    visual.animationAssociationCount = 0;
                    visual.animationInitialized = false;
                } else if (remoteVehicleExit) {


                    visual.animationInitialized = true;
                    visual.animationSequence = player.animation.sequence;
                    visual.animationLocomotion = kMoveStill;
                    visual.animationAction = static_cast<uint8_t>(AnimationAction::None);
                    visual.animationFlags = 0;
                } else {

                    ApplyRemoteAnimationGroup(it->second, player.animation);

                    if (player.animationAssociationCount || visual.animationAssociationCount)
                        ApplyAssociationStates(it->second, player.id, player.animationAssociations.data(),
                                                   player.animationAssociationCount,
                                                   visual.animationAssociationCount,
                                                   visual.animationAssociations,
                                                   visual.animationInitialized);
                    if (player.animationAssociationCount == 0 && animationChanged) {
                        ApplyRemoteAnimationProfile(it->second, player.animation, inVehicle || player.vehicleId != 0, shooting);
                        visual.animationSequence = player.animation.sequence;
                        visual.animationLocomotion = player.animation.locomotion;
                        visual.animationAction = static_cast<uint8_t>(player.animation.action);
                        visual.animationFlags = player.animation.flags;
                        visual.animationInitialized = true;
                    }
                }
                if (kBonePoseSyncEnabled) ApplyRemoteBones(it->second, player);
                if (visual.shootingFrames != 0) --visual.shootingFrames;
                if (visual.fireAnimFrames != 0 && --visual.fireAnimFrames == 0) FadeRemoteFireAnimation(visual, it->second);
                if (networkInVehicle) visual.lastVehicleSeat = player.vehicleSeat;
                visual.wasInVehicle = inVehicle || networkInVehicle;
            }
            if (!cutsceneSuppressed && visual.weapon != player.weapon) {
                if (ApplyRemoteWeapon(it->second, player.weapon))
                {
                    visual.weapon = player.weapon;
                    logger->Info("Remote player %u native weapon switch complete: weapon=%u", player.id, player.weapon);
                }
                else
                    logger->Error("Remote player %u weapon %u application failed; retrying", player.id, player.weapon);
            }

            if (!cutsceneSuppressed)
                UpdateRemoteAimAndShooting(it->second, player, visual);
        }
        if (player.clothesValid && player.modelId == 0 && !specialModel && !clothesRebuildBlocked) {
            if (CPed* clothesPed = LiveRemotePed(player.id))
                ApplyRemotePlayerClothes(player.id, clothesPed, player.clothes);
        }
    }


    ApplyPendingRemotePlayerHits();
    UpdateLocalDeath();

    const uint64_t carjackNowMs = NowSteadyMs();
    for (auto eventIt = g_pendingRemoteVehicleEnters.begin(); eventIt != g_pendingRemoteVehicleEnters.end();) {
        const RemoteVehicleEnter& enter = eventIt->event;
        if (carjackNowMs - eventIt->receivedAtMs > kRemoteCarjackEventLifetimeMs) {
            logger->Info("REMOTE_CARJACK_EVENT_EXPIRED player=%u vehicle=%u", enter.id, enter.vehicleId);
            eventIt = g_pendingRemoteVehicleEnters.erase(eventIt);
            continue;
        }
        auto pit = g_remotePeds.find(enter.id);
        auto vit = g_remoteVehicles.find(enter.vehicleId);
        if (pit == g_remotePeds.end() || vit == g_remoteVehicles.end() || !pit->second || !vit->second) {
            ++eventIt;
            continue;
        }
        CPed* entrant = pit->second;
        CVehicle* vehicle = vit->second;
        int seatIndex = -1;
        if (!SeatIndexFor(vehicle, enter.seat, seatIndex)) {
            logger->Info("REMOTE_VEHICLE_ENTER_INVALID_SEAT player=%u vehicle=%u seat=%u",
                         enter.id, enter.vehicleId, enter.seat);
            eventIt = g_pendingRemoteVehicleEnters.erase(eventIt);
            continue;
        }
        CPed* occupant = seatIndex < 0 ? vehicle->m_pDriver : vehicle->m_apPassengers[seatIndex];
        if (seatIndex == -1 && occupant && occupant != entrant &&
            occupant->m_fHealth > 0.0f) {
            const int pedRef = CPools::GetPedRef(entrant);
            const int vehicleRef = CPools::GetVehicleRef(vehicle);
            if (pedRef > 0 && vehicleRef > 0 && CPools::GetPed(pedRef) == entrant &&
                CPools::GetVehicle(vehicleRef) == vehicle) {
                g_remoteTaskPermitUntilMs[occupant] = carjackNowMs + kRemoteCarjackTaskDurationMs;
                g_remoteCarjackVictimUntilMs[occupant] = carjackNowMs + kRemoteCarjackVictimGraceMs;
                for (const auto& npc : g_remoteNpcs) {
                    if (npc.second == occupant) {
                        g_remoteCarjackNpcRemovalUntilMs[npc.first] = carjackNowMs + kRemoteCarjackVictimGraceMs;
                        break;
                    }
                }
                const bool taskStarted = StartRemoteDriverEntryTask(entrant, vehicle);
                if (taskStarted) {
                    g_remoteTaskPermitUntilMs[entrant] = carjackNowMs + kRemoteCarjackTaskDurationMs + 220;
                    g_remoteCarjackAttempts[enter.id] = RemoteCarjackAttempt{
                        enter.vehicleId, pedRef, vehicleRef, occupant,
                        carjackNowMs + kRemoteCarjackTaskDurationMs};
                    logger->Info("REMOTE_CARJACK_TASK_START player=%u vehicle=%u entrantRef=%d vehicleRef=%d occupantRef=%d",
                                 enter.id, enter.vehicleId, pedRef, vehicleRef, CPools::GetPedRef(occupant));
                } else {
                    g_remoteCarjackVictimUntilMs.erase(occupant);
                    g_remoteTaskPermitUntilMs.erase(occupant);
                    for (auto npc = g_remoteNpcs.begin(); npc != g_remoteNpcs.end(); ++npc)
                        if (npc->second == occupant) g_remoteCarjackNpcRemovalUntilMs.erase(npc->first);
                    logger->Info("REMOTE_CARJACK_TASK_FAILED player=%u vehicle=%u; seat sync will use safe fallback",
                                 enter.id, enter.vehicleId);
                }
            }
        }
        eventIt = g_pendingRemoteVehicleEnters.erase(eventIt);
    }

    for (const auto& player : g_client->GetRemotePlayers()) {
        if (!player.vehicleId) continue;
        auto pit = g_remotePeds.find(player.id);
        if (pit != g_remotePeds.end() && !LiveRemotePed(player.id)) pit = g_remotePeds.end();
        auto vit = g_remoteVehicles.find(player.vehicleId);
        if (pit != g_remotePeds.end() && vit != g_remoteVehicles.end() && pit->second && vit->second) {
            const int liveVehicleRef = CPools::GetVehicleRef(vit->second);
            if (liveVehicleRef <= 0 || CPools::GetVehicle(liveVehicleRef) != vit->second) {
                WeaponFileLog("REMOTE_VEHICLE_SEAT_SYNC_SKIP reason=stale-vehicle player=%u vehicle=%u ref=%d",
                              player.id, player.vehicleId, liveVehicleRef);
                continue;
            }
            int seatIndex = -1;
            if (!SeatIndexFor(vit->second, player.vehicleSeat, seatIndex)) {
                logger->Info("Remote player %u: vehicle %u has no seat %u; not placed",
                             player.id, player.vehicleId, player.vehicleSeat);
                continue;
            }
            if (IsRemoteCarjackVictimProtected(pit->second)) continue;
            if (RemotePedOccupiesSeat(pit->second, vit->second, seatIndex)) {
                auto completedCarjack = g_remoteCarjackAttempts.find(player.id);
                if (completedCarjack != g_remoteCarjackAttempts.end()) {
                    if (completedCarjack->second.victimPed)
                        g_remoteTaskPermitUntilMs.erase(completedCarjack->second.victimPed);
                    g_remoteCarjackAttempts.erase(completedCarjack);
                }
                continue;
            }
            auto carjackAttempt = g_remoteCarjackAttempts.find(player.id);
            const bool attemptStillBound = carjackAttempt != g_remoteCarjackAttempts.end() &&
                carjackAttempt->second.vehicleId == player.vehicleId &&
                carjackAttempt->second.pedPoolRef > 0 && carjackAttempt->second.vehiclePoolRef > 0 &&
                CPools::GetPed(carjackAttempt->second.pedPoolRef) == pit->second &&
                CPools::GetVehicle(carjackAttempt->second.vehiclePoolRef) == vit->second;
            if (attemptStillBound && NowSteadyMs() < carjackAttempt->second.expiresAtMs) {
                continue;
            }
            if (carjackAttempt != g_remoteCarjackAttempts.end()) {
                logger->Info("REMOTE_CARJACK_TASK_TIMEOUT player=%u vehicle=%u; using safe seat fallback",
                             player.id, player.vehicleId);
                if (carjackAttempt->second.victimPed)
                    g_remoteTaskPermitUntilMs.erase(carjackAttempt->second.victimPed);
                g_remoteCarjackAttempts.erase(carjackAttempt);
            }
            CPed* occupant = seatIndex < 0 ? vit->second->m_pDriver : vit->second->m_apPassengers[seatIndex];
            if (occupant && occupant != pit->second) {




                if (occupant == FindPlayerPed(-1) || IsRemotePlayerPed(occupant)) {
                    logger->Info("Remote player %u waiting for occupied vehicle %u seat %u",
                                 player.id, player.vehicleId, player.vehicleSeat);
                    continue;
                }
                const int occupantRef = CPools::GetPedRef(occupant);
                if (occupantRef > 0) g_saUtils->ScriptCommand(&kScmRemoveCharFromCarMaintainPosition, occupantRef);



                const bool stillOccupiesSeat = seatIndex < 0
                    ? vit->second->m_pDriver == occupant
                    : vit->second->m_apPassengers[seatIndex] == occupant;
                if (stillOccupiesSeat) {
                    if (seatIndex < 0) vit->second->m_pDriver = nullptr;
                    else vit->second->m_apPassengers[seatIndex] = nullptr;
                    occupant->m_pVehicle = nullptr;
                    occupant->bInVehicle = 0;
                }
                logger->Info("Remote player %u cleared stale NPC from vehicle %u seat %u",
                             player.id, player.vehicleId, player.vehicleSeat);
            }
            if (WarpRemotePedIntoVehicle(pit->second, vit->second, seatIndex)) {
                logger->Info("Remote player %u seat verified after direct warp vehicle=%u seat=%u",
                             player.id, player.vehicleId, player.vehicleSeat);
            } else {
                static std::unordered_map<uint16_t, uint64_t> lastWarpRetryLog;
                const uint64_t logNow = NowSteadyMs();
                uint64_t& lastLog = lastWarpRetryLog[player.id];
                if (logNow - lastLog >= 1000) {
                    lastLog = logNow;
                    logger->Error("Remote player %u seat pending after direct warp vehicle=%u seat=%u pedVehicle=%p driver=%p",
                                  player.id, player.vehicleId, player.vehicleSeat,
                                  static_cast<void*>(pit->second->m_pVehicle),
                                  static_cast<void*>(vit->second->m_pDriver));
                }
            }
        }
    }
    const uint64_t shotNowMs = SteadyNowMs();
    for (auto shotIt = g_pendingRemoteShots.begin(); shotIt != g_pendingRemoteShots.end();) {
        if (shotNowMs >= shotIt->expiresAtMs) {
            WeaponFileLog("REMOTE_SHOT_EXPIRED id=%u kind=%u weapon=%u", shotIt->shot.id, shotIt->shot.kind, shotIt->shot.weapon);
            shotIt = g_pendingRemoteShots.erase(shotIt);
            continue;
        }
        if (g_heldRemoteCutscenePartners.find(shotIt->shot.id) != g_heldRemoteCutscenePartners.end()) {
            shotIt = g_pendingRemoteShots.erase(shotIt);
            continue;
        }
        auto it = g_remotePeds.find(shotIt->shot.id);
        if (it != g_remotePeds.end() && !LiveRemotePed(shotIt->shot.id)) it = g_remotePeds.end();
        if (it == g_remotePeds.end()) { ++shotIt; continue; }
        if (ExecuteRemoteShot(shotIt->shot, it->second)) shotIt = g_pendingRemoteShots.erase(shotIt);
        else ++shotIt;
    }

    if (cutsceneSceneActive) UpdateCutscenePartners(cutsceneCj);
}

void OnSaUtilsPlayerUpdate(uintptr_t )
{
    if (!g_saUtils || !g_saUtils->IsGameInitialised()) return;
    TickCutsceneMocap();
    if (!g_client) return;
    for (const auto& test : g_client->DrainNpcSpawnTests()) SpawnTestNpc(test);
    UpdateRemotePlayers();
}

struct NameTagScreenData
{
    CVector2D posXY;
    CVector2D sizeXY;
    bool isWithinScreen = false;
};

NameTagScreenData ConvertRemoteNameTagCoord(CVector pos, bool nearClip, bool farClip, CVector2D size)
{
    NameTagScreenData out{};
    RwV3d screenPos2D{};
    float sizeX = 0.0f, sizeY = 0.0f;
    const bool result = CSprite::CalcScreenCoors(pos, &screenPos2D, &sizeX, &sizeY, farClip, nearClip);
    if (!result || RsGlobal.maximumWidth <= 0 || RsGlobal.maximumHeight <= 0) return out;
    out.posXY.x = (screenPos2D.x / RsGlobal.maximumWidth) * 640.0f;
    out.posXY.y = (screenPos2D.y / RsGlobal.maximumHeight) * 448.0f;
    sizeX = (sizeX / RsGlobal.maximumWidth) * 15.0f;
    sizeY = (sizeY / RsGlobal.maximumHeight) * 15.0f;
    out.sizeXY.x = sizeX * size.x;
    out.sizeXY.y = sizeY * size.y;
    out.isWithinScreen = true;
    return out;
}

struct RemoteNameTagLayout
{
    float screenX = 0.0f;
    float screenY = 0.0f;
    float iconSize = 0.0f;
    float gap = 0.0f;
    float barWidth = 0.0f;
    float barHeight = 0.0f;
    float armourHeight = 0.0f;
    float tagLeft = 0.0f;
    float iconCx = 0.0f;
    float iconCy = 0.0f;
    float textX = 0.0f;
    float textY = 0.0f;
    float healthY = 0.0f;
    float armourY = 0.0f;
    float fontSize = 0.0f;
    float pad = 0.0f;
    float starRadius = 0.0f;
    float alpha = 1.0f;
};

bool CalculateRemoteNameTagLayout(CPed* ped, RemoteNameTagLayout& layout)
{
    if (!ped || RsGlobal.maximumWidth <= 0 || RsGlobal.maximumHeight <= 0)
        return false;

    CVector pos = ped->GetPosition();
    pos.z += 1.65f;
    const float distance = DistanceBetweenPoints(pos, TheCamera.GetPosition());
    if (distance > 60.0f)
        return false;

    const auto convert = ConvertRemoteNameTagCoord(pos, true, true, CVector2D(1.5f, 1.5f));
    if (!convert.isWithinScreen)
        return false;

    layout.screenX = convert.posXY.x * RsGlobal.maximumWidth * 0.0015625f;
    layout.screenY = convert.posXY.y * RsGlobal.maximumHeight * 0.002232143f;

    const float screenH = static_cast<float>(RsGlobal.maximumHeight);
    const float scale = g_remoteNameTagScale;

    layout.alpha = distance <= 35.0f ? 1.0f : std::max(0.0f, 1.0f - (distance - 35.0f) / 25.0f);

    layout.iconSize = std::max(40.0f, screenH * 0.12f) * scale;
    layout.gap = std::max(10.0f, screenH * 0.03f) * scale;
    layout.barWidth = std::max(72.0f, screenH * 0.21f) * scale;
    layout.barHeight = std::max(10.0f, layout.iconSize * 0.19f);
    layout.armourHeight = std::max(4.0f, layout.barHeight * 0.38f);
    layout.fontSize = std::max(11.0f, layout.iconSize * 0.22f);
    layout.pad = std::max(3.0f, layout.iconSize * 0.07f);
    layout.starRadius = std::max(6.0f, layout.iconSize * 0.115f);

    const float totalWidth = layout.iconSize + layout.gap + layout.barWidth;
    layout.tagLeft = layout.screenX - totalWidth * 0.5f;
    layout.iconCx = layout.tagLeft + layout.iconSize * 0.5f;
    layout.iconCy = layout.screenY - layout.iconSize * 0.02f;
    layout.textX = layout.tagLeft + layout.iconSize + layout.gap;

    const float lineGap = std::max(2.0f, layout.iconSize * 0.04f);
    const float blockHeight = layout.fontSize + lineGap + layout.barHeight + lineGap + layout.armourHeight;
    layout.textY = layout.iconCy - blockHeight * 0.5f;
    layout.healthY = layout.textY + layout.fontSize + lineGap;
    layout.armourY = layout.healthY + layout.barHeight + lineGap;
    return true;
}

void DrawRemoteWeaponIconsInternal()
{
    if (!g_client || !g_client->IsConnected() || !g_saUtils || !g_saUtils->IsGameInitialised())
        return;

    for (const auto& player : g_client->GetRemotePlayers())
    {
        auto it = g_remotePeds.find(player.id);
        if (it != g_remotePeds.end() && !LiveRemotePed(player.id)) it = g_remotePeds.end();
        if (it == g_remotePeds.end() || !it->second)
            continue;

        RemoteNameTagLayout layout{};
        if (!CalculateRemoteNameTagLayout(it->second, layout))
            continue;

        DrawRemoteWeaponIcon(it->second, player.weapon, layout.iconCx, layout.iconCy, layout.iconSize,
                             255.0f * layout.alpha);
    }
}

ImU32 TagAlpha(ImU32 color, float alphaScale)
{
    const float a = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * std::max(0.0f, std::min(1.0f, alphaScale));
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

void DrawRemoteImGuiMeter(ImDrawList* drawList, float left, float top, float width, float height,
                          float value, ImU32 fill, float alpha)
{
    const float clamped = std::max(0.0f, std::min(1.0f, value));
    const float rounding = height * 0.5f;
    drawList->AddRectFilled(ImVec2(left, top), ImVec2(left + width, top + height),
        TagAlpha(IM_COL32(0, 0, 0, 170), alpha), rounding);
    const float fillWidth = width * clamped;
    if (fillWidth > 0.5f)
        drawList->AddRectFilled(ImVec2(left, top), ImVec2(left + fillWidth, top + height),
            TagAlpha(fill, alpha), std::min(rounding, fillWidth * 0.5f));
}

void DrawRemoteStar(ImDrawList* drawList, float cx, float cy, float outerRadius,
                    ImU32 fill, ImU32 outline, float outlineThickness)
{
    constexpr float kPi = 3.14159265f;
    ImVec2 points[10];
    const float innerRadius = outerRadius * 0.46f;
    for (int i = 0; i < 10; ++i) {
        const float angle = -kPi * 0.5f + static_cast<float>(i) * (kPi / 5.0f);
        const float radius = (i % 2 == 0) ? outerRadius : innerRadius;
        points[i] = ImVec2(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
    }
    const ImVec2 centre(cx, cy);
    for (int i = 0; i < 10; ++i)
        drawList->AddTriangleFilled(centre, points[i], points[(i + 1) % 10], fill);
    if (outlineThickness > 0.0f)
        drawList->AddPolyline(points, 10, outline, ImDrawFlags_Closed, outlineThickness);
}

void DrawRemoteWantedStars(ImDrawList* drawList, const RemoteNameTagLayout& layout, uint8_t wanted)
{
    if (wanted == 0) return;
    const int lit = std::min<int>(wanted, 6);
    const float radius = layout.starRadius;
    const float spacing = radius * 2.35f;
    const float rowWidth = spacing * 5.0f;
    const float startX = layout.screenX - rowWidth * 0.5f;
    const float y = layout.iconCy - layout.iconSize * 0.5f - radius * 1.35f;

    const float pulse = wanted >= 4 ? 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f) : 0.0f;
    for (int i = 0; i < 6; ++i) {
        const float cx = startX + spacing * static_cast<float>(i);
        if (i < lit) {
            DrawRemoteStar(drawList, cx, y, radius * 1.28f,
                TagAlpha(IM_COL32(255, 190, 40, static_cast<int>(40 + 45 * pulse)), layout.alpha), 0, 0.0f);
            DrawRemoteStar(drawList, cx, y, radius,
                TagAlpha(IM_COL32(255, 214, 72, 255), layout.alpha),
                TagAlpha(IM_COL32(110, 62, 0, 255), layout.alpha), std::max(1.2f, radius * 0.14f));
            DrawRemoteStar(drawList, cx, y - radius * 0.12f, radius * 0.45f,
                TagAlpha(IM_COL32(255, 244, 190, 170), layout.alpha), 0, 0.0f);
        } else {
            DrawRemoteStar(drawList, cx, y, radius,
                TagAlpha(IM_COL32(70, 72, 80, 150), layout.alpha),
                TagAlpha(IM_COL32(0, 0, 0, 170), layout.alpha), std::max(1.0f, radius * 0.1f));
        }
    }
}

void RenderRemotePlayerNameTagsInternal(ImDrawList* drawList)
{
    if (!drawList || !g_client || !g_client->IsConnected() || !g_saUtils || !g_saUtils->IsGameInitialised())
        return;

    for (const auto& player : g_client->GetRemotePlayers())
    {
        auto it = g_remotePeds.find(player.id);
        if (it != g_remotePeds.end() && !LiveRemotePed(player.id)) it = g_remotePeds.end();
        if (it == g_remotePeds.end() || !it->second)
            continue;

        RemoteNameTagLayout layout{};
        if (!CalculateRemoteNameTagLayout(it->second, layout))
            continue;
        if (layout.alpha <= 0.01f)
            continue;

        const float a = layout.alpha;
        const ImU32 accent = NameTagColor(player.colorIndex);

        if (g_remoteNameTagsVisible && player.name[0] != '\0')
        {
            float fontSize = layout.fontSize;
            const float textWidth = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, player.name).x;
            if (textWidth > layout.barWidth && textWidth > 0.0f)
                fontSize = std::max(8.0f, fontSize * layout.barWidth / textWidth);
            UI::OutlinedText(drawList, fontSize, ImVec2(layout.textX, layout.textY),
                TagAlpha(accent, a), player.name);
        }

        if (g_remoteHealthBarsVisible)
        {
            DrawRemoteImGuiMeter(drawList, layout.textX, layout.healthY, layout.barWidth, layout.barHeight,
                static_cast<float>(player.health) / 100.0f, IM_COL32(60, 200, 80, 255), a);
            if (player.armour > 0)
                DrawRemoteImGuiMeter(drawList, layout.textX, layout.armourY, layout.barWidth, layout.armourHeight,
                    static_cast<float>(player.armour) / 100.0f, IM_COL32(200, 208, 224, 255), a);
        }

        DrawRemoteWantedStars(drawList, layout, player.wanted);
    }
}


static void HookWorldProcess()
{
    if (g_originalWorldProcess) g_originalWorldProcess();
}

void OnGameProcess()
{
    if (!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;
    for (const RemoteGameTime& time : g_client->DrainGameTimes()) {
        if (CClock::ms_nGameClockHours != time.hour || CClock::ms_nGameClockMinutes != time.minute || CClock::ms_nGameClockDays != time.day)
            CClock::SetGameClock(time.hour, time.minute, time.day);
    }
    g_client->SetLocalGameTime(CClock::ms_nGameClockHours, CClock::ms_nGameClockMinutes, CClock::ms_nGameClockDays);
    CPlayerPed* localPlayer = FindPlayerPed(-1);
    if (localPlayer) {
        DetectLocalCarjackIntent(localPlayer);
        g_client->SetLocalAreaCode(localPlayer->m_nAreaCode);


        const uint8_t weapon = CurrentWeaponTypeForPed(localPlayer);
        g_client->SetLocalWeaponType(weapon);
        const auto health = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, localPlayer->GetHealth())));
        const auto armour = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, localPlayer->m_fArmour)));
        g_client->SetLocalHealthArmour(health, armour);
        g_client->SetLocalWanted(LocalWantedLevel());
        if (localPlayer->m_pPlayerData && localPlayer->m_pPlayerData->m_pPedClothesDesc) {
            PlayerClothesDesc localClothes = *reinterpret_cast<const PlayerClothesDesc*>(
                localPlayer->m_pPlayerData->m_pPedClothesDesc);
            ReadLocalBodyStats(localClothes);
            g_client->SetLocalClothesDesc(localClothes);
        }
        if (kBonePoseSyncEnabled) CaptureAndSendLocalBones(localPlayer);

    }
    for (const auto& test : g_client->DrainNpcSpawnTests()) SpawnTestNpc(test);
    UpdateRemotePlayers();
    UpdateRemoteMissionMarkers();
    SyncLocalNpcs();
    static int npcMarkerTick=0; if((++npcMarkerTick % 6)==0) SyncLocalMissionMarkers();
}

void HookPedProcess(void* ped)
{
    if (g_originalPedProcess) g_originalPedProcess(ped);




    CPlayerPed* localPlayer = FindPlayerPed(-1);
    if (g_client && ped && reinterpret_cast<void*>(localPlayer) == ped)
        SendPendingLocalCarjackEvent();
    if (g_client && g_getBoundCentre && ped &&
        reinterpret_cast<void*>(localPlayer) == ped) {
        gtasa_v200_armv7::CVectorAbi centre{};
        g_getBoundCentre(ped, centre);
        static bool havePrevious = false;
        static gtasa_v200_armv7::CVectorAbi previous{};
        auto* localPed = static_cast<CPed*>(ped);
        float heading = localPed->m_placement.m_fHeading * 57.2957795f;
        if (const auto* matrix = localPed->GetMatrix()) {
            const auto& forward = matrix->GetForward();
            if (std::isfinite(forward.x) && std::isfinite(forward.y) &&
                (std::fabs(forward.x) > 0.0001f || std::fabs(forward.y) > 0.0001f))
                heading = std::atan2(-forward.x, forward.y) * 57.2957795f;
        }
        heading = std::fmod(heading, 360.0f);
        if (heading < 0.0f) heading += 360.0f;
    uint8_t animation = kMoveStill;
        if (havePrevious) {
            const float dx = centre.x - previous.x;
            const float dy = centre.y - previous.y;
            const float speed2 = dx * dx + dy * dy;
            if (speed2 > 0.0001f) {



                animation = speed2 > 0.01f ? kMoveRun : kMoveWalk;
            }
        }
        previous = centre;
        havePrevious = true;
        const auto modelId = *reinterpret_cast<const uint16_t*>(
            reinterpret_cast<uintptr_t>(ped) + gtasa_v200_armv7::kCPedModelIndexOffset);
        const auto weaponType = CurrentWeaponTypeForPed(static_cast<CPed*>(ped));
    const std::string specialName = StableSpecialNameForPed(static_cast<CPed*>(ped), modelId);
        g_client->SetLocalModel(modelId, specialName.c_str());
        g_client->SetLocalWeaponType(weaponType);
        const auto health = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, static_cast<CPed*>(ped)->GetHealth())));
        const auto armour = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, static_cast<CPed*>(ped)->m_fArmour)));
        g_client->SetLocalHealthArmour(health, armour);




        uint8_t localSeat = 0;
        CVehicle* localVehicle = GetSeatedVehicle(localPed, &localSeat);
        {



            static uint64_t lastStaleLogMs = 0;
            if (!localVehicle && localPed->m_pVehicle) {
                const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                if (nowMs - lastStaleLogMs > 1000) {
                    lastStaleLogMs = nowMs;
                    logger->Info("LOCAL_STALE_VEHICLE_PTR m_pVehicle=%p driverMatch=%d bInVehicle=%d pedState=%d -> sending on-foot position",
                                 static_cast<void*>(localPed->m_pVehicle),
                                 localPed->m_pVehicle->m_pDriver == localPed ? 1 : 0,
                                 localPed->bInVehicle ? 1 : 0,
                                 static_cast<int>(localPed->m_ePedState));
                }
            }
        }
        if (localVehicle) {
            const CVector& vehiclePosition = localVehicle->GetPosition();
            const CVector& vehicleVelocity=reinterpret_cast<CPhysical*>(localVehicle)->m_vecMoveSpeed;
            g_client->SetLocalTransform(vehiclePosition.x, vehiclePosition.y,
                                         vehiclePosition.z, HeadingDegrees(localVehicle),
                                         vehicleVelocity.x,vehicleVelocity.y,vehicleVelocity.z);
        } else {




            const CVector& worldPosition = localPed->GetPosition();
            const CVector& pedVelocity=reinterpret_cast<CPhysical*>(localPed)->m_vecMoveSpeed;
            g_client->SetLocalTransform(worldPosition.x, worldPosition.y,
                                         worldPosition.z, heading,
                                         pedVelocity.x,pedVelocity.y,pedVelocity.z);
        }
        g_client->SetLocalAnimationState(animation);
        static uint32_t animationSequence = 0;
        static AnimationProfile lastAnimationProfile{};
        AnimationProfile localAnimation = CaptureAnimationProfile(localPed, 0);
        CaptureLocalAimDirection(localPed, localAnimation);
        const bool semanticAnimationChanged = animationSequence == 0 ||
            localAnimation.locomotion != lastAnimationProfile.locomotion ||
            localAnimation.action != lastAnimationProfile.action ||
            localAnimation.flags != lastAnimationProfile.flags ||
            localAnimation.weaponState != lastAnimationProfile.weaponState ||
            localAnimation.animGroup != lastAnimationProfile.animGroup;
        if (semanticAnimationChanged) ++animationSequence;
        localAnimation.sequence = animationSequence;
        lastAnimationProfile = localAnimation;
        g_client->SetLocalAnimationProfile(localAnimation);
        AnimationAssociationState localAssociations[64]{};
        static LocalAssociationGate associationGate;
        static bool lastAssociationsSuppressed = false;
        const uint64_t associationNowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        const bool suppressAssociations = associationGate.ShouldSuppress(localVehicle, localPed->m_pVehicle, associationNowMs);
        if (lastAssociationsSuppressed && !suppressAssociations && localPed->m_pVehicle) {

            logger->Info("LOCAL_ASSOC_RESUMED_AFTER_EXIT m_pVehicle=%p bInVehicle=%d pedState=%d -> on-foot animation associations resumed",
                         static_cast<void*>(localPed->m_pVehicle), localPed->bInVehicle ? 1 : 0,
                         static_cast<int>(localPed->m_ePedState));
        }
        lastAssociationsSuppressed = suppressAssociations;
        g_client->SetLocalAnimationAssociations(
            suppressAssociations ? nullptr : localAssociations,
            suppressAssociations ? 0 : CaptureActiveAssociations(localPed, localAssociations));

        if (localVehicle) {
            const CVector& vp = localVehicle->GetPosition();
            const CVector& vv = reinterpret_cast<CPhysical*>(localVehicle)->m_vecMoveSpeed;
            const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(localVehicle));
            const uint8_t seat=localSeat;
            const uint16_t remoteVehicleId=FindRemoteVehicleNetworkId(localVehicle);
            if(remoteVehicleId && seat==0){
                const uint64_t now=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                auto& last=g_lastVehicleClaimMs[remoteVehicleId];
                if(now-last>500){
                    g_client->SendVehicleClaim(remoteVehicleId,seat,key);
                    last=now;
                }
            }
            g_client->SetLocalVehicle(key, localVehicle->m_nModelIndex,
                                      vp.x, vp.y, vp.z,
                                      HeadingDegrees(localVehicle),
                                      vv.x, vv.y, vv.z, localVehicle->m_fHealth, seat);
            g_client->SetLocalVehicleNetworkId(remoteVehicleId);
            CaptureLocalVehicleDetails(localVehicle);
        } else {
            g_client->SetLocalVehicleNetworkId(0);
            g_client->SetLocalVehicle(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }
    }


    if (g_client && g_saUtils && g_saUtils->IsGameInitialised()) {
        for (const auto& test : g_client->DrainNpcSpawnTests()) SpawnTestNpc(test);
        UpdateRemotePlayers();
    }
}

void AfterVehicleProcess(void* vehicle)
{
    if (!g_client || !vehicle) return;

    CPlayerPed* localPlayer = FindPlayerPed(-1);
    CVehicle* localVehicle = reinterpret_cast<CVehicle*>(vehicle);
    if (localPlayer && GetSeatedVehicle(localPlayer) == localVehicle) {
        const CVector& vp = localVehicle->GetPosition();
        const CVector& vv = reinterpret_cast<CPhysical*>(localVehicle)->m_vecMoveSpeed;
        const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(localVehicle));
        g_client->SetLocalVehicle(key, localVehicle->m_nModelIndex,
                                  vp.x, vp.y, vp.z,
                                  HeadingDegrees(localVehicle),
                                  vv.x, vv.y, vv.z, localVehicle->m_fHealth, ([&](){ uint8_t seat=0; if(localVehicle->m_pDriver==localPlayer) return seat; for(uint8_t si=0;si<8;++si) if(localVehicle->m_apPassengers[si]==localPlayer) return (uint8_t)(si+1); return seat; })());
        CaptureLocalVehicleDetails(localVehicle);
    } else {
        ReapplyRemoteVehicleHeading(localVehicle);
    }
    ReapplyRemoteAimsForVehicle(localVehicle);
}

void HookVehicleProcess(void* vehicle)
{
    if (g_originalVehicleProcess) g_originalVehicleProcess(vehicle);
    AfterVehicleProcess(vehicle);
}

void HookPlaneProcess(void* vehicle)
{
    if (g_originalPlaneProcess) g_originalPlaneProcess(vehicle);
    AfterVehicleProcess(vehicle);
}

void HookHeliProcess(void* vehicle)
{
    if (g_originalHeliProcess) g_originalHeliProcess(vehicle);
    AfterVehicleProcess(vehicle);
}

void HookBikeProcess(void* vehicle)
{
    if (g_originalBikeProcess) g_originalBikeProcess(vehicle);
    ReapplyRemoteVehicleHeading(static_cast<CVehicle*>(vehicle));
    ReapplyRemoteAimsForVehicle(static_cast<CVehicle*>(vehicle));
}

void HookBoatProcess(void* vehicle)
{
    if (g_originalBoatProcess) g_originalBoatProcess(vehicle);
    ReapplyRemoteVehicleHeading(static_cast<CVehicle*>(vehicle));
    ReapplyRemoteAimsForVehicle(static_cast<CVehicle*>(vehicle));
}

}

void DrawRemoteWeaponIcons()
{
    DrawRemoteWeaponIconsInternal();
}

void RenderRemotePlayerNameTags(ImDrawList* drawList)
{
    RenderRemotePlayerNameTagsInternal(drawList);
}

void InstallVehicleClassHook(void* gameHandle, const char* name, const char* symbol, void* hook, void** original)
{
    const uintptr_t target = gameHandle ? aml->GetSym(gameHandle, symbol) : 0;
    if (!target) {
        logger->Info("%s::ProcessControl symbol not found; this class keeps the default sync path", name);
        return;
    }
    const bool ok = aml->Hook(reinterpret_cast<void*>(target), hook, original);
    logger->Info("%s::ProcessControl hook %s", name, ok ? "installed" : "failed");
}

void InstallGameHooks(uintptr_t gameLibrary, CoopClient& client, ISAUtils* saUtils)
{
    CutsceneAnimLogReset();
    CutsceneAnimLog("CUTSCENE_LOG_PATH primary=/sdcard/cutscenes anim log fallback=/data/user/0/com.rockstargames.gtasaclone/files/cutscenes anim log");
    g_game = gameLibrary;
    void* gameHandle = aml->GetLibHandle("libGTASA.so");
    auto resolveGameSymbol = [&](const char* name) -> uintptr_t {
        uintptr_t result = gameHandle ? aml->GetSym(gameHandle, name) : 0;
        if (!result) result = aml->GetSym(g_game, name);
        return result;
    };
    g_cutsceneManagerRunning = reinterpret_cast<bool*>(resolveGameSymbol("_ZN12CCutsceneMgr10ms_runningE"));
    g_cutsceneManagerProcessing = reinterpret_cast<bool*>(resolveGameSymbol("_ZN12CCutsceneMgr21ms_cutsceneProcessingE"));
    g_cutsceneManagerObjects = reinterpret_cast<CEntity**>(resolveGameSymbol("_ZN12CCutsceneMgr19ms_pCutsceneObjectsE"));
    g_cutsceneManagerObjectCount = reinterpret_cast<int32_t*>(resolveGameSymbol("_ZN12CCutsceneMgr18ms_numCutsceneObjsE"));
    g_cutsceneManagerName = reinterpret_cast<char*>(resolveGameSymbol("_ZN12CCutsceneMgr15ms_cutsceneNameE"));
    g_cutsceneLoadStatus = reinterpret_cast<int32_t*>(resolveGameSymbol("_ZN12CCutsceneMgr20ms_cutsceneLoadStatusE"));
    g_cutsceneAnimLoaded = reinterpret_cast<bool*>(resolveGameSymbol("_ZN12CCutsceneMgr13ms_animLoadedE"));
    g_getCutsceneTimeMs = reinterpret_cast<CutsceneTimeFn>(resolveGameSymbol("_ZN12CCutsceneMgr29GetCutsceneTimeInMillesecondsEv"));
    g_animManagerNumBlocks = reinterpret_cast<int32_t*>(resolveGameSymbol("_ZN12CAnimManager16ms_numAnimBlocksE"));
    g_animManagerNumAnimations = reinterpret_cast<int32_t*>(resolveGameSymbol("_ZN12CAnimManager16ms_numAnimationsE"));
    g_getAnimationBlockIndex = reinterpret_cast<GetAnimationBlockIndexFn>(
        resolveGameSymbol("_ZN12CAnimManager22GetAnimationBlockIndexEPKc"));
    g_getAnimationBlock = reinterpret_cast<GetAnimationBlockFn>(
        resolveGameSymbol("_ZN12CAnimManager17GetAnimationBlockEPKc"));
    g_getAnimation = reinterpret_cast<GetAnimationFn>(
        resolveGameSymbol("_ZN12CAnimManager12GetAnimationEPKcPK10CAnimBlock"));
    g_rwStreamOpen = reinterpret_cast<RwStreamOpenFn>(resolveGameSymbol("_Z12RwStreamOpen12RwStreamType18RwStreamAccessTypePKv"));
    g_rwStreamClose = reinterpret_cast<RwStreamCloseFn>(resolveGameSymbol("_Z13RwStreamCloseP8RwStreamPv"));
    g_loadAnimFile = reinterpret_cast<LoadAnimFileFn>(resolveGameSymbol("_ZN12CAnimManager12LoadAnimFileEP8RwStreambPA32_Kc"));
    g_cutsceneManagerSymbolsReady = g_cutsceneManagerRunning && g_cutsceneManagerObjects && g_cutsceneManagerObjectCount;
    logger->Info("Cutscene partner symbols: running=%p processing=%p objects=%p count=%p name=%p feature=%s",
                 static_cast<void*>(g_cutsceneManagerRunning), static_cast<void*>(g_cutsceneManagerProcessing),
                 static_cast<void*>(g_cutsceneManagerObjects), static_cast<void*>(g_cutsceneManagerObjectCount),
                 static_cast<void*>(g_cutsceneManagerName),
                 g_cutsceneManagerSymbolsReady ? "ready" : "disabled");
    logger->Info("Cutscene mocap native bridge: streamOpen=%d streamClose=%d loadAnim=%d time=%d blockCount=%d animCount=%d loadState=%d animLoaded=%d",
                 g_rwStreamOpen != nullptr, g_rwStreamClose != nullptr, g_loadAnimFile != nullptr,
                 g_getCutsceneTimeMs != nullptr, g_animManagerNumBlocks != nullptr,
                 g_animManagerNumAnimations != nullptr, g_cutsceneLoadStatus != nullptr,
                 g_cutsceneAnimLoaded != nullptr);
    if (gameHandle) {
        g_getFirstAssociation = reinterpret_cast<GetFirstAssociationFn>(aml->GetSym(gameHandle, "_Z35RpAnimBlendClumpGetFirstAssociationP7RpClump"));
        g_getNextAssociation = reinterpret_cast<GetNextAssociationFn>(aml->GetSym(gameHandle, "_Z29RpAnimBlendGetNextAssociationP21CAnimBlendAssociation"));
        g_removeAllAssociations = reinterpret_cast<RemoveAllAssociationsFn>(aml->GetSym(gameHandle, "_Z37RpAnimBlendClumpRemoveAllAssociationsP7RpClump"));
        g_blendAnimation = reinterpret_cast<BlendAnimationFn>(aml->GetSym(gameHandle, "_ZN12CAnimManager14BlendAnimationEP7RpClump12AssocGroupId11AnimationIdf"));
        g_blendAnimationByHierarchy = reinterpret_cast<BlendAnimationHierarchyFn>(aml->GetSym(gameHandle, "_ZN12CAnimManager14BlendAnimationEP7RpClumpP19CAnimBlendHierarchyif"));
        g_getAnimBlockName = reinterpret_cast<GetAnimBlockNameFn>(aml->GetSym(gameHandle, "_ZN12CAnimManager16GetAnimBlockNameEi"));
        g_findGroundZFor3DCoord = reinterpret_cast<FindGroundZFor3DCoordFn>(
            resolveGameSymbol("_ZN6CWorld21FindGroundZFor3DCoordEfffPbPP7CEntity"));
        logger->Info("Cutscene partner ground query: %s", g_findGroundZFor3DCoord ? "available" : "unavailable");
        g_setAssocBlend = reinterpret_cast<SetAssocBlendFn>(aml->GetSym(gameHandle, "_ZN21CAnimBlendAssociation8SetBlendEff"));
        WeaponFileLog("ANIM_SYNC mixed path: first=%d next=%d removeAll=%d blendByGroup=%d blendByHierarchy=%d setBlend=%d",
                      g_getFirstAssociation != nullptr, g_getNextAssociation != nullptr,
                      g_removeAllAssociations != nullptr, g_blendAnimation != nullptr,
                      g_blendAnimationByHierarchy != nullptr, g_setAssocBlend != nullptr);
        WeaponFileLog("ANIM_SYNC resource gate: getBlockName=%d getBlockIndex=%d getBlock=%d getAnimation=%d blendHierarchy=%d modelBase=%d",
                      g_getAnimBlockName != nullptr, g_getAnimationBlockIndex != nullptr,
                      g_getAnimationBlock != nullptr, g_getAnimation != nullptr,
                      g_blendAnimationByHierarchy != nullptr, kAnimationStreamingModelBase);
        g_setAssocBlend = reinterpret_cast<SetAssocBlendFn>(aml->GetSym(gameHandle, "_ZN21CAnimBlendAssociation8SetBlendEff"));


        g_doGunFlash = reinterpret_cast<DoGunFlashFn>(aml->GetSym(gameHandle, "_ZN4CPed10DoGunFlashEib"));
        if (!g_doGunFlash) logger->Error("CPed::DoGunFlash symbol not found; remote muzzle flashes disabled");
        g_getWeaponInfoForIcon = reinterpret_cast<GetWeaponInfoForIconFn>(aml->GetSym(gameHandle, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponTypea"));
        g_requestWeaponIconModel = reinterpret_cast<RequestWeaponIconModelFn>(aml->GetSym(gameHandle, "_ZN10CStreaming12RequestModelEii"));
        g_loadWeaponIconModels = reinterpret_cast<LoadWeaponIconModelsFn>(aml->GetSym(gameHandle, "_ZN10CStreaming22LoadAllRequestedModelsEb"));
        g_drawWeaponIcon = reinterpret_cast<DrawWeaponIconFn>(aml->GetSym(gameHandle, "_ZN17CWidgetPlayerInfo14DrawWeaponIconEP4CPed5CRectf"));
        logger->Info("Weapon icon bridge: info=%d request=%d load=%d draw=%d",
                     g_getWeaponInfoForIcon != nullptr, g_requestWeaponIconModel != nullptr,
                     g_loadWeaponIconModels != nullptr, g_drawWeaponIcon != nullptr);
    }
    g_client = &client;
    g_saUtils = saUtils;
    uintptr_t specialCharRequest = reinterpret_cast<uintptr_t>(CStreaming::RequestSpecialChar);
    if (!specialCharRequest)
        specialCharRequest = aml->GetSym(g_game, "_ZN10CStreaming18RequestSpecialCharEiPKci");
    const bool specialCharHooked = specialCharRequest && aml->Hook(
        reinterpret_cast<void*>(specialCharRequest), reinterpret_cast<void*>(HookRequestSpecialChar),
        reinterpret_cast<void**>(&g_originalRequestSpecialChar));
    if (specialCharHooked) logger->Info("CStreaming::RequestSpecialChar identity hook installed");
    else logger->Error("CStreaming::RequestSpecialChar hook unavailable; unnamed special player/NPC models will safely fall back to CJ");
    const auto specialModelRequest = resolveGameSymbol("_ZN10CStreaming19RequestSpecialModelEiPKci");
    const bool specialModelHooked = specialModelRequest && aml->Hook(reinterpret_cast<void*>(specialModelRequest),
        reinterpret_cast<void*>(HookRequestSpecialModel), reinterpret_cast<void**>(&g_originalRequestSpecialModel));
    if (specialModelHooked) logger->Info("CStreaming::RequestSpecialModel identity hook installed (mission/story model names tracked)");
    else logger->Error("CStreaming::RequestSpecialModel hook unavailable; special characters may not have names");
    Events::gameProcessEvent += OnGameProcess;
    const auto getBoundCentreSymbol = aml->GetSym(g_game, "_ZNK7CEntity14GetBoundCentreER7CVector");
    g_giveWeapon = reinterpret_cast<GiveWeaponFn>(aml->GetSym(g_game, "_ZN4CPed10GiveWeaponE11eWeaponTypejb"));
    g_setCurrentWeapon = reinterpret_cast<SetCurrentWeaponFn>(aml->GetSym(g_game, "_ZN4CPed16SetCurrentWeaponE11eWeaponType"));
    if (g_giveWeapon && g_setCurrentWeapon) logger->Info("Verified CPed weapon methods resolved");
    else logger->Error("CPed weapon methods unavailable; weapon visuals disabled");
    g_getBoundCentre = reinterpret_cast<gtasa_v200_armv7::EntityGetBoundCentreFn>(
        getBoundCentreSymbol ? getBoundCentreSymbol : g_game + gtasa_v200_armv7::kCEntityGetBoundCentre);
    g_setMoveState = reinterpret_cast<gtasa_v200_armv7::PedSetMoveStateFn>(g_game + gtasa_v200_armv7::kCPedSetMoveState);
    g_entityTeleport = reinterpret_cast<gtasa_v200_armv7::EntityTeleportFn>(g_game + gtasa_v200_armv7::kCPedTeleport);
    g_setMoveAnim = reinterpret_cast<gtasa_v200_armv7::PedSetMoveAnimFn>(g_game + gtasa_v200_armv7::kCPedSetMoveAnim);

    const auto ped = reinterpret_cast<gtasa_v200_armv7::ProcessControlFn>(g_game + gtasa_v200_armv7::kCPlayerPedProcessControl);
    uintptr_t genericPedAddress = gameHandle ? aml->GetSym(gameHandle, "_ZN4CPed14ProcessControlEv") : 0;
    if (!genericPedAddress) genericPedAddress = g_game + gtasa_v200_armv7::kCPedProcessControl;
    const bool genericPedAddressVerified = HasVerifiedCPedProcessControlPrologue(genericPedAddress);
    const auto genericPed = reinterpret_cast<gtasa_v200_armv7::ProcessControlFn>(genericPedAddress);

    if (g_saUtils) {
        coop_cutscene_mocap::SetGameSearchRoots(
            aml ? aml->GetDataPath() : nullptr,
            aml ? aml->GetAndroidDataPath() : nullptr,
            aml ? aml->GetInternalPath() : nullptr,
            aml ? aml->GetAndroidDataRootPath() : nullptr);
        coop_cutscene_mocap::StartBuildAsync();
        g_cutsceneMocapBuildStarted = true;
        logger->Info("CUTSCENE_MOCAP_BUILD_QUEUED: reading installed anim/cuts.img on worker thread");
        CutsceneAnimLog("CUTSCENE_MOCAP_BUILD_QUEUED source=anim/cuts.img");
        g_saUtils->AddPlayerUpdateListener(OnSaUtilsPlayerUpdate, true);
        logger->Info("SAUtils post-player-update NPC listener installed");
    }

    const auto vehicle = reinterpret_cast<gtasa_v200_armv7::ProcessControlFn>(
        g_game + gtasa_v200_armv7::kCAutomobileProcessControl);
    const auto fireInstantHit = reinterpret_cast<void*>(g_game + gtasa_v200_armv7::kCWeaponFireInstantHit);
    const auto doBulletImpact = resolveGameSymbol("_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPointi");
    const auto generateDamageEvent = resolveGameSymbol("_ZN7CWeapon19GenerateDamageEventEP4CPedP7CEntity11eWeaponTypei14ePedPieceTypesi");
    g_resolvedGenerateDamageEvent = reinterpret_cast<GenerateDamageEventFn>(generateDamageEvent);
    const uintptr_t fightTaskVtable = resolveGameSymbol("_ZTV16CTaskSimpleFight");
    g_fightTaskVtableAddressPoint = fightTaskVtable ? fightTaskVtable + sizeof(uintptr_t) * 2 : 0;
    const auto addProjectile = resolveGameSymbol("_ZN15CProjectileInfo13AddProjectileEP7CEntity11eWeaponType7CVectorfPS3_S1_");
    const auto addExplosion = resolveGameSymbol("_ZN10CExplosion12AddExplosionEP7CEntityS1_14eExplosionType7CVectorjhfh");
    g_weaponFire = reinterpret_cast<WeaponFireFn>(resolveGameSymbol("_ZN7CWeapon4FireEP7CEntityP7CVectorS3_S1_S3_S3_"));
    const auto enterCarAsDriverCtor = resolveGameSymbol("_ZN28CTaskComplexEnterCarAsDriverC1EP8CVehicle");
    g_resolvedEnterCarAsDriverCtor = reinterpret_cast<EnterCarAsDriverCtorFn>(enterCarAsDriverCtor);
    g_taskAllocate = reinterpret_cast<TaskAllocateFn>(resolveGameSymbol("_ZN5CTasknwEj"));
    g_taskManagerSetTask = reinterpret_cast<TaskManagerSetTaskFn>(resolveGameSymbol("_ZN12CTaskManager7SetTaskEP5CTaskib"));
    const bool enterCarAsDriverHooked = enterCarAsDriverCtor && aml->Hook(
        reinterpret_cast<void*>(enterCarAsDriverCtor),
        reinterpret_cast<void*>(HookEnterCarAsDriverConstructor),
        reinterpret_cast<void**>(&g_originalEnterCarAsDriverCtor));

    const bool pedHooked = aml->Hook(reinterpret_cast<void*>(ped),
        reinterpret_cast<void*>(HookPedProcess), reinterpret_cast<void**>(&g_originalPedProcess));
    const bool genericPedHooked = genericPedAddressVerified && aml->Hook(reinterpret_cast<void*>(genericPed),
        reinterpret_cast<void*>(HookGenericPedProcess), reinterpret_cast<void**>(&g_originalGenericPedProcess));
    const bool vehicleHooked = aml->Hook(reinterpret_cast<void*>(vehicle),
        reinterpret_cast<void*>(HookVehicleProcess), reinterpret_cast<void**>(&g_originalVehicleProcess));
    const uintptr_t worldProcessSymbol = resolveGameSymbol("_ZN6CWorld7ProcessEv");
    const void* worldProcess = reinterpret_cast<void*>(worldProcessSymbol ? worldProcessSymbol :
        (g_game + gtasa_v200_armv7::kCWorldProcess));
    const bool worldProcessHooked = aml->Hook(const_cast<void*>(worldProcess),
        reinterpret_cast<void*>(HookWorldProcess), reinterpret_cast<void**>(&g_originalWorldProcess));
    const auto planeProcess = resolveGameSymbol("_ZN6CPlane14ProcessControlEv");
    const auto heliProcess = resolveGameSymbol("_ZN5CHeli14ProcessControlEv");
    const bool planeProcessHooked = planeProcess && aml->Hook(reinterpret_cast<void*>(planeProcess),
        reinterpret_cast<void*>(HookPlaneProcess), reinterpret_cast<void**>(&g_originalPlaneProcess));
    const bool heliProcessHooked = heliProcess && aml->Hook(reinterpret_cast<void*>(heliProcess),
        reinterpret_cast<void*>(HookHeliProcess), reinterpret_cast<void**>(&g_originalHeliProcess));
    InstallVehicleClassHook(gameHandle, "CBike", "_ZN5CBike14ProcessControlEv",
                            reinterpret_cast<void*>(HookBikeProcess), reinterpret_cast<void**>(&g_originalBikeProcess));
    InstallVehicleClassHook(gameHandle, "CBoat", "_ZN5CBoat14ProcessControlEv",
                            reinterpret_cast<void*>(HookBoatProcess), reinterpret_cast<void**>(&g_originalBoatProcess));
    const bool fireHooked = aml->Hook(fireInstantHit,
        reinterpret_cast<void*>(HookFireInstantHit), reinterpret_cast<void**>(&g_originalFireInstantHit));
    const bool impactHooked = doBulletImpact && aml->Hook(reinterpret_cast<void*>(doBulletImpact),reinterpret_cast<void*>(HookDoBulletImpact),reinterpret_cast<void**>(&g_originalDoBulletImpact));
    const bool damageEventHooked = generateDamageEvent && aml->Hook(reinterpret_cast<void*>(generateDamageEvent),
        reinterpret_cast<void*>(HookGenerateDamageEvent), reinterpret_cast<void**>(&g_originalGenerateDamageEvent));
    if (!damageEventHooked) g_originalGenerateDamageEvent = g_resolvedGenerateDamageEvent;
    const bool projectileHooked = addProjectile && aml->Hook(reinterpret_cast<void*>(addProjectile),reinterpret_cast<void*>(HookAddProjectile),reinterpret_cast<void**>(&g_originalAddProjectile));
    const bool explosionHooked = addExplosion && aml->Hook(reinterpret_cast<void*>(addExplosion),reinterpret_cast<void*>(HookAddExplosion),reinterpret_cast<void**>(&g_originalAddExplosion));
    if (projectileHooked && g_originalAddProjectile) {
        g_projectileReplayFn = g_originalAddProjectile;
    } else if (addProjectile) {
        g_projectileReplayFn = reinterpret_cast<AddProjectileFn>(addProjectile);
        WeaponFileLog("PROJECTILE_HOOK_FALLBACK replay-native=%p capture-hook=off", reinterpret_cast<void*>(addProjectile));
    }
    g_shotImpactHooked = impactHooked;
    logger->Info("CWeapon::GenerateDamageEvent hook: %s address=%p fightVtable=%p",
                 damageEventHooked ? "installed" : "unavailable",
                 reinterpret_cast<void*>(generateDamageEvent), reinterpret_cast<void*>(g_fightTaskVtableAddressPoint));

    logger->Info("CTaskComplexEnterCarAsDriver constructor hook: %s address=%p",
                 enterCarAsDriverHooked ? "installed" : "unavailable",
                 reinterpret_cast<void*>(enterCarAsDriverCtor));
    logger->Info("Remote driver-entry native task dependencies: ctor=%p taskAlloc=%p taskManagerSetTask=%p",
                 reinterpret_cast<void*>(g_resolvedEnterCarAsDriverCtor),
                 reinterpret_cast<void*>(g_taskAllocate),
                 reinterpret_cast<void*>(g_taskManagerSetTask));
    if (pedHooked) logger->Info("CPlayerPed::ProcessControl hook installed");
    else logger->Error("CPlayerPed::ProcessControl hook failed");
    if (genericPedHooked) logger->Info("CPed::ProcessControl post-aim hook installed at %p (verified Android prologue)",
                                       reinterpret_cast<void*>(genericPedAddress));
    else logger->Error("CPed::ProcessControl hook unavailable (prologue mismatch or hook failure); native remote aim task remains enabled");
    if (vehicleHooked) logger->Info("CAutomobile::ProcessControl hook installed");
    else logger->Error("CAutomobile::ProcessControl hook failed");
    if (worldProcessHooked) logger->Info("CWorld::Process post-physics vehicle correction hook installed source=%s address=%p",
        worldProcessSymbol ? "symbol" : "GTA-SA-2.00-ARMv7-RVA", const_cast<void*>(worldProcess));
    else logger->Error("CWorld::Process post-physics correction hook failed; native vehicle physics may overwrite remote transforms");
    logger->Info("Aircraft process hooks: plane=%s heli=%s", planeProcessHooked ? "on" : "off", heliProcessHooked ? "on" : "off");
    if (fireHooked) logger->Info("CWeapon::FireInstantHit hook installed");
    else logger->Error("CWeapon::FireInstantHit hook failed");
    logger->Info("Shot sync hooks: impact=%s projectile-capture=%s projectile-replay=%s bullet-replay=%s", impactHooked ? "on" : "off", projectileHooked ? "on" : "off", g_projectileReplayFn ? "on" : "off", g_weaponFire ? "CWeapon::Fire" : (g_originalFireInstantHit ? "FireInstantHit" : "unavailable"));
    logger->Info("Explosion sync hook: %s native=%p", explosionHooked ? "on" : "off", reinterpret_cast<void*>(addExplosion));
    if (!projectileHooked) logger->Error("CProjectileInfo::AddProjectile capture hook missing (symbol=%p); outbound RPG/vehicle projectile capture is unavailable", reinterpret_cast<void*>(addProjectile));
    else logger->Info("CProjectileInfo::AddProjectile capture hook installed at %p", reinterpret_cast<void*>(addProjectile));

    InstallRemotePedAiHooks();
}

void SetRemoteNameTagsVisible(bool visible)
{
    g_remoteNameTagsVisible = visible;
}
void SetRemoteNameTagScale(float scale)
{
    g_remoteNameTagScale = std::max(0.5f, std::min(2.0f, scale));
}
void SetRemoteHealthBarsVisible(bool visible)
{
    g_remoteHealthBarsVisible = visible;
}
