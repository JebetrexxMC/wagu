// 5.02.26: the open.mp functions that reach the client (tools/omp_sweep: sampgate_test with MP_TEST_FUNCS=1).
// Spawns the phone and calls them; the callbacks the phone answers with are logged (FT ...).
#include <open.mp>

new gObj, gCar;

main() {}

public OnGameModeInit()
{
    AddPlayerClass(0, 1958.33, 1343.12, 15.36, 269.15, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    gObj = CreateObject(1337, 1960.0, 1343.0, 15.0, 0.0, 0.0, 0.0);
    gCar = CreateVehicle(560, 1962.0, 1340.0, 15.4, 90.0, 1, 1, -1);
    return 1;
}
public OnPlayerRequestClass(playerid, classid) { SetSpawnInfo(playerid, NO_TEAM, 0, 1958.33, 1343.12, 15.36, 269.15); return 1; }
public OnPlayerSpawn(playerid)
{
    printf("FT spawn %d", playerid);
    TogglePlayerClock(playerid, true);
    TogglePlayerWidescreen(playerid, true);
    SetPlayerWorldBounds(playerid, 2100.0, 1800.0, 1500.0, 1200.0);
    SetPlayerSkillLevel(playerid, WEAPONSKILL_PISTOL, 999);
    SetPlayerFightingStyle(playerid, FIGHT_STYLE_KUNGFU);
    SetPlayerSpecialAction(playerid, SPECIAL_ACTION_HANDSUP);
    CreateExplosionForPlayer(playerid, 1970.0, 1343.0, 15.0, 12, 5.0);
    SendDeathMessage(INVALID_PLAYER_ID, playerid, 54);
    SetObjectRot(gObj, 0.0, 0.0, 45.0);
    AddVehicleComponent(gCar, 1010);
    ChangeVehiclePaintjob(gCar, 1);
    ChangeVehicleColours(gCar, 3, 6);
    UpdateVehicleDamageStatus(gCar, 0, 0, 5, 15);
    RemoveVehicleComponent(gCar, 1010);
    SetPlayerAttachedObject(playerid, 0, 18645, 2, 0.07, 0.0, 0.0, 88.0, 75.0, 0.0);
    SetTimerEx("FtEdit", 1500, false, "i", playerid);
    return 1;
}
forward FtEdit(playerid);
public FtEdit(playerid)
{
    printf("FT edit object"); EditObject(playerid, gObj);
    AddVehicleComponent(gCar, 1010); ChangeVehicleColours(gCar, 7, 8); ChangeVehiclePaintjob(gCar, 2);
    UpdateVehicleDamageStatus(gCar, 0, 0, 0, 1);
    return 1;
}
forward FtEditAtt(playerid);
public FtEditAtt(playerid) { printf("FT edit attached"); EditAttachedObject(playerid, 0); RemoveVehicleComponent(gCar, 1010); return 1; }
forward FtSelect(playerid);
public FtSelect(playerid) { printf("FT select"); SelectObject(playerid); return 1; }
public OnPlayerEditObject(playerid, playerobject, objectid, EDIT_RESPONSE:response, Float:fX, Float:fY, Float:fZ, Float:fRotX, Float:fRotY, Float:fRotZ)
{
    printf("FT OnPlayerEditObject %d obj %d resp %d %.2f %.2f %.2f rz %.1f", playerid, objectid, _:response, fX, fY, fZ, fRotZ);
    if(response == EDIT_RESPONSE_FINAL) { SetObjectPos(objectid, fX, fY, fZ); SetTimerEx("FtEditAtt", 500, false, "i", playerid); }
    return 1;
}
public OnPlayerEditAttachedObject(playerid, EDIT_RESPONSE:response, index, modelid, boneid, Float:fOffsetX, Float:fOffsetY, Float:fOffsetZ, Float:fRotX, Float:fRotY, Float:fRotZ, Float:fScaleX, Float:fScaleY, Float:fScaleZ)
{
    printf("FT OnPlayerEditAttachedObject resp %d index %d model %d bone %d x %.2f", _:response, index, modelid, boneid, fOffsetX);
    SetTimerEx("FtSelect", 500, false, "i", playerid);
    return 1;
}
public OnPlayerSelectObject(playerid, SELECT_OBJECT:type, objectid, modelid, Float:fX, Float:fY, Float:fZ)
{
    printf("FT OnPlayerSelectObject type %d obj %d model %d", _:type, objectid, modelid);
    CancelEdit(playerid);
    return 1;
}
