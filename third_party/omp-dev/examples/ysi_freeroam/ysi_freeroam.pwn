// 5.02.05: example open.mp gamemode - YSI foreach (y_iterate) + YSI timers (y_timers) + y_hooks.
// Runs the same on an open.mp / SA-MP 0.3.7 PC server and on the MP server (mp_server --samp), so phone and
// PC players share it through the crossplay.
//
//   sh tools/pawn/compile.sh third_party/omp-dev/examples/ysi_freeroam/ysi_freeroam.pwn
//   server/mp_server --samp-test third_party/omp-dev/examples/ysi_freeroam/ysi_freeroam.amx
//   server/mp_server 1922 "YSI freeroam" 50 --samp third_party/omp-dev/examples/ysi_freeroam/ysi_freeroam.amx --samp-port 7777
//
// What it shows:
//   foreach  - the built-in Player iterator, plus our own Iterator:Spawned (players who are spawned right now)
//   task     - ServerClock: once a second for the whole server (world clock, the "players on foot" count)
//   ptask    - PlayerPayday: every 60 s for each connected player (pays $250, only while spawned)
//   timer    - HealBack (defer: one shot, 3 s after /heal) and CountdownTick (repeat: /countdown, stopped at 0)
//   commands - /heal, /online, /countdown, /tp, /car, /help (plain OnPlayerCommandText, no command plugin needed)

#define YSI_NO_HEAP_MALLOC
#define YSI_NO_MODE_CACHE
#include <open.mp>
#include <YSI_Coding\y_hooks>
#include <YSI_Coding\y_timers>
#include <YSI_Data\y_iterate>

#define COL_INFO    0x33CCFFFF
#define COL_OK      0x88AA62FF
#define COL_WARN    0xFFAA33FF

new Iterator:Spawned<MAX_PLAYERS>;          // custom iterator: who is spawned (foreach (new i : Spawned))
new gWorldMinute = 12 * 60;                 // world clock in minutes, moved on by the ServerClock task
new Timer:gCountdown = Timer:0;             // the running /countdown repeat timer (0 = none)
new gCountdownLeft = 0;
new bool:gHealPending[MAX_PLAYERS];

main()
{
    print("[ysi_freeroam] YSI foreach + timers example loaded");
}

// ---- game mode ---------------------------------------------------------------------------------
hook OnGameModeInit()
{
    SetGameModeText("YSI freeroam");
    UsePlayerPedAnims();
    AddPlayerClass(0,   1958.33, 1343.12, 15.36, 269.15, WEAPON_COLT45, 120, WEAPON_FIST, 0, WEAPON_FIST, 0);
    AddPlayerClass(106, 2495.33, -1687.36, 13.52, 0.0, WEAPON_COLT45, 120, WEAPON_FIST, 0, WEAPON_FIST, 0);
    AddStaticVehicle(411, 1966.0, 1343.0, 15.2, 90.0, 1, 1);   // Infernus next to the first spawn
    print("[ysi_freeroam] game mode ready");
    return 1;
}

hook OnPlayerConnect(playerid)
{
    gHealPending[playerid] = false;
    new n = 0;
    foreach (new i : Player) ++n;            // YSI foreach over every connected player
    new msg[96];
    format(msg, sizeof(msg), "Welcome to YSI freeroam - %d player(s) online. Type /help.", n);
    SendClientMessage(playerid, COL_INFO, msg);
    printf("[ysi_freeroam] player %d connected (%d online)", playerid, n);
    return 1;
}

hook OnPlayerSpawn(playerid)
{
    Iter_Add(Spawned, playerid);
    SetPlayerTime(playerid, gWorldMinute / 60, gWorldMinute % 60);
    return 1;
}

hook OnPlayerDeath(playerid, killerid, WEAPON:reason)
{
    Iter_Remove(Spawned, playerid);
    return 1;
}

hook OnPlayerDisconnect(playerid, reason)
{
    Iter_Remove(Spawned, playerid);           // safe even when the player never spawned
    gHealPending[playerid] = false;
    return 1;
}

