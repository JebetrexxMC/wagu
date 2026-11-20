// 5.02.05: second YSI example - custom foreach iterators for VEHICLES (y_iterate) + timers (y_timers).
// Runs the same on an open.mp / SA-MP 0.3.7 PC server and on the MP server (mp_server --samp).
//
//   sh tools/pawn/compile.sh third_party/omp-dev/examples/ysi_vehicles/ysi_vehicles.pwn
//   server/mp_server --samp-test third_party/omp-dev/examples/ysi_vehicles/ysi_vehicles.amx "/v 411;/myveh;/vlist;/vfix;/vdel"
//
// What it shows:
//   Iterator:ServerVeh<MAX_VEHICLES>          - every vehicle this script made (static + spawned)
//   Iterator:PlayerVeh[MAX_PLAYERS]<MAX_VEHICLES> - a multi-iterator: the cars each player spawned with /v
//   Iterator:Occupied<MAX_VEHICLES>           - vehicles with a driver right now (OnPlayerStateChange)
//   Iter_Add / Iter_Remove / Iter_Contains / Iter_Count / Iter_Clear / Iter_Last, Iter_Remove while looping
//   task CleanupEmpty[30000] - deletes player cars left empty for 2 minutes (Iter_Remove inside foreach)
//   task RepairOccupied[5000] - tops up the health of occupied cars below 300
//   commands /v <model>, /myveh, /vlist, /vfix, /vdel, /help

#define YSI_NO_HEAP_MALLOC
#define YSI_NO_MODE_CACHE
#include <open.mp>
#include <YSI_Coding\y_hooks>
#include <YSI_Coding\y_timers>
#include <YSI_Data\y_iterate>

#define COL_INFO    0x33CCFFFF
#define COL_OK      0x88AA62FF
#define COL_WARN    0xFFAA33FF
#define MAX_PLAYER_CARS 3
#define EMPTY_LIMIT_S   120

new Iterator:ServerVeh<MAX_VEHICLES>;
new Iterator:PlayerVeh[MAX_PLAYERS]<MAX_VEHICLES>;
new Iterator:Occupied<MAX_VEHICLES>;
new gVehOwner[MAX_VEHICLES] = { INVALID_PLAYER_ID, ... };
new gVehEmptySince[MAX_VEHICLES];           // gettime() when the car was last left empty (0 = never driven)
new gPlayerCar[MAX_PLAYERS] = { INVALID_VEHICLE_ID, ... };   // the car the player is driving

main()
{
    print("[ysi_vehicles] YSI vehicle iterators example loaded");
}

stock AddVeh(veh, owner)
{
    if (veh == INVALID_VEHICLE_ID || veh <= 0 || veh >= MAX_VEHICLES) return 0;
    Iter_Add(ServerVeh, veh);
    gVehOwner[veh] = owner;
    gVehEmptySince[veh] = gettime();
    if (owner != INVALID_PLAYER_ID) Iter_Add(PlayerVeh[owner], veh);
    return 1;
}

stock DropVeh(veh)                          // not for use inside a foreach over ServerVeh (remove in place there)
{
    new owner = gVehOwner[veh];
    if (owner != INVALID_PLAYER_ID) Iter_Remove(PlayerVeh[owner], veh);
    Iter_Remove(Occupied, veh);
    Iter_Remove(ServerVeh, veh);
    gVehOwner[veh] = INVALID_PLAYER_ID;
    DestroyVehicle(veh);
}

// ---- game mode ---------------------------------------------------------------------------------
hook OnGameModeInit()
{
    SetGameModeText("YSI vehicles");
    UsePlayerPedAnims();
    Iter_Init(PlayerVeh);
    AddPlayerClass(0, 1958.33, 1343.12, 15.36, 269.15, WEAPON_COLT45, 120, WEAPON_FIST, 0, WEAPON_FIST, 0);
    AddVeh(AddStaticVehicle(411, 1966.0, 1343.0, 15.2, 90.0, 1, 1), INVALID_PLAYER_ID);   // Infernus
    AddVeh(AddStaticVehicle(522, 1966.0, 1350.0, 15.0, 90.0, 3, 3), INVALID_PLAYER_ID);   // NRG-500
    AddVeh(AddStaticVehicle(560, 1966.0, 1357.0, 15.2, 90.0, 6, 6), INVALID_PLAYER_ID);   // Sultan
    printf("[ysi_vehicles] game mode ready, %d vehicles", Iter_Count(ServerVeh));
    return 1;
}

hook OnPlayerConnect(playerid)
{
    Iter_Clear(PlayerVeh[playerid]);
    gPlayerCar[playerid] = INVALID_VEHICLE_ID;
    SendClientMessage(playerid, COL_INFO, "YSI vehicles: /v <model> spawns a car (max 3). Type /help.");
    return 1;
}

hook OnPlayerDisconnect(playerid, reason)
{
    new n = Iter_Count(PlayerVeh[playerid]);
    foreach (new v : PlayerVeh[playerid])     // the player's own cars go with them
    {
        Iter_Remove(Occupied, v);
        Iter_Remove(ServerVeh, v);
        gVehOwner[v] = INVALID_PLAYER_ID;
        DestroyVehicle(v);
        Iter_Remove(PlayerVeh[playerid], v);           // YSI 5: removing inside the loop is safe
    }
    if (gPlayerCar[playerid] != INVALID_VEHICLE_ID) Iter_Remove(Occupied, gPlayerCar[playerid]);
    gPlayerCar[playerid] = INVALID_VEHICLE_ID;
    printf("[ysi_vehicles] player %d left, %d car(s) removed, %d left on the server", playerid, n, Iter_Count(ServerVeh));
    return 1;
}

