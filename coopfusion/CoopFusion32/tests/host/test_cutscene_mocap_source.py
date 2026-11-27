"""Source-level checks for direct cuts.img loading and partner mocap playback."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MOCAP = (ROOT / "coopfusion/world/cutscene_mocap.cpp").read_text()
HEADER = (ROOT / "coopfusion/world/cutscene_mocap.h").read_text()
HOOKS = (ROOT / "coopfusion/world/game_hooks.cpp").read_text()
ANDROID_MK = (ROOT / "Android.mk").read_text()
CUTSCENE_LOG = (ROOT / "coopfusion/diagnostics/cutscene_anim_log.cpp").read_text()
CUTSCENE_LOG_H = (ROOT / "coopfusion/diagnostics/cutscene_anim_log.h").read_text()


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"PASS: {message}")


check('"VER2"' in MOCAP and "kSectorSize = 2048" in MOCAP and
      "* 32u" in MOCAP and "offsetSector" in MOCAP,
      "Android parser reads the GTA VER2 IMG directory using 32-byte entries and 2048-byte sectors")
check('anim/cuts.img' in MOCAP and "ReadProcessPackage" in MOCAP and
      "Android/data/" in MOCAP and "FindCutsImg" in MOCAP and
      '"/data/anim/cuts.img"' in MOCAP and '"/files/data/anim/cuts.img"' in MOCAP and
      "SetGameSearchRoots" in HEADER,
      "cuts.img lookup covers Android's data/anim path plus AML-provided external and private game roots")
check('"csplay"' in MOCAP and '"COOPCS\\0\\0"' in MOCAP and
      '"coopcs%02u"' in MOCAP and '"coopwk%02u"' in MOCAP,
      "selected csplay cutscene tracks are converted to named COOPCS standing and walking animations")
check("BodyBone" in MOCAP and "Normalize(bone.keys, kRootHeight)" in MOCAP and
      "Normalize(bone.keys, kWalkHeight)" in MOCAP and "speed = std::hypot" in MOCAP,
      "the generator preserves PC body-bone filtering, root normalization and walk-speed extraction")
check("std::thread(BuildWorker).detach()" in MOCAP and
      "BuildFromImg(path)" in MOCAP and "GetAnimPackData" in HEADER,
      "the multi-megabyte archive parsing/build is kept off the game thread and caches the generated ANPK")
check("RwStreamMemory" in HOOKS and "rwSTREAMMEMORY" in HOOKS and
      "g_loadAnimFile(stream, true, noUncompressedAnimNames)" in HOOKS and
      '"_ZN12CAnimManager12LoadAnimFileEP8RwStreambPA32_Kc"' in HOOKS,
      "Android RenderWare memory streams feed the generated ANPK to the real CAnimManager::LoadAnimFile")
check('"_Z12RwStreamOpen12RwStreamType18RwStreamAccessTypePKv"' in HOOKS and
      '"_Z13RwStreamCloseP8RwStreamPv"' in HOOKS and
      '"_ZN12CAnimManager22GetAnimationBlockIndexEPKc"' in HOOKS,
      "stream and animation-manager functions are resolved from the target game binary exports")
check('"COOPCS"' in HOOKS and "StartCutscenePartnerMocapClip" in HOOKS and
      "GetStandingClip(index)" in HOOKS and "GetWalkClip(mocapGait)" in HOOKS and
      "kScmSetCharAnimSpeed" in HOOKS,
      "partner idle/reaction clips and gait animations are selected from the generated bank with speed matching")
check("GetCutsceneTimeInMilleseconds" in HOOKS and
      "coop_cutscene_mocap::StartBuildAsync()" in HOOKS and
      "aml->GetDataPath()" in HOOKS and "aml->GetAndroidDataPath()" in HOOKS,
      "the port starts extraction automatically using AML game roots and GTA cutscene-local time")
check("CUTSCENE_PARTNER_ANIM_WAIT" in HOOKS and "CUTSCENE_PARTNER_ANIM_TASK_ISSUED" in HOOKS and
      "CUTSCENE_PARTNER_ANIM_PLAYING" in HOOKS,
      "runtime logs now distinguish unavailable animation blocks, issued tasks and animations confirmed playing")
check("actionBlockReady" in HOOKS and "sceneActionPending" in HOOKS and
      "runtime.lastSceneAction = actionKey" in HOOKS,
      "an unavailable scene-specific animation block no longer suppresses the partner's fallback idle")
check("all-cpp-files-under,coopfusion/world" in ANDROID_MK and
      "all-cpp-files-under,coopfusion/diagnostics" in ANDROID_MK,
      "Android.mk automatically includes the direct reader and standalone diagnostics")
check('"/sdcard/cutscenes anim log"' in CUTSCENE_LOG and
      '"/storage/emulated/0/cutscenes anim log"' in CUTSCENE_LOG and
      '"/data/user/0/com.rockstargames.gtasaclone/files/cutscenes anim log"' in CUTSCENE_LOG and
      "CutsceneAnimLogReset" in CUTSCENE_LOG_H and "CUTSCENE_LOG_PATH" in HOOKS,
      "cutscene diagnostics are written to /sdcard/cutscenes anim log with an Android storage fallback")
check("CutsceneAnimLog(\"CUTSCENE_PARTNER_ANIM_TASK_ISSUED" in HOOKS and
      "CutsceneAnimLog(\"CUTSCENE_PARTNER_ANIM_PLAYING" in HOOKS and
      "CutsceneAnimLog(\"CUTSCENE_PARTNER_SCENE_ACTION" in HOOKS and
      "CutsceneAnimLog(\"CUTSCENE_MOCAP_NOT_READY" in HOOKS,
      "standalone log records mocap initialization, partner task issue, scene actions and confirmed playback")
check("DetachPedFromVehicleWithoutScript" not in HOOKS and
      'static const SCRIPT_COMMAND kScmRemoveCharFromCarMaintainPosition = { 0x09C9, "i" };' in HOOKS and
      HOOKS.count("ScriptCommand(&kScmRemoveCharFromCarMaintainPosition") == 2,
      "reference carjack/seat reconciliation path is restored, including its original 09C9 detach route")
check("occupantAssociationValid" not in HOOKS and
      "occupantPoolValid" not in HOOKS and
      "StartRemoteDriverEntryTask(entrant, vehicle)" in HOOKS,
      "remote native driver-entry task is not blocked by transient occupant association state")

print("13 cutscene mocap/source checks passed, 0 failed")
