"""Source-level regression checks for the Revision 3 aim-sync port."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "coopfusion" / "world" / "game_hooks.cpp").read_text()
SYNC_TYPES = (ROOT / "coopfusion" / "world" / "sync_types.h").read_text()
CLIENT = (ROOT / "coopfusion" / "transport" / "coop_client.cpp").read_text()


def function_body(name: str, occurrence: int = 0) -> str:
    marker = f"{name}("
    pos = -1
    for _ in range(occurrence + 1):
        pos = SOURCE.find(marker, pos + 1)
        assert pos >= 0, f"function/call not found: {name} occurrence {occurrence}"
    start = SOURCE.find("{", pos)
    assert start >= 0, f"body not found: {name}"
    depth = 0
    for i in range(start, len(SOURCE)):
        if SOURCE[i] == "{":
            depth += 1
        elif SOURCE[i] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[start + 1:i]
    raise AssertionError(f"unterminated function: {name}")


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)
    print(f"PASS: {message}")


profile_apply = function_body("ApplyRemoteAnimationProfile")
associations = function_body("ApplyAssociationStates")
aim_update = function_body("UpdateRemoteAimAndShooting")
aim_heading = function_body("ApplyRemoteAimHeadingContext")
post_process = function_body("HookGenericPedProcess")
camera_capture = function_body("CaptureLocalAimDirection")
aim_reapply = function_body("ReapplyCachedRemoteAim")
vehicle_aim = function_body("ReapplyRemoteAimsForVehicle")

check("m_pedIK.m_TorsoOrien.m_fPitch" not in profile_apply and
      "m_fAimingRotation =" not in profile_apply,
      "remote animation profile does not compete with native gun/arm IK")
check("m_pedIK.m_TorsoOrien.m_fPitch" not in associations and
      "m_fAimingRotation =" not in associations,
      "association playback no longer writes a competing aim pose")
check("kScmTaskAimGunAtCoord" in aim_update and "RunRemotePedTask" in aim_update,
      "remote players use GTA's native TASK_AIM_GUN_AT_COORD")
check("FallbackAimTarget" in aim_update and "kAimTaskRefreshMs = 500" in aim_update and
      "kAimTaskDurationMs = 1000" in aim_update,
      "aim task uses Revision 3's bounded 8 m fallback target and refresh timing")
check("g_pointGunInDirection" not in aim_update and
      "m_pedIK.m_TorsoOrien.m_fPitch" not in aim_update,
      "remote-player update does not apply a second direct IK solve or torso-pitch write")
check("ANIM_AIM_FIRST_PERSON" in aim_heading and "bAimWithArm" in aim_heading and
      "m_fCurrentRotation = yaw" in aim_heading,
      "first-person and weapon-specific body-heading rules remain distinct")
check("g_originalGenericPedProcess" in post_process and
      post_process.find("g_originalGenericPedProcess(self)") < post_process.find("ReapplyCachedRemoteAim"),
      "generic CPed post-hook runs after GTA processing and restores heading context only")
check("g_pointGunInDirection" not in aim_reapply and
      "ApplyRemoteAimHeadingContext" in aim_reapply,
      "post-ProcessControl reapply never layers another gun IK solve")
check("TheCamera.GetActiveCam().m_vecFront" in camera_capture and
      "YawFromDirection" in camera_capture and "PitchFromDirection" in camera_capture and
      "MODE_AIMWEAPON_FROMCAR" in SOURCE,
      "capture serializes yaw and pitch from the active camera ray, including vehicle aim mode")
check("ANIM_AIM_FIRST_PERSON = 1u << 8" in SYNC_TYPES and
      "aimYaw" in CLIENT and "aimPitch" in CLIENT,
      "first-person state reuses the existing ANIM2 uint16 flags and aim angle fields")
check("HasVerifiedCPedProcessControlPrologue" in SOURCE and
      "0xF0, 0xB5, 0x03, 0xAF" in SOURCE,
      "generic CPed hook is guarded by the supplied Android binary prologue")
check("m_pVehicle != vehicle" in vehicle_aim and
      "ReapplyRemoteAimsForVehicle" in SOURCE,
      "vehicle aim reapply is limited to remote peds attached to the processed vehicle")

update_players = function_body("UpdateRemotePlayers")
call_pos = update_players.rfind("UpdateRemoteAimAndShooting(")
association_pos = update_players.rfind("ApplyAssociationStates(")
weapon_pos = update_players.rfind("ApplyRemoteWeapon(")
check(call_pos > association_pos and call_pos > weapon_pos,
      "aim task update runs after animation associations and weapon switching")

print("13 passed, 0 failed")
