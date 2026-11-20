// 5.02.05: third YSI example - custom foreach iterators for PICKUPS (y_iterate) + timers (y_timers).
// Runs the same on an open.mp / SA-MP 0.3.7 PC server and on the MP server (mp_server --samp).
//
//   sh tools/pawn/compile.sh third_party/omp-dev/examples/ysi_pickups/ysi_pickups.pwn
//   server/mp_server --samp-test third_party/omp-dev/examples/ysi_pickups/ysi_pickups.amx "/plist;/pgrab 1;/pgrab 2;/pdrop;/pdrop;/pmine;/plist;/pgrab 4;/pclear;/plist"
//
// What it shows:
//   Iterator:Pickups<MAX_PICKUPS>                     - every pickup the script made
//   Iterator:Respawning<MAX_PICKUPS>                  - its own slots (Iter_Free) for taken pickups waiting to come back
//   Iterator:Dropped[MAX_PLAYERS]<MAX_PICKUPS>        - a multi-iterator: the money bags each player dropped
//   task RespawnTick[1000]   - brings taken pickups back after their own delay (Iter_Remove inside foreach)
//   task ExpireDrops[10000]  - money bags vanish after 60 s, the money goes back to who dropped it
//   timer PickupCooldown[2000](playerid) with defer - a short cooldown so one pickup doesn't fire twice
//   commands /plist, /pdrop, /pmine, /pclear, /pgrab <id>, /help

#define YSI_NO_HEAP_MALLOC
#define YSI_NO_MODE_CACHE
#include <open.mp>
#include <YSI_Coding\y_hooks>
#include <YSI_Coding\y_timers>
#include <YSI_Data\y_iterate>

#define COL_INFO    0x33CCFFFF
#define COL_OK      0x88AA62FF
#define COL_WARN    0xFFAA33FF
#define MAX_DROPS   3
#define DROP_AMOUNT 100
#define DROP_LIFE_S 60

enum { PK_HEALTH, PK_ARMOUR, PK_MONEY, PK_WEAPON, PK_BAG };
enum E_PK { pkType, pkModel, Float:pkX, Float:pkY, Float:pkZ, pkRespawnS, pkBackAt, pkOwner, pkMade, pkValue };

new gPk[MAX_PICKUPS][E_PK];        // by pickup id (live pickups)
new gRs[MAX_PICKUPS][E_PK];        // by respawn slot: a pickup id can be reused while the old one waits
new Iterator:Pickups<MAX_PICKUPS>;
new Iterator:Respawning<MAX_PICKUPS>;
new Iterator:Dropped[MAX_PLAYERS]<MAX_PICKUPS>;
new bool:gCooldown[MAX_PLAYERS];

main()
{
    print("[ysi_pickups] YSI pickup iterators example loaded");
}

stock MakePickup(type, model, Float:x, Float:y, Float:z, respawnS, owner = INVALID_PLAYER_ID, value = 0)
{
    new id = CreatePickup(model, 1, x, y, z, -1);   // type 1: we decide ourselves when it comes back
    if (id < 0 || id >= MAX_PICKUPS) return -1;
    gPk[id][pkType] = type; gPk[id][pkModel] = model; gPk[id][pkX] = x; gPk[id][pkY] = y; gPk[id][pkZ] = z;
    gPk[id][pkRespawnS] = respawnS; gPk[id][pkBackAt] = 0; gPk[id][pkOwner] = owner; gPk[id][pkMade] = gettime();
    gPk[id][pkValue] = value;
    Iter_Add(Pickups, id);
    if (owner != INVALID_PLAYER_ID) Iter_Add(Dropped[owner], id);
    return id;
}

stock RemovePickup(id)   // not inside a foreach over Pickups / Dropped (remove in place there)
{
    if (!Iter_Contains(Pickups, id)) return;
    new owner = gPk[id][pkOwner];
    if (owner != INVALID_PLAYER_ID) Iter_Remove(Dropped[owner], id);
    Iter_Remove(Pickups, id);
    DestroyPickup(id);
}

