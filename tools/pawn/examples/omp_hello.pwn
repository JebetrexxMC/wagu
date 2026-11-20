#include <open.mp>

main()
{
    print("[omp_hello] compiled with open.mp compiler + omp-stdlib");
}

public OnGameModeInit()
{
    SetGameModeText("omp_hello");
    AddPlayerClass(0, 1958.33, 1343.12, 15.36, 269.15, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    return true;
}

public OnPlayerConnect(playerid)
{
    SendClientMessage(playerid, 0x33CCFFFF, "Welcome - built with omp-stdlib.");
    return true;
}
