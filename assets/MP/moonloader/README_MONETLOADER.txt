MonetLoader for MP 4.0.0-mp (MP 5.01.00, built from MonetLoaderOSS + the real libraries)
=========================================================================================
PC MoonLoader .lua scripts run on Android, on MP servers and offline. Built into libMP32.so (armeabi-v7a) and
libMP64.so (arm64-v8a) - no separate libmonetloader.so. Source + CI: https://github.com/ThirtyFoxMCx00E1/MonetLoaderOSS

Where things go
  Scripts:    Android/data/com.rockstargames.gtasa/files/TYFMC/MP/moonloader/*.lua
  Libraries:  .../files/TYFMC/MP/moonloader/lib/
  Configs:    .../files/TYFMC/MP/moonloader/config/
  Log:        .../files/TYFMC/MP/moonloader/moonloader.log
  Mods:       .../files/TYFMC/MP/moonloader/mods/*.lua (started by mods_loader.lua)
  Copy the WHOLE moonloader folder from MP.zip with all its subfolders.

What changed in 4.0.0-mp (MP 5.01.00)
  - The old lib/ bundle is gone. It was a flattened copy (CPed.lua, CVehicle.lua ... in the lib root) whose game
    addresses were guessed from other builds. lib/ is now the real libraries, laid out the way their authors ship
    them (pl/, samp/events/, copas/, websocket/, xml/, lub/, compat53/, htmlparser/ ...), synced from source by
    tools/sync_libs.sh in MonetLoaderOSS.
  - The three remade mods (mods/hg.lua HG menu, mods/hitmarker.lua, mods/tvplayer.lua) are removed.
    On the first start mods_loader.lua 1.2-mp renames the old files on your phone to *.old (nothing is deleted;
    rename one back to keep using it) and writes lib/.mp_lib_4.0.0 so it only happens once.
  - LuaJIT 2.1 is built from source (ThirtyFoxMCx00E1/LuaJIT) for both ABIs in the same CI run.

PC compatibility layer (pccompat, built in)
  PC scripts use gta_sa.exe 1.0 US addresses (0x400000 ... 0xFFFFFF). MonetLoader for MP has a map of 6336 named
  PC addresses (5228 functions + globals / arrays) generated from plugin-sdk-sa (tools/gen_pc_addr_map.py), and finds
  the same name in libGTASA.so's own symbol table at run time (both ABIs). So:
    readMemory / writeMemory / getStructElement / setStructElement / *Float* with a PC address
        -> the same variable in libGTASA.so (array elements keep their index; pointer arrays widen on arm64)
    memory.* (lib/memory.lua) goes through the same translation; memory.pcaddr(addr) gives you the address
    callFunction(addr, nparams, pop, ...) / callMethod(addr, this, nparams, pop, ...)
        -> calls the matching libGTASA.so function (arm64: x0-x7 + v0-v7, armeabi-v7a: softfp)
    getModuleHandle('gta_sa.exe') = 0x400000, so base + PC offset translates like any PC address
    getModuleHandle('libGTASA.so' / 'samp.dll' / any .so), getModuleProcAddress
    loadDynamicLibrary / freeDynamicLibrary / getDynamicLibraryProcedure
    getGameGlobal / setGameGlobal / getGameGlobalPtr (CTheScripts::ScriptSpace)
    isKeyDown / wasKeyPressed / wasKeyReleased / setVirtualKeyDown (VK codes; hardware keyboards and MP's key pad)
    isGameWindowForeground, getD3DDevicePtr (0: no Direct3D on Android)
    require 'pccompat' -> pcBase, isPcAddress, translate(addr, size), name(addr), resolve(name), stats()
  Addresses that are not in the map, or member offsets inside PC structs on arm64 (pointers are 8 bytes there),
  are logged once in moonloader.log ("pccompat: ...") instead of reading random memory.

.dll / .asi inside Lua mods
  loadDynamicLibrary('foo.dll'), ffi.load('foo.dll'), ffi.load('C:\\Games\\GTA\\foo.asi') look for an ANDROID build
  of the same library: lib<foo>.so / <foo>.so next to the script, in moonloader/lib, moonloader/libs or moonloader.
  kernel32 / user32 / msvcrt / ws2_32 / winmm map to the C library. A real Windows file (starts with MZ) is x86 code:
  it can't run on an ARM phone, and moonloader.log says so clearly (no crash). Mods that ship their own Android .so
  (both ABIs) load the same way as on PC.

PC MoonImGui on Android (lib/imgui.lua)
  require 'imgui' is the PC MoonImGui API on top of mimgui (same Dear ImGui): imgui.Process, imgui.OnDrawFrame,
  imgui.BeforeDrawFrame, imgui.ShowCursor, imgui.LockPlayer, imgui.ImBool / ImInt / ImFloat / ImInt2-4 / ImFloat2-4 /
  ImBuffer (.v), Begin(name, ImBool), InputText(label, ImBuffer), Combo / ListBox with a Lua table, RadioButton,
  Selectable, MenuItem, CollapsingHeader, Text(fmt, ...) and every other mimgui function. imgui_addons.lua and
  imgui_notf.lua (PC imgui addons) run through it.

Native modules built in (C/C++, both ABIs - no .dll needed)
  lpeg 1.1.0 (+ re.lua), lrexlib (rex_posix, rex_pcre2 with PCRE2 10.44; rex_pcre = rex_pcre2),
  luasql.sqlite3 (LuaSQL 2.7 + SQLite 3.50.4), lanes.core 4.0.1 (+ lanes.lua), xml.core (lubyk xml + lub),
  cjson, effil, lfs, socket / mime (luasocket), ssl (luasec + LibreSSL), base64 (lbase64), mimgui renderer

Lua libraries in lib/ (from their own sources)
  Penlight (pl), inspect, lume, lustache, argparse, compat53, copas, lua-websockets (websocket), lua-requests
  (requests), lua-htmlparser, lua-path (path), xml2lua + xmlhandler, aeslua, SNET, carbJsonConfig, arizona-events,
  extensions-lite, SAMemory, fa-icons (lua-fa-icons-4), fAwesome5 / fAwesome6, tabler_icons, rkeys, vkeys, moonloader-bind
  (bind), windows.message, encoding, inicfg, jsoncfg, MoonMonet, mimgui + mimgui_hotkeys / mimhotkey / mimtoasts /
  imspinner / ADDONS / mimgui_addons / notifications, samp.events (SAMP.Lua), sampfuncs, memory, monethook, strings,
  UTF8 / utf8 / utf8data, basexx, md5, sha1, timerwheel, binaryheap, vector3d, matrix3x3, webviews, android (JNI)

What works on MP servers
  samp.events: onServerMessage, onChatMessage, onShowDialog, onPlayerJoin, onPlayerQuit,
               onSendChat, onSendCommand, onSendDialogResponse (read, edit or block)
  sampAddChatMessage, sampSendChat, sampProcessChatInput, sampRegisterChatCommand,
  sampShowDialog, sampHasDialogRespond, sampSendDialogResponse, sampIsDialogActive,
  sampGetPlayerNickname / Color / Score / Ping, sampIsPlayerConnected, sampGetMaxPlayerId,
  sampGetCharHandleBySampPlayerId, sampGetPlayerIdByCharHandle, sampGetCurrentServerName / Address,
  isSampLoaded / isSampfuncsLoaded / isSampAvailable = true, every game opcode (CLEO functions).

Not on MP servers / not on Android (honest list)
  - SA-MP RakNet sync (sampSendOnfootData / IncarData / AimData, sampSpawnPlayer, sampRequestClass, textdraw /
    object / vehicle id RPCs): MP has its own sync. Scripts that only use these do nothing (no crash).
  - Libraries that read samp.dll memory on PC (SF.lua, SAMP-API.lua, RakLua, EntityRender d3d9): there is no
    samp.dll on Android. MoonAdditions (Windows .dll) and luatcc (libtcc emits x86 code) are not included.
  - MonetLoader runs on GTA San Andreas. Vice City uses MP's own Lua bridge, not MonetLoader.

The mods folder
  moonloader/mods_loader.lua starts every .lua file directly in moonloader/mods/. A mod keeps its files in its
  own folder next to it (that folder's .lua files are NOT started - they are the mod's data).
  Cheat mods (a "MP_CHEAT_MOD" line at the top) follow the CheatMenu rule:
    - offline (single player): allowed
    - the default / LAN server and the featured servers (Brief Case War, MW3, Mega Vil, DayZ, League): never
    - a custom server: only while its owner has "Allow CheatMenu + MonetLoader cheats" on (Server Management,
      or MP Launcher > host settings > Cheat Menu), and "Block menu cheats" off. Off by default.
  mods_loader.lua checks every second (MP_LuaCheatsAllowed() in libMP): when the owner turns it off, or you join a
  server that doesn't allow it, the cheat mod stops at once and starts again where it is allowed.

txAdmin
  txadmin.lua + lib/webviews/ + lib/android/ - "txAdmin Open Source" 4.6.0-webview by 4trays LazyCat Studios (MIT,
  https://github.com/ThirtyFoxMCx00E1/MonetLoader-txAdmin). An admin panel in an Android WebView: player list, duty,
  goto / gethere / spec / freeze / slap / sethp, kick / ban / warn / mute / jail with reasons, vehicles. Open it with
  /tx or /txadmin (/txlock, /txlogout).
