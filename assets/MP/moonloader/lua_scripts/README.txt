MP - moonloader/lua_scripts/  (lua_scripts_loader.lua 1.0-mp)
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

disabled.txt   names that are not started (templates/, bb.lua ... - edit it).
Cheat scripts  (a "MP_CHEAT_MOD" line, clickwarp.lua, AutoRob.lua, bunnyhopfix.lua) follow MP's CheatMenu rule:
               offline and on custom servers whose owner turned on "Allow CheatMenu + MonetLoader cheats".
New files      are picked up within 5 seconds. Chat: /luascripts, /luascripts list, /luascripts rescan.
Log            moonloader.log, lines "[LuaScripts] ...".

Inside:
  moonloader-scripts/   https://github.com/ThirtyFoxMCx00E1/moonloader-scripts (ins1x, PC MoonLoader scripts for
                        SA-MP servers: Arizona, Absolute Play, TRAINING, UIF ... - see its README.md). Scripts for a
                        server only do something on that server; Windows-only parts of a script fail in
                        moonloader.log instead of crashing.
  hitmarker.zip         "Dynamic Hitmarker" by fausto200 (https://ko-fi.com/fausto200), the PC MoonLoader package
                        (moonloader/hitmarker.lua + moonloader/resources/hitmarker/), started straight from the zip.
