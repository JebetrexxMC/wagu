# MonetLoader mods

![Mods](../Screenshots/gifs/monetloader.gif)

`moonloader/mods_loader.lua` starts every `.lua` right inside `moonloader/mods/`. Old copies left in `moonloader/` are stopped and renamed to `.lua.old`.

## lua_scripts folder (.lua, .luac and .zip)

`moonloader/lua_scripts_loader.lua` is the entry point of `moonloader/lua_scripts/`:

- every `.lua` / `.luac` in `lua_scripts/` and its sub folders starts (the [moonloader-scripts](https://github.com/ThirtyFoxMCx00E1/moonloader-scripts) collection is in `lua_scripts/moonloader-scripts/`);
- every `.zip` is unpacked to `lua_scripts/.unpacked/<zip name>/` and its scripts start from there: at the top of the zip, in a folder, or in a PC-style `moonloader/` folder (see `lua_scripts/hitmarker.zip`);
- `lib/`, `config/`, `resource/`, `resources/` and a folder named like the script are copied into `moonloader/` (missing files only); a zip without a script is copied as data;
- PC scripts with `\\` paths work (`lib/mp_pcpaths.lua`);
- `lua_scripts/disabled.txt` keeps scripts from starting; `/luascripts list` shows what runs, `/luascripts rescan` starts them again.

| Mod | What it does |
|---|---|
| `mods/tvplayer.lua` | Plays video on the TV model in houses |
| `mods/hitmarker.lua` | Hit markers from MP's real hits (`MP_PopHit`) |
| `mods/hg.lua` | HG menu, opened with `/hg` |

## When HG runs

HG and the CLEO menu are cheat mods.

They run:
- offline (single player)
- on custom servers whose owner allows CheatMenu + MonetLoader cheats (see [Server Management](Server-Management.md))

They never run:
- on the default server or LAN server
- on featured servers
- with "Block menu cheats"

When the owner turns cheats off, HG stops at once. It starts again where it is allowed.
