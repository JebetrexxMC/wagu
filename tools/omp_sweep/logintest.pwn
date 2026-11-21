// 5.02.17: crossplay login-server check (tools/omp_sweep/local_test.sh part 4). Works like a roleplay login server
// (Horizonte RP and co): OnPlayerRequestSpawn says no until you log in, the login window opens in
// OnPlayerFinishedDownloading, the script spawns you itself (SpawnPlayer) and OnPlayerSpawn moves you, gives a gun
// and shows a textdraw. Prints what IsPlayerUsingOfficialClient / IsPlayerUsingOmp report for the phone.
#include <open.mp>

new bool:gLogged[MAX_PLAYERS], gRefused[MAX_PLAYERS], Text:gTd;

main() {}

public OnGameModeInit()
{
    AddPlayerClass(0, 1958.33, 1343.12, 15.36, 269.15, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    gTd = TextDrawCreate(320.0, 400.0, "LT welcome");
    TextDrawAlignment(gTd, TEXT_DRAW_ALIGN_CENTER);
    return 1;
}
public OnPlayerConnect(playerid)
{
    new ver[24]; GetPlayerVersion(playerid, ver, sizeof(ver));
    gLogged[playerid] = false; gRefused[playerid] = 0;
    printf("LT connect official %d omp %d version %s", IsPlayerUsingOfficialClient(playerid), IsPlayerUsingOmp(playerid), ver);
    return 1;
}
public OnPlayerFinishedDownloading(playerid, virtualworld)
{
    printf("LT finished downloading");
    if(!gLogged[playerid]) ShowPlayerDialog(playerid, 10, DIALOG_STYLE_PASSWORD, "Login", "Password:", "Login", "Quit");
    return 1;
}
public OnPlayerRequestClass(playerid, classid) { SetSpawnInfo(playerid, NO_TEAM, 0, 1958.33, 1343.12, 15.36, 269.15); return 1; }
public OnPlayerRequestSpawn(playerid)
{
    if(gLogged[playerid]) return 1;
    gRefused[playerid]++;
    printf("LT spawn refused %d", gRefused[playerid]);
    return 0;
}
public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
    if(dialogid != 10) return 0;
    printf("LT login response %d text %s", response, inputtext);
    if(response && !strcmp(inputtext, "secret"))
    {
        gLogged[playerid] = true;
        SetSpawnInfo(playerid, NO_TEAM, 7, 1500.0, -1600.0, 13.5, 0.0);
        SpawnPlayer(playerid);
    }
    else ShowPlayerDialog(playerid, 10, DIALOG_STYLE_PASSWORD, "Login", "Wrong password:", "Login", "Quit");
    return 1;
}
public OnPlayerSpawn(playerid)
{
    printf("LT spawn %d logged %d", playerid, gLogged[playerid]);
    SetPlayerPos(playerid, 1481.0, -1750.0, 15.4);
    GivePlayerWeapon(playerid, WEAPON_DEAGLE, 50);
    TextDrawShowForPlayer(playerid, gTd);
    return 1;
}
