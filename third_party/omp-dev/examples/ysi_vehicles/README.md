# ysi_vehicles - YSI custom vehicle iterators (5.02.05)

Second YSI example: vehicles kept in custom `foreach` iterators, on open.mp / SA-MP 0.3.7 PC servers and on the
MP server (`--samp`), so PC and phone players share it through the crossplay.

What it shows:
- `Iterator:ServerVeh<MAX_VEHICLES>` - every vehicle the script made (3 static cars + the ones players spawn).
- `Iterator:PlayerVeh[MAX_PLAYERS]<MAX_VEHICLES>` - a multi-iterator, the cars each player spawned
  (`foreach (new v : PlayerVeh[playerid])`, `Iter_Init`, `Iter_Clear`, `Iter_Last`).
- `Iterator:Occupied<MAX_VEHICLES>` - cars with a driver right now, kept by `OnPlayerStateChange`.
- `Iter_Remove` inside a `foreach` loop (YSI 5 allows it; `Iter_SafeRemove` is deprecated).
- `task CleanupEmpty[30000]` - removes player cars left empty for 2 minutes; `task RepairOccupied[5000]` - tops up
  driven cars below 300 health.
- Commands: `/v <model 400-611>` (max 3 a player), `/myveh`, `/vlist`, `/vfix`, `/vdel`, `/help`. A player's cars
  are removed when they leave.

```sh
sh tools/pawn/compile.sh third_party/omp-dev/examples/ysi_vehicles/ysi_vehicles.pwn
server/mp_server --samp-test third_party/omp-dev/examples/ysi_vehicles/ysi_vehicles.amx "/v 411;/v 522;/myveh;/vlist;/vfix;/vdel;/myveh"
```

Checked on the host: 0 errors / 0 warnings; on `mp_server --samp-test` every command answers as expected (2 cars
spawned, listed, repaired, 1 removed, the last one removed on leave); on omp-server 1.6.7.3218 the gamemode loads
("3 vehicles") and a crossplay test player joins, runs /help and leaves cleanly. Built and run by `pawn.yml`.
