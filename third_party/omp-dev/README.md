# open.mp development libraries (5.02.04, 5.02.05)

MP crossplays with open.mp / SA-MP 0.3.7 PC servers, and the MP server runs SA-MP / open.mp Pawn gamemodes
itself (`mp_server --samp <folder|.zip|.rar|.amx>`, Pawn AMX in `third_party/pawn-amx`). These are the official
open.mp scripting repositories, added as git submodules (pinned commits, nothing is copied into wagu), so
gamemodes and filterscripts are written and compiled once and run on both sides of the crossplay.

| Folder | Repository | Upstream | What it is |
|---|---|---|---|
| `compiler` | https://github.com/ThirtyFoxMCx00E1/compiler | https://github.com/openmultiplayer/compiler | The open.mp Pawn compiler (pawncc 3.10) |
| `omp-stdlib` | https://github.com/mpsamp-open/omp-stdlib | https://github.com/openmultiplayer/omp-stdlib | open.mp standard includes (`#include <open.mp>`, also `a_samp`) |
| `qawno` | https://github.com/ThirtyFoxMCx00E1/qawno | https://github.com/openmultiplayer/qawno | qawno, the open.mp Pawn editor (Qt, PC) |
| `sampctl` | https://github.com/mpsamp-open/sampctl | https://github.com/Southclaws/sampctl | sampctl package manager / build tool (Go) |
| `sampctl2` | https://github.com/mpsamp-open/sampctl2 | https://github.com/Southclaws/sampctl | Second fork of sampctl (same commit as `sampctl` today) |
| `YSI-Includes` | https://github.com/ThirtyFoxMCx00E1/YSI-Includes (branch 5.x) | https://github.com/pawn-lang/YSI-Includes | YSI 5 (y_hooks, y_timers, y_iterate/foreach, y_commands, ...) |
| `amx_assembly` | https://github.com/ThirtyFoxMCx00E1/amx_assembly | https://github.com/pawn-lang/amx_assembly | AMX introspection / code generation (needed by YSI) |
| `code-parse.inc` | https://github.com/ThirtyFoxMCx00E1/code-parse.inc | https://github.com/Y-Less/code-parse.inc | Pre-processor parsing helpers (needed by YSI) |
| `indirection` | https://github.com/ThirtyFoxMCx00E1/indirection | https://github.com/Y-Less/indirection | Function pointers / callbacks (needed by YSI) |
| `md-sort` | https://github.com/ThirtyFoxMCx00E1/md-sort | https://github.com/oscar-broman/md-sort | Multi-dimensional array sort |
| `open.mp-gdk` | https://github.com/ThirtyFoxMCx00E1/open.mp-gdk | https://github.com/openmultiplayer/open.mp-gdk | open.mp C++ component SDK (GDK) for server components |

The folders stay empty until you fetch them (a normal clone of wagu, CI and the release zips do not download them):

```sh
git submodule update --init --depth 1 third_party/omp-dev/compiler third_party/omp-dev/omp-stdlib
# YSI and its helpers
git submodule update --init --depth 1 third_party/omp-dev/YSI-Includes third_party/omp-dev/amx_assembly \
    third_party/omp-dev/code-parse.inc third_party/omp-dev/indirection third_party/omp-dev/md-sort
# everything (sampctl / sampctl2 are about 145 MB each)
git submodule update --init --depth 1
```

## All repositories (5.02.05)

Besides the 11 folders above, 675 more open.mp / SA-MP repositories are git submodules (real addresses, pinned
commits, empty until fetched) under `third_party/omp-dev/<category>/<name>`: `stdlib`, `sdk`, `editors`,
`includes`, `plugins`, `crypto`, `langs`, `gamemodes`, `tools`, `omp` (every repository of the
[openmultiplayer](https://github.com/openmultiplayer) organization, at its real address) and `topic/<owner>/<name>`
(every repository with the GitHub topic [sa-mp](https://github.com/topics/sa-mp) that is not already here, 532,
sorted by stars). The full list with addresses and commits is [REPOS.md](REPOS.md); the source is
[repos.tsv](repos.tsv), and `python3 tools/pawn/gen_omp_repos.py` makes REPOS.md and `src/ui/omp_repos.h` (the app's
About page > open.mp development section) from it.

```sh
git submodule update --init --depth 1 third_party/omp-dev/plugins/Pawn.RakNet   # one repository
git submodule foreach --quiet 'echo $sm_path'                                    # list them
```

Example gamemodes: [examples/ysi_pickups](examples/ysi_pickups/) (custom pickup iterators, respawn / expiry
tasks, a cooldown timer), [examples/ysi_vehicles](examples/ysi_vehicles/) (custom vehicle iterators, a per-player
multi-iterator, cleanup / repair tasks) and [examples/ysi_freeroam](examples/ysi_freeroam/) (YSI foreach, custom iterator, task / ptask /
defer / repeat timers, commands).

## Compile a gamemode

```sh
sh tools/pawn/build_pawncc.sh                      # builds tools/pawn/bin/pawncc (needs cmake, make, gcc)
sh tools/pawn/compile.sh mymode.pwn                # -> mymode.amx, includes omp-stdlib + YSI
server/mp_server --samp-test mymode.amx "/help;wait:5000;/cmd"   # one pretend player on the MP server (wait:<ms> for timers)
server/mp_server 1922 "My server" 50 --samp mymode.amx --samp-port 7777   # MP phones + PC open.mp players
```

The same `.amx` goes into an open.mp server's `gamemodes/` folder. `tools/pawn/examples/omp_hello.pwn` (plain
omp-stdlib) and `ysi_hello.pwn` (YSI y_hooks + foreach) are compiled and run on both the MP server and a real
omp-server 1.6.7 by `.github/workflows/pawn.yml`.

Notes:
- pawncc is built with 32-bit cells (`-DPAWN_CELL_SIZE=32`); a 64-bit host would otherwise make 64-bit-cell
  `.amx` files that neither open.mp nor the MP server load.
- `compile.sh` uses `-Z+` (compatibility mode) so YSI's `<YSI_Coding\y_hooks>` style includes work on Linux.
- YSI calls `fcreatedir` at start-up; the MP server has it (plus `fmkdir`, `fflush`, `ftell`, `ftouch`, `fcopy`)
  since 5.02.04.
