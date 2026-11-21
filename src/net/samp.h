#pragma once
// 4.11: SA-MP gamemodes on MP servers (net/server_samp.inc). A custom server (phone host, Termux, VPS)
// can run a PC SA-MP server folder as it is: server.cfg, gamemodes/*.amx, filterscripts/*.amx and
// scriptfiles/ - the compiled Pawn scripts run in the Pawn abstract machine (third_party/pawn-amx,
// zLib licence) with the SA-MP natives made on MP (messages, dialogs, textdraws, teleports, money,
// weapons, objects, Pawn.CMD, sscanf, timers, PVars, files) and MySQL R41 natives on SQLite (the
// tables and columns the script uses are made by themselves - no MySQL server needed).
#include <stdint.h>

// PKT_SAMP: S<->C. Old phones / servers ignore it.
enum eSampOp : uint8_t
{
    SPO_HELLO = 1,    // S->C  the server runs a SA-MP gamemode: text = its name. Every /command goes to it.
    SPO_CMD,          // C->S  text = "/command params"
    SPO_TDSELECT,     // S->C  a = 1 SelectTextDraw (val = hover colour) / 0 CancelSelectTextDraw
    SPO_TDCLICK,      // C->S  val = textdraw id (MP id: 0x4000+ global, 0x6000+ player), -1 = Esc / cancelled
    SPO_CHECKPOINT,   // S->C  a = 1 set / 0 off, f = x y z size (b = 1 race checkpoint)
    SPO_CPENTER,      // C->S  a = 1 entered / 0 left, b = 1 race checkpoint
    SPO_MAPICON,      // S->C  a = icon id (0..99), b = marker type (0 = remove), val = colour, f = x y z
    SPO_REMOVEBLD,    // S->C  val = model (-1 = every model), f = x y z radius
    SPO_CAMERA,       // S->C  a = 0 behind the player, 1 = set pos (f = x y z), 2 = look at (f = x y z)
    SPO_CONTROL,      // S->C  a = 1 controllable / 0 frozen
    SPO_WORLD,        // S->C  val = virtual world, a = interior
    SPO_TIME,         // S->C  a = hour, b = minute (val = weather, -1 = keep)
    SPO_SOUND,        // S->C  val = sound id, f = x y z
    SPO_ATTACH,       // S->C  a = slot (0..9), b = bone, val = model (-1 = remove), f = x y z, text = "rx ry rz sx sy sz"
    SPO_LABEL,        // S->C  val = label id, a = 1 add / 0 del, f = x y z dist, b = colour index, text = text
    SPO_KEYS,         // C->S  val = SA-MP key bits (for OnPlayerKeyStateChange)
    // 4.13.53 (old phones ignore them)
    SPO_BUBBLE,       // S->C  val = player id, a = 1, f = draw distance, ms; text = "{RRGGBB}text"
    SPO_ZONE,         // S->C  gang zone id = a | b << 8, f = min x y max x y, val = colour, text[0] = 1 show / 2 flash / 3 stop flash / 4 hide
    SPO_ACTOR,        // S->C  val = actor id, a = 0 remove / 1 set (b = invulnerable, f = x y z angle, text = model) / 2 anim (text = "lib anim loop") / 3 clear anim
    SPO_AUDIO,        // S->C  a = 1 play (text = url, b = 1 at f = x y z distance) / 0 stop
    SPO_VELOCITY,     // S->C  b = 0 me / 1 my car, f = x y z (SA-MP units: metres per 1/50 s)
    SPO_WEAPON,       // S->C  a = 0 armed weapon (val) / 1 ammo (b = weapon, val = ammo)
    SPO_TAGS,         // S->C  a = 0 every tag (b = show, f[0] = distance) / 1 one player's tag (val = player id, b = show)
    SPO_VEHPARAMS,    // S->C  my car: a = engine + 1, b = doors locked + 1 (0 = unset), val = lights (-1 unset)
    SPO_PCLIST,       // S->C  5.02.12: everyone on the PC server (the Tab list): a = 0 clear / 1 add or update (val = id, text = name,
                      //       f[0] = score, f[1] = ping, b = 1 me) / 2 remove (val = id)
    SPO_CLASSSEL,     // S->C  5.02.13: the PC server's class selection: a = 1 show < Spawn > (val = class index) / 0 hide (spawned / login dialog)
    SPO_SPAWNREQ,     // C->S  5.02.13: the phone's class selection buttons: a = 0 Spawn (RequestSpawn) / 1 previous class / 2 next class
    // 5.02.25: the PC server's cars on the phone + the callbacks samp.exe fires (old phones / servers ignore them)
    SPO_VEHICLE,      // S->C  val = server vehicle id. a = 1 add / update (f = x y z angle, b = colour 1, text = "model colour2"),
                      //       0 remove, 2 a PC player drives it (hide the parked copy), 3 put me in it (b = seat),
                      //       4 doors (b = 1 locked / 0 open)
    SPO_MYCAR,        // C->S  the server car I sit in: val = vehicle id (-1 = none), a = seat (0 driver, 1.. passenger)
    SPO_VEHDEATH,     // C->S  val = vehicle id: the server car blew up / sank (OnVehicleDeath)
    SPO_CLICKPLAYER,  // C->S  val = PC player id tapped in the player list (OnPlayerClickPlayer)
    SPO_CLICKMAP,     // C->S  f = x y z of the map waypoint (OnPlayerClickMap)
    SPO_INTERIOR,     // C->S  a = the interior the phone ped is in now (OnPlayerInteriorChange)
};
struct PktSamp { uint8_t type, op, a, b; int32_t val; float f[4]; char text[200]; };
