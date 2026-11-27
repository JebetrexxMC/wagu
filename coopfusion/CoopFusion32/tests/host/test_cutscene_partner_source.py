"""Source-level regression checks for the Android cutscene companion port."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "coopfusion" / "world" / "game_hooks.cpp").read_text()
LOGIC = (ROOT / "coopfusion" / "world" / "cutscene_partner_logic.h").read_text()


def function_body(name: str) -> str:
    marker = f"{name}("
    search_from = 0
    while True:
        pos = SOURCE.find(marker, search_from)
        assert pos >= 0, f"function not found: {name}"
        open_paren = SOURCE.find("(", pos)
        depth_paren = 0
        close_paren = -1
        for i in range(open_paren, len(SOURCE)):
            if SOURCE[i] == "(":
                depth_paren += 1
            elif SOURCE[i] == ")":
                depth_paren -= 1
                if depth_paren == 0:
                    close_paren = i
                    break
        after = close_paren + 1
        while after < len(SOURCE) and SOURCE[after].isspace():
            after += 1
        if close_paren >= 0 and after < len(SOURCE) and SOURCE[after] == "{":
            start = after
            depth = 0
            for i in range(start, len(SOURCE)):
                if SOURCE[i] == "{":
                    depth += 1
                elif SOURCE[i] == "}":
                    depth -= 1
                    if depth == 0:
                        return SOURCE[start + 1:i]
            raise AssertionError(f"unterminated function: {name}")
        search_from = pos + len(marker)


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"PASS: {message}")


find_cj = function_body("FindLocalCutsceneCj")
release = function_body("ReleaseRemoteCutscenePartner")
update_scene = function_body("UpdateCutscenePartners")
update_players = function_body("UpdateRemotePlayers")
shot_update = function_body("UpdateCutsceneShotState")
nearest_actor = function_body("FindNearestCutsceneActor")
block_ready = function_body("EnsureCutsceneAnimationBlockReady")
start_animation = function_body("StartCutscenePartnerAnimation")
start_idle = function_body("StartCutscenePartnerIdle")
start_fidget = function_body("StartCutscenePartnerFidget")
isolate_animation = function_body("ClearCutscenePartnerInheritedAnimation")
post_process = function_body("HookGenericPedProcess")
install = function_body("InstallGameHooks")

check('"_ZN12CCutsceneMgr10ms_runningE"' in install and
      '"_ZN12CCutsceneMgr21ms_cutsceneProcessingE"' in install and
      '"_ZN12CCutsceneMgr19ms_pCutsceneObjectsE"' in install and
      '"_ZN12CCutsceneMgr18ms_numCutsceneObjsE"' in install,
      "Android CCutsceneMgr data is resolved by exported symbols, not copied PC addresses")
check("std::min<int32_t>(*g_cutsceneManagerObjectCount, 50)" in find_cj and
      "object->m_nModelIndex == 1" in find_cj,
      "cutscene actor scan is bounded to 50 entries and selects csplay model index 1")
check("kScmClearCharTasksImmediately" in update_scene and
      "kScmFreezeCharPosition" in update_scene and
      "kScmSetCurrentCharWeapon" in update_scene,
      "entry clears remote tasks, empties the weapon and freezes the remote ped")
check("kScmSetCharCoordinates" in update_scene and
      "kScmSetCharHeading" in update_scene and
      "cutsceneCj->m_nAreaCode" in update_scene,
      "remote partner follows the cutscene actor transform and area")
check("g_findGroundZFor3DCoord" in SOURCE and
      "FindGroundZFor3DCoordEfffPbPP7CEntity" in install and
      "actorZ + 6.0f" in function_body("ResolveCutscenePartnerGround"),
      "partner feet are placed using the Android GTA world-floor collision query")
check("kScmTaskPlayAnim" in start_animation and
      '"DEALER_IDLE", "DEALER"' in start_idle and
      "runtime.animationStage = 2" in start_animation,
      "partner starts the looped streamed DEALER/DEALER_IDLE pose after its placement frame")
check("kScmIsCharPlayingAnim" in update_scene and
      "CUTSCENE_PARTNER_ANIM_FINISHED" in update_scene and
      "CUTSCENE_PARTNER_ANIM_PLAYING" in update_scene and
      "runtime.activeAnimation.c_str()" in update_scene,
      "the active base/fidget animation is checked and recovered when interrupted or finished")
check("TheCamera.GetActiveCam()" in shot_update and
      "m_vecSource" in shot_update and "m_vecFront" in shot_update and
      "UpdateShotTracker" in shot_update and "CUTSCENE_PARTNER_SHOT" in shot_update,
      "mobile camera discontinuities create logged shot IDs without treating smooth pans as cuts")
check("CameraScreenRight" in shot_update and
      "placementRight = g_cutsceneShotRight" in update_scene,
      "each camera shot chooses a camera-relative side for the partner instead of a fixed CJ-relative side")
check("FindNearestCutsceneActor" in update_scene and
      "runtime.facingHeading" in update_scene and
      "HeadingToward" in LOGIC and "object->IsPed()" in nearest_actor,
      "partner faces the nearest real cutscene actor and falls back to CJ")
check('"DEALER_IDLE", "DEALER"' in start_idle and
      'true, false, nowMs, 0, 4.1f' in start_idle and
      'EnsureCutsceneAnimationBlockReady("DEALER")' in start_idle and
      'g_getAnimation("DEALER_IDLE", dealerBlock)' in start_idle and
      '"roadcross_female"' not in start_idle,
      "the partner's default pose is the looped streamed DEALER/DEALER_IDLE animation, not a PED-bank clip")
check("g_blendAnimationByHierarchy" in isolate_animation and
      'g_getAnimation("DEALER_IDLE", dealerBlock)' in isolate_animation and
      'EnsureCutsceneAnimationBlockReady("DEALER")' in isolate_animation and
      "association != idleAssociation" in isolate_animation and
      "kAnimAssocFlagRetireWhenFaded" in isolate_animation and
      "kAnimAssocFlagsStartedLooped" in isolate_animation and
      "address + kAssocBlend" in isolate_animation and
      "g_removeAllAssociations(ped->m_pRwClump)" not in isolate_animation and
      "animationInitialized = false" in isolate_animation and
      "animationAssociationCount = 0" in isolate_animation and
      "ClearCutscenePartnerInheritedAnimation(candidate.id, ped)" in update_scene and
      "partnerAlreadyHeld" in update_scene,
      "cutscene takeover waits for a valid DEALER/DEALER_IDLE base association before retiring inherited tracks and resetting network animation state")
check("if (idleAssociation)" in isolate_animation and
      "g_getFirstAssociation(ped->m_pRwClump)" in isolate_animation and
      "kAssocBlendDelta) = -1000.0f" in isolate_animation and
      "kAnimAssocFlagRetireWhenFaded" in isolate_animation and
      "associationsCleared" not in isolate_animation,
      "animation isolation preserves a live clump association so CPed::PlayFootSteps cannot dereference an empty association list")
check('"Coplook_nod"' in start_fidget and '"Coplook_think"' in start_fidget and
      '"Coplook_shake"' in start_fidget and '"shift"' in start_fidget and
      '"shldr"' in start_fidget and '"strleg"' in start_fidget and
      '"time"' in start_fidget and '"XPRESSscratch"' in start_fidget,
      "shot reactions and timed fidgets draw from the PC reference's ambient animation set")
check("g_getAnimationBlockIndex" in block_ready and
      "g_getAnimationBlock" in block_ready and
      "CStreaming::IsInitialised()" in block_ready and
      "CStreaming::RequestModel" in block_ready and
      "CStreaming::LoadAllRequestedModels(false)" not in block_ready and
      "kAnimationStreamingModelBase + blockIndex" in block_ready,
      "non-PED IFP blocks are resolved through GTA streaming and checked before their animation is requested")
check("g_cutsceneManagerProcessing && *g_cutsceneManagerProcessing" in block_ready and
      "!cutsceneRunning && !cutsceneProcessing" in update_players and
      'EnsureCutsceneAnimationBlockReady("DEALER")' in update_players,
      "DEALER is preloaded during normal gameplay because GTA pauses IFP streaming during cutscene processing")
check("if (!runtime.baseAnimationInstalled)" in update_scene and
      "ClearCutscenePartnerInheritedAnimation(candidate.id, ped)" in update_scene and
      "runtime.baseAnimationInstalled = false" in update_scene and
      "runtime.baseAnimationInstalled && runtime.animationStage == 0" in update_scene and
      "runtime.baseAnimationInstalled &&\n                    EnsureCutsceneAnimationBlockReady" in update_scene,
      "partner retries the DEALER base-association handoff and holds off competing tasks until the IFP is ready")
check("runtime.shotReactionPending" in update_scene and
      "StartCutscenePartnerFidget" in update_scene and
      "runtime.nextFidgetAtMs" in update_scene,
      "new camera shots trigger a contextual reaction and later timed fidgets")
check("CutscenePartnerSpotChanged(runtime, spot)" in update_scene and
      "if (moved)" in update_scene and
      "runtime.lastAnimationCheckMs = 0;\n                continue;" in update_scene,
      "SET_CHAR_COORDINATES is only called after a material move, not every frame")
check("coop_cutscene::MakePartnerSpot" in update_scene and
      "std::sort(candidates.begin(), candidates.end()" in update_scene,
      "multi-player placement uses stable ranked side positions")
check("UpdateActorMotion" in update_scene and
      "nowMs, false);" in update_scene and
      "MakeWalkingPartnerSpot" in update_scene and
      '"WALK_player"' in update_scene and
      '"COOPCS"' in update_scene and
      "GetWalkClip(mocapGait)" in update_scene and
      "kScmSetCharAnimSpeed" in SOURCE and
      "g_saUtils->SetPosition(physical, spot.x, spot.y, spot.z)" in update_scene and
      "runtime.animationStage = 4" in update_scene and
      "sceneAlreadyPlaying && actorMoving" in update_scene,
      "a moving cutscene CJ makes each partner use the extracted COOPCS gait when available, speed-match it, and preserve walking across camera cuts")
check("CAT_3" in SOURCE and "handscower" in SOURCE and
      "W2_ALT" in SOURCE and "Coplook_loop" in SOURCE and
      "CUTSCENE_PARTNER_SCENE_ACTION" in update_scene and
      "actionBlockReady" in update_scene and "sceneActionPending" in update_scene and
      "sceneActionOneShotFinished" in update_scene,
      "PC-authored CAT_3 and W2_ALT actions wait for their blocks without freezing the fallback pose")
check("&kScmFreezeCharPosition, pedRef, 0" in release and
      "animationInitialized = false" in release and "weapon = 0xFF" in release,
      "release unfreezes the ped and forces network animation/weapon restoration")
check("g_heldRemoteCutscenePartners.find(entry.first)" in post_process,
      "generic ped post-process does not overwrite a held partner heading")
check("cutsceneSuppressed" in update_players and
      "!cutsceneSuppressed && visual.weapon != player.weapon" in update_players and
      "if (!cutsceneSuppressed)\n                UpdateRemoteAimAndShooting" in update_players,
      "network movement/animation/weapon/aim cannot overwrite the held cutscene pose")
check("!cutsceneSceneActive && !g_heldRemoteCutscenePartners.empty()" in update_players and
      "cutscene actor unavailable" in update_players and
      "if (cutsceneSceneActive) UpdateCutscenePartners(cutsceneCj)" in update_players,
      "scene end or missing csplay actor releases partners and active scenes place them last each tick")
check("ReleaseRemoteCutscenePartner(removed.id, \"remote player disconnected\")" in update_players and
      "ReleaseRemoteCutscenePartner(player.id, \"remote player model changed\")" in update_players,
      "disconnect and model replacement paths release cutscene holds")
check("std::atan2(-forward.x, forward.y)" in LOGIC and
      "rank % 2 == 0" in LOGIC and "rank / 2" in LOGIC and
      "if (headingDegrees < 0.0f) headingDegrees += 360.0f;" in update_scene and
      "NeedsReposition" in LOGIC,
      "placement math derives facing from actor orientation, normalizes heading and suppresses tiny teleports")

print("26 cutscene source checks passed, 0 failed")
