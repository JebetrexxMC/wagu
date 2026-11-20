# ysi_freeroam - YSI foreach + timers gamemode (5.02.05)

A small freeroam gamemode that runs on open.mp / SA-MP 0.3.7 PC servers and on the MP server (`--samp`), so PC and
phone players share it through the crossplay.

What it shows:
- `y_hooks` - `hook OnPlayerConnect` / `OnPlayerDisconnect` / `OnPlayerSpawn` next to the normal callbacks.
- `foreach` - `foreach (new i : Player)` plus a custom iterator `Iterator:Spawned<MAX_PLAYERS>` (`Iter_Add` /
  `Iter_Remove` / `Iter_Count`).
- `y_timers` - `task ServerClock[1000]` (every second for the whole server), `ptask PlayerPayday[60000]` (every
  minute per player), `timer HealBack[3000](playerid)` started with `defer`, and `timer CountdownTick[1000]` started
  with `repeat` and ended with `stop`.
- Commands: `/help`, `/heal` (heals after 3 s), `/online` (lists the spawned players with foreach), `/countdown`,
  `/tp`, `/car`.

```sh
git submodule update --init --depth 1 third_party/omp-dev/compiler third_party/omp-dev/omp-stdlib \
    third_party/omp-dev/YSI-Includes third_party/omp-dev/amx_assembly third_party/omp-dev/code-parse.inc \
    third_party/omp-dev/indirection third_party/omp-dev/md-sort
sh tools/pawn/build_pawncc.sh
sh tools/pawn/compile.sh third_party/omp-dev/examples/ysi_freeroam/ysi_freeroam.pwn
server/mp_server --samp-test third_party/omp-dev/examples/ysi_freeroam/ysi_freeroam.amx "/help;/online;/heal;/countdown;/car"
```

Checked on the host: 0 errors / 0 warnings, runs on `mp_server --samp-test` (YSI 05.10.0006, 0 missing natives) and
on omp-server 1.6.7.3218 (gamemode loads, a crossplay test player joins with the server password and chats).
`.github/workflows/pawn.yml` compiles and runs it on both on every push.
