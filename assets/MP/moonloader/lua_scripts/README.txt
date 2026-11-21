MP - moonloader/lua_scripts/  (lua_scripts_loader.lua 1.4-mp)
================================================================================

Put MoonLoader / MonetLoader scripts here. moonloader/lua_scripts_loader.lua (started by MonetLoader like any
script in moonloader/) starts them:

  *.lua / *.luac          started from here or any sub folder (moonloader-scripts/arizona/AutoLock.lua ...)
  *.zip                   unpacked to .unpacked/<zip name>/ (only again when the zip changes), its scripts started
                          from there. The scripts can be at the top of the zip, in a folder, or in a moonloader/
                          (or monetloader/) folder like the PC packages - see hitmarker.zip.
  lib/ config/ resource/ resources/ and a folder named like a script (tvplayer/ for tvplayer.lua)
                          copied into moonloader/ - only the files that are not there yet, so MP's libraries and
                          your settings are never overwritten.
  a zip without a script  (a data pack, like the tvplayer.zip videos) copied into moonloader/.

Left out of zips: Windows files (.dll .asi .exe .bat), MoonLoader's own luajit/, luarocks/ and libstd/ folders.
PC scripts that write paths with '\' work: the loader starts them through a runner in .run/ that loads
lib/mp_pcpaths.lua first ('\' -> '/' in io.open, doesFileExist, loadAudioStream, imgui textures, lfs ...).

Mod folders    a folder (or a zip) here that has its own lib/ is a self-contained mod (like Hasslehud/): its
               lib/ config/ resource/ files stay inside it, getWorkingDirectory() is that folder for the mod,
               its lib/ is searched after moonloader/lib (MP's Android mimgui, samp.events ... win), and a .so
               of the mod that can't load on this game returns a stub instead of killing the mod.
On / Off       moon icon on the top bar > Script Manager: the switch next to a script is saved in
               moonloader/config/mp_scripts_off.txt and kept after a restart or a rejoin (checked every second).
disabled.txt   names that are not started (templates/, bb.lua ... - edit it).
Cheat scripts  (a "MP_CHEAT_MOD" line, clickwarp.lua, AutoRob.lua, bunnyhopfix.lua) follow MP's CheatMenu rule:
               offline and on custom servers whose owner turned on "Allow CheatMenu + MonetLoader cheats".
New files      are picked up within 5 seconds. Chat: /luascripts, /luascripts list, /luascripts rescan.
Log            moonloader.log, lines "[LuaScripts] ...".

Inside:
  moonloader-scripts/   https://github.com/JebetrexxMC/moonloader-scripts (ins1x, PC MoonLoader scripts for
                        SA-MP servers: Arizona, Absolute Play, TRAINING, UIF ... - see its README.md). Scripts for a
                        server only do something on that server; Windows-only parts of a script fail in
                        moonloader.log instead of crashing.
  hitmarker.zip         "Dynamic Hitmarker" by fausto200 (https://ko-fi.com/fausto200), the PC MoonLoader package
                        (moonloader/hitmarker.lua + moonloader/resources/hitmarker/), started straight from the zip.
  Hasslehud/            HassleHUD Android 1.0 by SurtzyX and GustaX (from Hasslehud/HassleHUD Android 1.0 -
                        Monetloader.zip): self-contained mod folder, /hasslehud opens its panel. See Hasslehud/README.txt.
  MessaHUD/             MessaHUD 1.2c by hq~ (Monet loader/messahud.zip), rewritten for Android: /messahud settings
                        (MessaHUD style window), /messahud on|off|move|game. See MessaHUD/README.txt. Use one HUD at a
                        time: /messahud off, or switch HassleHUD off.
  WeaponX/              Weapon Wheel 1.4.6 by mistus_ (https://libertycity.net/files/file234128.html), 1.4.6-mp:
                        tap WX (a button you can drag) or type /weaponx, tap a weapon. /weaponx cfg or /wheelcfg
                        settings, /wheel on / off. Clip ammo comes from lib/mp_weaponslots.lua on Android.
  DynamicAmmoHUD/       Dynamic Ammo HUD & Animated Crosshair 2.0 by Fausto200
                        (https://libertycity.net/files/file246763.html), 2.0-mp: /ammohud menu, /ammohud on|off,
                        /ammohud calib. On Android the game's crosshair is hidden through a hook, aiming is read from
                        the camera, the sprint bar is off (its PC memory offset means something else on the phone).
  AutoCleanStreaming/   AutoCleanStreaming V2 by sebicu (https://libertycity.net/files/file242203.html): frees
                        streaming memory. /memclean window, /memclean now, /memclean on|off.
  AdvancedTacticalCompass.lua  MW3-style compass, a new Lua version of zConnor.i22's PC mod
                        (https://libertycity.net/files/gta-san-andreas/239690-advanced-tactical-compass.html):
                        /compass settings, /compass mode, /compass top|bottom, /compass hide.
  fpp_realistic.lua     first person camera at the head bone: /fpp on / off, /fppcfg settings. Drag on the right
                        half of the screen to look around.
  mod_commands.lua      /mods: every mod command with a button.
