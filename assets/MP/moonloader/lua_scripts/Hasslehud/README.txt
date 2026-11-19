HassleHUD Android 1.0 - MonetLoader (SurtzyX, GustaX)
================================================================================

Started by moonloader/lua_scripts_loader.lua 1.1-mp as a self-contained mod folder: everything it needs stays here.

  HassleHUD Android.luac     the mod
  config/HassleHUD.json      its settings (written by the mod)
  lib/                       its own libraries (SAMemory, fAwesome6, encoding, json, libGIF.so ...). moonloader/lib
                             is searched first, so MP's Android mimgui / samp.events / monetloader are used.
  resource/HassleHUD/        pictures, fonts and the fist GIF

Use      /hasslehud in chat opens the panel (HUD / CORES / CRÉDITOS). The panel keeps HassleHUD's own look:
         it is drawn by MonetLoader's mimgui, not by MP's windows.
Switch   moon icon on the top bar > Script Manager > lua_scripts/ > Hasslehud > switch. Kept after a restart.
Notes    libGIF.so is a 32-bit ARM file: on a 64-bit game the fist animation stays empty, the rest works.
         Left out of the zip: Windows .dll files, .svn data, ~ backups and "(1)" copies.