hook OnPlayerStateChange(playerid, PLAYER_STATE:newstate, PLAYER_STATE:oldstate)
{
    if (newstate == PLAYER_STATE_DRIVER)
    {
        new v = GetPlayerVehicleID(playerid);
        gPlayerCar[playerid] = v;
        if (Iter_Contains(ServerVeh, v)) Iter_Add(Occupied, v);
    }
    else if (oldstate == PLAYER_STATE_DRIVER)
    {
        new v = gPlayerCar[playerid];
        gPlayerCar[playerid] = INVALID_VEHICLE_ID;
        if (v != INVALID_VEHICLE_ID && Iter_Contains(Occupied, v))
        {
            Iter_Remove(Occupied, v);
            gVehEmptySince[v] = gettime();
        }
    }
    return 1;
}

// ---- timers ------------------------------------------------------------------------------------
task CleanupEmpty[30000]()
{
    new now = gettime(), removed = 0;
    foreach (new v : ServerVeh)
    {
        if (gVehOwner[v] == INVALID_PLAYER_ID) continue;           // static cars stay
        if (Iter_Contains(Occupied, v)) continue;
        if (now - gVehEmptySince[v] < EMPTY_LIMIT_S) continue;
        new owner = gVehOwner[v];
        Iter_Remove(PlayerVeh[owner], v);
        gVehOwner[v] = INVALID_PLAYER_ID;
        DestroyVehicle(v);
        Iter_Remove(ServerVeh, v);                                // YSI 5: safe inside foreach (no Iter_SafeRemove)
        SendClientMessage(owner, COL_WARN, "Your empty car was removed after 2 minutes.");
        ++removed;
    }
    if (removed) printf("[ysi_vehicles] cleanup: %d empty car(s) removed, %d left", removed, Iter_Count(ServerVeh));
}

task RepairOccupied[5000]()
{
    new Float:hp;
    foreach (new v : Occupied)
    {
        GetVehicleHealth(v, hp);
        if (hp < 300.0) SetVehicleHealth(v, 600.0);
    }
}

// ---- commands ----------------------------------------------------------------------------------
public OnPlayerCommandText(playerid, cmdtext[])
{
    if (!strcmp(cmdtext, "/help", true))
    {
        SendClientMessage(playerid, COL_INFO, "/v <model 400-611>  /myveh  /vlist  /vfix  /vdel");
        return 1;
    }
    if (!strcmp(cmdtext, "/v", true, 2) && (cmdtext[2] == ' ' || cmdtext[2] == EOS))
    {
        new model = cmdtext[2] ? strval(cmdtext[3]) : 411;
        if (model < 400 || model > 611) return SendClientMessage(playerid, COL_WARN, "Model 400-611."), 1;
        if (Iter_Count(PlayerVeh[playerid]) >= MAX_PLAYER_CARS)
            return SendClientMessage(playerid, COL_WARN, "You have 3 cars already, /vdel one first."), 1;
        new Float:x, Float:y, Float:z, Float:a;
        GetPlayerPos(playerid, x, y, z); GetPlayerFacingAngle(playerid, a);
        new v = CreateVehicle(model, x + 3.0, y, z + 0.5, a, -1, -1, -1);
        if (!AddVeh(v, playerid)) return SendClientMessage(playerid, COL_WARN, "No free vehicle slot."), 1;
        PutPlayerInVehicle(playerid, v, 0);
        new msg[64];
        format(msg, sizeof(msg), "Car %d (model %d) spawned, %d/3.", v, model, Iter_Count(PlayerVeh[playerid]));
        SendClientMessage(playerid, COL_OK, msg);
        return 1;
    }
    if (!strcmp(cmdtext, "/myveh", true))
    {
        new line[64];
        format(line, sizeof(line), "Your cars (%d):", Iter_Count(PlayerVeh[playerid]));
        SendClientMessage(playerid, COL_INFO, line);
        foreach (new v : PlayerVeh[playerid])
        {
            format(line, sizeof(line), " car %d model %d%s", v, GetVehicleModel(v), Iter_Contains(Occupied, v) ? " (driven)" : "");
            SendClientMessage(playerid, COL_INFO, line);
        }
        return 1;
    }
    if (!strcmp(cmdtext, "/vlist", true))
    {
        new line[80];
        format(line, sizeof(line), "%d vehicles on the server, %d driven, %d spawned by players.",
            Iter_Count(ServerVeh), Iter_Count(Occupied), Iter_Count(ServerVeh) - 3);
        SendClientMessage(playerid, COL_INFO, line);
        return 1;
    }
    if (!strcmp(cmdtext, "/vfix", true))
    {
        new n = 0;
        foreach (new v : PlayerVeh[playerid]) { RepairVehicle(v); ++n; }
        new msg[48]; format(msg, sizeof(msg), "%d car(s) repaired.", n);
        SendClientMessage(playerid, COL_OK, msg);
        return 1;
    }
    if (!strcmp(cmdtext, "/vdel", true))
    {
        if (!Iter_Count(PlayerVeh[playerid])) return SendClientMessage(playerid, COL_WARN, "You have no cars."), 1;
        new v = Iter_Last(PlayerVeh[playerid]);   // newest slot id
        DropVeh(v);
        SendClientMessage(playerid, COL_OK, "Car removed.");
        return 1;
    }
    return 0;
}
