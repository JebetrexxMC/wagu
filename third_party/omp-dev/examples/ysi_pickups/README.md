# ysi_pickups - YSI custom pickup iterators + timers (5.02.05)

Third YSI example: pickups kept in custom `foreach` iterators, on open.mp / SA-MP 0.3.7 PC servers and on the MP
server (`--samp`), so PC and phone players share it through the crossplay.

What it shows:
- `Iterator:Pickups<MAX_PICKUPS>` - every pickup on the map (4 by the spawn: health, armour, $500, M4).
- `Iterator:Respawning<MAX_PICKUPS>` - its own slots (`Iter_Free`) for taken pickups that wait to come back, so a
  pickup id the server hands out again (a money bag) can't overwrite a waiting one.
- `Iterator:Dropped[MAX_PLAYERS]<MAX_PICKUPS>` - a multi-iterator, the money bags each player dropped.
- `task RespawnTick[1000]` - brings taken pickups back after their own delay (20 / 30 / 45 / 60 s).
- `task ExpireDrops[10000]` - a money bag nobody took vanishes after 60 s and the money goes back to who dropped it.
- `timer PickupCooldown[2000](playerid)` with `defer` - one walk-in doesn't fire twice.
- `Iter_Remove` inside `foreach` (YSI 5), `Iter_Init`, `Iter_Clear`, `Iter_First`.
- Commands: `/plist`, `/pdrop` (a $100 bag, max 3), `/pmine`, `/pclear`, `/pgrab <id>` (take a pickup without
  walking, for tests), `/help`. New players get $1000 to start.

```sh
sh tools/pawn/compile.sh third_party/omp-dev/examples/ysi_pickups/ysi_pickups.pwn
server/mp_server --samp-test third_party/omp-dev/examples/ysi_pickups/ysi_pickups.amx "/pgrab 2;/plist;wait:31000;/plist;/pdrop;/pmine"
```

`wait:<ms>` (5.02.05) holds the next test command, so timers can be checked with `--samp-test`.

Checked on the host: 0 errors / 0 warnings; on `mp_server --samp-test` the armour taken with `/pgrab 2` came back
after 30 s ("pickup back (model 1242)"), bags drop / list / come back with `/pclear`, 2 bags nobody took expired after 60 s with the money back (`wait:71000`), and a player's bags are removed
when they leave; on omp-server 1.6.7.3218 the gamemode loads ("4 pickups") and a crossplay test player joins.
Built and run by `pawn.yml`.