// ---- game mode ---------------------------------------------------------------------------------
hook OnGameModeInit()
{
    SetGameModeText("YSI pickups");
    UsePlayerPedAnims();
    Iter_Init(Dropped);
    AddPlayerClass(0, 1958.33, 1343.12, 15.36, 269.15, WEAPON_COLT45, 120, WEAPON_FIST, 0, WEAPON_FIST, 0);
    MakePickup(PK_HEALTH, 1240, 1962.0, 1338.0, 15.4, 20);    // heart, back after 20 s
    MakePickup(PK_ARMOUR, 1242, 1962.0, 1348.0, 15.4, 30);    // armour, 30 s
    MakePickup(PK_MONEY,  1274, 1970.0, 1338.0, 15.4, 45);    // dollar icon, $500, 45 s
    MakePickup(PK_WEAPON, 356,  1970.0, 1348.0, 15.4, 60);    // M4, 60 s
    printf("[ysi_pickups] game mode ready, %d pickups", Iter_Count(Pickups));
    return 1;
}

hook OnPlayerConnect(playerid)
{
    Iter_Clear(Dropped[playerid]);
    gCooldown[playerid] = false;
    GivePlayerMoney(playerid, 1000);         // starting cash, so /pdrop works right away
    SendClientMessage(playerid, COL_INFO, "YSI pickups: walk into the icons by the spawn. Type /help.");
    return 1;
}

hook OnPlayerDisconnect(playerid, reason)
{
    new n = 0;
    foreach (new id : Dropped[playerid])   // the player's money bags go with them
    {
        Iter_Remove(Pickups, id);
        DestroyPickup(id);
        Iter_Remove(Dropped[playerid], id);   // YSI 5: removing inside the loop is safe
        ++n;
    }
    if (n) printf("[ysi_pickups] player %d left, %d bag(s) removed", playerid, n);
    return 1;
}

public OnPlayerPickUpPickup(playerid, pickupid)
{
    if (!Iter_Contains(Pickups, pickupid) || gCooldown[playerid]) return 1;
    gCooldown[playerid] = true;
    defer PickupCooldown(playerid);
    new msg[80];
    switch (gPk[pickupid][pkType])
    {
        case PK_HEALTH: { SetPlayerHealth(playerid, 100.0); SendClientMessage(playerid, COL_OK, "Health refilled."); }
        case PK_ARMOUR: { SetPlayerArmour(playerid, 100.0); SendClientMessage(playerid, COL_OK, "Armour refilled."); }
        case PK_MONEY:  { GivePlayerMoney(playerid, 500); SendClientMessage(playerid, COL_OK, "+$500"); }
        case PK_WEAPON: { GivePlayerWeapon(playerid, WEAPON_M4, 150); SendClientMessage(playerid, COL_OK, "M4 + 150 rounds."); }
        case PK_BAG:
        {
            GivePlayerMoney(playerid, gPk[pickupid][pkValue]);
            format(msg, sizeof(msg), "You picked up a money bag: +$%d", gPk[pickupid][pkValue]);
            SendClientMessage(playerid, COL_OK, msg);
            RemovePickup(pickupid);          // bags don't come back
            return 1;
        }
    }
    // a map pickup: copy it to a free respawn slot, take it off the map, RespawnTick brings it back
    new rs = Iter_Free(Respawning);
    if (rs != INVALID_ITERATOR_SLOT)
    {
        gRs[rs] = gPk[pickupid];
        gRs[rs][pkBackAt] = gettime() + gPk[pickupid][pkRespawnS];
        Iter_Add(Respawning, rs);
    }
    DestroyPickup(pickupid);
    Iter_Remove(Pickups, pickupid);
    return 1;
}

// ---- timers ------------------------------------------------------------------------------------
timer PickupCooldown[2000](playerid)
{
    gCooldown[playerid] = false;
}

task RespawnTick[1000]()
{
    new now = gettime();
    foreach (new rs : Respawning)
    {
        if (now < gRs[rs][pkBackAt]) continue;
        Iter_Remove(Respawning, rs);         // YSI 5: safe inside foreach
        new id = MakePickup(gRs[rs][pkType], gRs[rs][pkModel], gRs[rs][pkX], gRs[rs][pkY], gRs[rs][pkZ], gRs[rs][pkRespawnS]);
        printf("[ysi_pickups] pickup back (model %d, id %d)", gRs[rs][pkModel], id);
    }
}

task ExpireDrops[10000]()
{
    new now = gettime(), n = 0;
    foreach (new id : Pickups)
    {
        if (gPk[id][pkType] != PK_BAG || now - gPk[id][pkMade] < DROP_LIFE_S) continue;
        new owner = gPk[id][pkOwner];
        if (owner != INVALID_PLAYER_ID)
        {
            Iter_Remove(Dropped[owner], id);
            GivePlayerMoney(owner, gPk[id][pkValue]);
            SendClientMessage(owner, COL_WARN, "Nobody took your money bag, the money is back.");
        }
        DestroyPickup(id);
        Iter_Remove(Pickups, id);            // YSI 5: safe inside foreach
        ++n;
    }
    if (n) printf("[ysi_pickups] %d money bag(s) expired", n);
}

