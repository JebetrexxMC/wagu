VICE Loader 5.00.01 - files exported by the app to Download/ViceLoader

mods/libMP32.so, libMP64.so      MP Multiplayer 5.00.01 (San Andreas + Vice City, VICE Loader bridge)
                                 MonetLoader 3.9.1-mp (mimgui fix) is built into libMP - no separate libmonetloader.so
moonloader/lib/                  Lua libraries (mimgui, imgui, monethook, SA classes) patched for 3.9.1-mp

Only MP is in here (5.00.01): no separate AML, SAUtils or MonetLoader files.

Install on GTA Vice City (a game APK that already loads mods):
  1. Copy mods/libMP32.so (32-bit game) or mods/libMP64.so (64-bit game) to
     Android/data/com.rockstargames.gtavc/mods/
  2. Start VICE Loader, give "Display over other apps", tap Start overlay, then Launch Vice City.
     The floating bar [ ... ][ chat ][ mic ][ moon ] opens the MP launcher (Internet / Add server, chat, keyboard).
