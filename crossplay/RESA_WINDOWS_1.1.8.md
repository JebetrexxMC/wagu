# ReSA Windows 1.1.8 (resa-windows-1.1.8-c18df27cde9b.zip) - what is inside

Read for MP 5.02.18 (static analysis: PE imports/strings, Dart AOT strings, the zip layout). No ReSA code is copied into MP.

## Layout

| Path | Size | What it is |
|---|---|---|
| `alyn_launcher.exe` | 1.1 MB | Flutter Windows runner ("ReSA Multiplayer" 1.1.8+128, ro.alynsampmobile). Registers the `resa:` link handler, loads `alyn_resa.dll` |
| `alyn_resa.dll` + `alyn_resa.symmap` | 19.6 + 4.5 MB | The game itself: a native GTA SA engine port (CustomPipes, CAnimBlendHierarchy, ImGui) with the SA-MP client inside. Exports `ReSA_ConfigureSession` / `ReSA_Run` |
| `alloader.dll` | 4.6 MB | AL Loader (Alyn Lua Loader): the Lua script runtime |
| `dartjni.dll`, `flutter_windows.dll`, ANGLE | 20 MB+ | Flutter + GL-on-D3D |
| `data/app.so` | 19 MB | Dart AOT launcher UI (server list, community files, Discord login) |
| `data/flutter_assets/assets/al_scripts/` | 47 KB | 7 bundled AL scripts (chat_log, command_buttons, freecam, info_hud, info_window, quick_commands, script_manager) + `catalog.json` |
| `data/flutter_assets/assets/al_docs/al_api.json` | 554 KB | AL API reference: 6 globals, 13 events, 1729 functions, 65 modules, 8 guides |
| `al_loader/lib/` | 66 files | Lua libs: SAMP.Lua (`samp.events`, `samp.raknet`), MoonLoader `moonloader`/`sampfuncs`, mimgui, copas, socket, cjson, jsoncfg, widgets, fAwesome6 ... |
| `SAMP/` | 7 files | gta.dat, main.scm, script.img, peds.ide, TIMECYC.DAT (SA-MP versions) |
| `data/TouchDefaultPhone*.cfg`, `ShopUVs.dat`, `colorcycle.dat` | 55 KB | Mobile touch layouts / data the engine reads |

The game needs a PC GTA SA folder: the launcher checks `models\gta3.img`, `models\gta_int.img`, `models\player.img`, `anim\anim.img`, `data\gta.dat`, `audio\CONFIG\BankLkup.dat`, `SAMP\SAMP.img`, `SAMP\SAMPCOL.img`, `SAMP\SAMP.ide`, `textures\mobile.bin`.

## How a server reaches the game

1. Link handler, written by the launcher under `HKCU\Software\Classes\resa`: `URL Protocol`, `shell\open\command` = `"alyn_launcher.exe" "%1"`. It also registers `.resacommunity` files (`ReSA.CommunityFile`).
2. Link format (same on Android, `com.alyn.resamp.JoinLinkActivity`):
   - `resa://connect?host=IP&port=PORT` (join a server)
   - `resa://community?...` (community packs)
   - Web: `https://resa-mp.com/join?host=IP&port=PORT`. The page only forwards to `resa://connect`, so with no ReSA app installed the browser stays open. That is why the old MP ReSA Join ended up in Chrome.
3. Flutter method channels: `resa/links` (the link from argv), `resa/window`, `ro.alynsampmobile.launcher/windows_game` (start the game: `ReSA_ConfigureSession` then `ReSA_Run` in `alyn_resa.dll`).
4. Engine env switches: `RESA_AUTOSTART`, `RESA_TIMEOUT_MS`, `RESA_WINDOW_WIDTH/HEIGHT`, `RESA_BACKGROUND_MAX_FPS`, `RESA_NO_WINDOW_INPUT`, `RESA_HIDE_AUTOMATION_WINDOW`.

## Web endpoints (from app.so)

| Endpoint | Use |
|---|---|
| `https://resa-mp.com/api/servers` | Server list JSON `{"servers":[{"host","port","priority","id","featured","verified"}]}`, the same data as `alynsampmobile.pro/api/servers` (checked Nov 2026) |
| `https://resa-mp.com/api/community/{pack,file,asset}/` | Community packs |
| `/launcher/auth/code`, `/launcher/discord/token`, `/launcher/me` | Discord account login |
| `/launcher/integrity`, `/launcher/logs` | Anti-tamper check / log upload ("The app was modified or a tool is interfering") |
| `https://resa-mp.com/download` | Downloads |

## What MP uses from this (5.02.18)

- ReSA tab Join goes through MP crossplay. The separate "Open in ReSA" button only sends `resa://connect?host=&port=` (no Chrome fallback).
- The ReSA list is read from `alynsampmobile.pro/api/servers`, with `resa-mp.com/api/servers` as the fallback.
- `assets/MP/moonloader/lib/alloader.lua`: AL Loader defines `MONET_VERSION`, so AL scripts are MonetLoader scripts. The shim adds `require 'alloader'` (touch_events, AL_PLATFORM, AL_VERSION), so scripts written for ReSA run in MP's MonetLoader. The other libraries they need (samp.events, mimgui, jsoncfg, widgets, inspect, encoding) are already in MP.
- Nothing in the Windows build changes the crossplay join itself. It is a separate engine that speaks the normal SA-MP 0.3.7 protocol, the same as MP's `[crossplay] joining as SA-MP 0.3.7-R5 (4057)` path. So no extra handshake bytes are needed.
