// 5.02.15: crossplay key bar check (tools/omp_sweep/keybar_test.sh). Logs what the PC server gets from the phone's
// << ESC TAB ALT SPC F H 2 Y N G bar: OnPlayerKeyStateChange, the ESC dialog close, the ESC textdraw-menu cancel.
#include <open.mp>

main() {}

public OnGameModeInit()
{
    AddPlayerClass(0, 1958.33, 1343.12, 15.36, 269.15, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    return 1;
}
public OnPlayerRequestClass(playerid, classid) { SetSpawnInfo(playerid, NO_TEAM, 0, 1958.33, 1343.12, 15.36, 269.15); return 1; }
public OnPlayerSpawn(playerid) { printf("KT spawn %d", playerid); return 1; }
public OnPlayerKeyStateChange(playerid, KEY:newkeys, KEY:oldkeys)
{
    printf("KT keys new %d old %d", _:newkeys, _:oldkeys);
    return 1;
}
public OnPlayerCommandText(playerid, cmdtext[])
{
    if(!strcmp(cmdtext, "/dlg")) { ShowPlayerDialog(playerid, 77, DIALOG_STYLE_MSGBOX, "Keys", "ESC closes me", "OK", ""); return 1; }
    if(!strcmp(cmdtext, "/dlg2")) { ShowPlayerDialog(playerid, 78, DIALOG_STYLE_LIST, "Keys", "one\ntwo", "Pick", "Back"); return 1; }
    if(!strcmp(cmdtext, "/sel")) { SelectTextDraw(playerid, 0xFF0000FF); printf("KT select on"); return 1; }
    return 0;
}
public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
    printf("KT dialog %d response %d item %d", dialogid, response, listitem);
    return 1;
}
public OnPlayerClickTextDraw(playerid, Text:clickedid)
{
    printf("KT click %d", _:clickedid);
    return 1;
}
