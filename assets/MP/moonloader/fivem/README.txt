MP - moonloader/fivem/  (fivem_loader.lua 1.0-mp)
================================================================================

FiveM client Lua resources for GTA SA. moonloader/fivem_loader.lua starts every folder here that has an
fxmanifest.lua (or __resource.lua): its shared_scripts and client_scripts run with a FiveM-style API
(Citizen.CreateThread / Wait, RegisterCommand, events, vector3, PlayerPedId, GetEntityCoords, DrawText, DrawRect,
DrawLine, DrawMarker, World3dToScreen2d, time / weather, GetHashKey, LoadResourceFile, json ...).
Natives that have no GTA SA version return nil and are written once to moonloader.log.
Server scripts, NUI (HTML pages) and streamed GTA V files (.ytd .ydr .ymt) can't run on SA.

Chat     /fivem                       resource list (Start / Stop / Restart)
         /fivem list | start <name> | stop <name> | restart <name>
Off      a file named "disabled" in the resource folder (Stop in /fivem makes it).
Wait     start_on_command 'name' in fxmanifest.lua (MP key): the resource stays stopped until /name is typed,
         then it starts and runs its own /name. After a stop it waits again.

Inside
  fs-graphics/   FS-Graphics by IDK_FORCE (https://github.com/JebetrexxMC/fs-graphics) for SA: /fsgraphics.
                 Blends the colours of its GTA V timecycles into SA's timecycle (lib/fs_graphics.lua,
                 lib/fs_graphics_data.lua): Better Water / Rain / Colors / Reflections / Clouds / Nights / Sky /
                 StreetLights / FPS Increase. /fsgraphics starts it (start_on_command).
                 Presets: /fsgraphics preset full | nosky (game sky and clouds) | noclouds (game clouds hidden). An approximation of the look - SA can't load GTA V files, ReShade or
                 explosion.ymt (Better Fire).
  example-hud/   a small FiveM-style HUD script: /hudexample (starts it - start_on_command).

API names follow the FiveM Lua SDK (https://github.com/JebetrexxMC/fivem-lua-sdk), copied to lib/fivem-sdk/
as a reference for editors (it is documentation stubs, not loaded).