// ---- commands ----------------------------------------------------------------------------------
public OnPlayerCommandText(playerid, cmdtext[])
{
    if (!strcmp(cmdtext, "/help", true))
    {
        SendClientMessage(playerid, COL_INFO, "/plist  /pdrop (drop $100 bag)  /pmine  /pclear  /pgrab <id> (take one without walking)");
        return 1;
    }
    if (!strcmp(cmdtext, "/plist", true))
    {
        new line[96];
        format(line, sizeof(line), "%d pickups on the map, %d waiting to come back, %d money bag(s) from you.",
            Iter_Count(Pickups), Iter_Count(Respawning), Iter_Count(Dropped[playerid]));
        SendClientMessage(playerid, COL_INFO, line);
        new now = gettime();
        foreach (new id : Pickups)
        {
            format(line, sizeof(line), " id %d model %d", id, gPk[id][pkModel]);
            SendClientMessage(playerid, COL_INFO, line);
        }
        foreach (new rs : Respawning)
        {
            format(line, sizeof(line), " model %d back in %d s", gRs[rs][pkModel], gRs[rs][pkBackAt] - now);
            SendClientMessage(playerid, COL_INFO, line);
        }
        return 1;
    }
    if (!strcmp(cmdtext, "/pdrop", true))
    {
        if (Iter_Count(Dropped[playerid]) >= MAX_DROPS) return SendClientMessage(playerid, COL_WARN, "You have 3 bags out already."), 1;
        if (GetPlayerMoney(playerid) < DROP_AMOUNT) return SendClientMessage(playerid, COL_WARN, "You need $100."), 1;
        new Float:x, Float:y, Float:z, Float:a;
        GetPlayerPos(playerid, x, y, z); GetPlayerFacingAngle(playerid, a);
        x += 2.0 * floatsin(-a, degrees); y += 2.0 * floatcos(-a, degrees);
        new id = MakePickup(PK_BAG, 1550, x, y, z, 0, playerid, DROP_AMOUNT);
        if (id < 0) return SendClientMessage(playerid, COL_WARN, "No free pickup slot."), 1;
        GivePlayerMoney(playerid, -DROP_AMOUNT);
        new msg[64]; format(msg, sizeof(msg), "Money bag dropped (%d/3), it lasts 60 s.", Iter_Count(Dropped[playerid]));
        SendClientMessage(playerid, COL_OK, msg);
        return 1;
    }
    if (!strcmp(cmdtext, "/pmine", true))
    {
        new line[64], now = gettime();
        format(line, sizeof(line), "Your money bags (%d):", Iter_Count(Dropped[playerid]));
        SendClientMessage(playerid, COL_INFO, line);
        foreach (new id : Dropped[playerid])
        {
            format(line, sizeof(line), " slot %d $%d, %d s left", id, gPk[id][pkValue], DROP_LIFE_S - (now - gPk[id][pkMade]));
            SendClientMessage(playerid, COL_INFO, line);
        }
        return 1;
    }
    if (!strcmp(cmdtext, "/pgrab", true, 6) && (cmdtext[6] == ' ' || cmdtext[6] == EOS))   // test: same as walking into it
    {
        new id = cmdtext[6] ? strval(cmdtext[7]) : Iter_First(Pickups);
        if (!Iter_Contains(Pickups, id)) return SendClientMessage(playerid, COL_WARN, "No pickup with that id (see /plist)."), 1;
        gCooldown[playerid] = false;
        return OnPlayerPickUpPickup(playerid, id);
    }
    if (!strcmp(cmdtext, "/pclear", true))
    {
        new n = 0;
        foreach (new id : Dropped[playerid])
        {
            GivePlayerMoney(playerid, gPk[id][pkValue]);
            DestroyPickup(id);
            Iter_Remove(Pickups, id);
            Iter_Remove(Dropped[playerid], id);
            ++n;
        }
        new msg[48]; format(msg, sizeof(msg), "%d bag(s) picked back up.", n);
        SendClientMessage(playerid, COL_OK, msg);
        return 1;
    }
    return 0;
}
