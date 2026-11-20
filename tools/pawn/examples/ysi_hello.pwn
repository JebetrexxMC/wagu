// 5.02.04: YSI test - y_hooks + y_timers + y_iterate on top of omp-stdlib
#define YSI_NO_HEAP_MALLOC
#include <open.mp>
#include <YSI_Coding\y_hooks>
#include <YSI_Coding\y_timers>
#include <YSI_Data\y_iterate>

main() { print("[ysi_hello] YSI loaded"); }

hook OnGameModeInit()
{
    SetGameModeText("ysi_hello");
    AddPlayerClass(0, 1958.33, 1343.12, 15.36, 269.15, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    return 1;
}

hook OnPlayerConnect(playerid)
{
    new n = 0;
    foreach(new i : Player) ++n;
    SendClientMessage(playerid, 0x33CCFFFF, "Welcome - YSI hooks work.");
    printf("[ysi_hello] %d player(s) online", n);
    return 1;
}
