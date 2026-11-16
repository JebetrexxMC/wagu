VICE Loader 4.13.63 - files exported by the app to Download/ViceLoader

AML/<abi>/libAML.so              AndroidModLoader (built from ThirtyFoxMCx00E1/AndroidModLoader + ARMPatch)
mods/libMP32.so, libMP64.so      MP Multiplayer 4.13.63 (San Andreas + Vice City, VICE Loader bridge)
monetloader/<abi>/libmonetloader.so   MonetLoader 3.9.1-mp (mimgui fix, embedded Lua libs)
moonloader/lib/                  Lua libraries (mimgui, imgui, monethook, SA classes) patched for 3.9.1-mp

Install on GTA Vice City (AML-patched APK):
  1. Copy libAML.so for your phone's ABI into the game (AML installer) if the game is not patched yet.
  2. Copy mods/libMP32.so (32-bit game) or mods/libMP64.so (64-bit game) to
     Android/data/com.rockstargames.gtavc/mods/
  3. Start VICE Loader, give "Display over other apps", tap Start overlay, then Launch Vice City.
     The floating VL bubble opens the MP launcher (servers, VICE City LAN server, chat, keyboard).

SAUtils is not included: its source needs RusJJ's private GTASA struct headers and it targets
San Andreas; Vice City menus are drawn by the VICE Loader overlay instead.