// ---- timers ------------------------------------------------------------------------------------
// task: one timer for the whole server, every 1000 ms
task ServerClock[1000]()
{
    gWorldMinute = (gWorldMinute + 1) % (24 * 60);
    if (gWorldMinute % 60 == 0)
    {
        foreach (new i : Spawned) SetPlayerTime(i, gWorldMinute / 60, 0);
        printf("[ysi_freeroam] world clock %02d:00, %d spawned", gWorldMinute / 60, Iter_Count(Spawned));
    }
}

// ptask: one timer per connected player, every 60 s (YSI starts / stops it on connect / disconnect)
ptask PlayerPayday[60000](playerid)
{
    if (!Iter_Contains(Spawned, playerid)) return;
    GivePlayerMoney(playerid, 250);
    SendClientMessage(playerid, COL_OK, "Payday: +$250");
}

// timer + defer: one shot, 3 s after /heal
timer HealBack[3000](playerid)
{
    gHealPending[playerid] = false;
    if (!IsPlayerConnected(playerid)) return;
    SetPlayerHealth(playerid, 100.0);
    SendClientMessage(playerid, COL_OK, "You are healed.");
}

// timer + repeat: /countdown ticks once a second until stop
timer CountdownTick[1000]()
{
    new msg[32];
    if (gCountdownLeft <= 0)
    {
        stop gCountdown;
        gCountdown = Timer:0;
        foreach (new i : Player)
        {
            GameTextForPlayer(i, "~g~GO!", 1500, 3);
        }
        return;
    }
    format(msg, sizeof(msg), "~y~%d", gCountdownLeft);
    foreach (new i : Player) GameTextForPlayer(i, msg, 1000, 3);
    --gCountdownLeft;
}

// ---- commands ----------------------------------------------------------------------------------
public OnPlayerCommandText(playerid, cmdtext[])
{
    if (!strcmp(cmdtext, "/help", true))
    {
        SendClientMessage(playerid, COL_INFO, "/heal  /online  /countdown  /tp  /car");
        return 1;
    }
    if (!strcmp(cmdtext, "/heal", true))
    {
        if (gHealPending[playerid]) return SendClientMessage(playerid, COL_WARN, "Heal is already on the way."), 1;
        gHealPending[playerid] = true;
        defer HealBack(playerid);
        SendClientMessage(playerid, COL_INFO, "Healing in 3 seconds...");
        return 1;
    }
    if (!strcmp(cmdtext, "/online", true))
    {
        new name[MAX_PLAYER_NAME + 1], line[80];
        format(line, sizeof(line), "%d online, %d spawned:", Iter_Count(Player), Iter_Count(Spawned));
        SendClientMessage(playerid, COL_INFO, line);
        foreach (new i : Player)
        {
            GetPlayerName(i, name, sizeof(name));
            format(line, sizeof(line), " [%d] %s%s", i, name, Iter_Contains(Spawned, i) ? "" : " (not spawned)");
            SendClientMessage(playerid, COL_INFO, line);
        }
        return 1;
    }
    if (!strcmp(cmdtext, "/countdown", true))
    {
        if (gCountdown != Timer:0) return SendClientMessage(playerid, COL_WARN, "A countdown is already running."), 1;
        gCountdownLeft = 5;
        gCountdown = repeat CountdownTick();
        return 1;
    }
    if (!strcmp(cmdtext, "/tp", true))
    {
        SetPlayerPos(playerid, 2495.33, -1687.36, 13.52);   // Grove Street
        return 1;
    }
    if (!strcmp(cmdtext, "/car", true))
    {
        new Float:x, Float:y, Float:z, Float:a;
        GetPlayerPos(playerid, x, y, z); GetPlayerFacingAngle(playerid, a);
        new v = CreateVehicle(411, x + 3.0, y, z + 0.5, a, -1, -1, 300);
        if (v != INVALID_VEHICLE_ID) PutPlayerInVehicle(playerid, v, 0);
        return 1;
    }
    return 0;
}
