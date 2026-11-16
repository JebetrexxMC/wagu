# VICE Loader (GTA Vice City) - 4.13.63

A display-overlay app (`SYSTEM_ALERT_WINDOW`) that gives GTA Vice City the MP launcher from the San Andreas side.
Vice City has no AML floating menu like SAUtils on San Andreas, so VICE Loader draws it over the game.

- ImGui overlay with the same SA launcher look (`vl_kit.inc` comes from `src/mp_launcher.inc` / `mp_social.inc`).
- Floating "VL" bubble: tap = launcher, long-press = chat with the MP virtual keyboard.
- Pages: Internet / Favorites / Lan, Add server, VICE City LAN server, Chat (type field + keyboard), Players, Mods, Settings, About.
- No authentication: nickname + CONNECT.
- Bridge to MP in the game: UDP `127.0.0.1:47790` (`src/vc/vc_overlay.inc`).
  - Overlay -> game: `HELLO`, `PING`, `CHAT`, `JOIN <port> <host>\t<pass>`, `NAME`, `LEAVE`, `LANSTART <port> <max> <name>`, `LANSTOP`.
  - Game -> overlay: `STATE`, `CHAT <ARGB> <text>`, `PLAYERS`, `LAN`, `BYE`.
- VICE City LAN server: hosted in the app (`files/vclan`) or in the game. Port 7777, tags `vc,lan`. It is separate from the SA servers.
- Bundle (`assets/bundle`, exported to Download/ViceLoader): AML (`prebuilt/`, built from AndroidModLoader + ARMPatch), libMP32/64, MonetLoader 3.9.1-mp, moonloader/lib.

## Build

```bash
source ~/env.sh          # JAVA_HOME (17+), NDK_HOME, ANDROID_HOME (build-tools 34, platform 34)
bash viceloader/build_apk.sh     # -> viceloader/ViceLoader.apk (~70 MB)
```

Set `KEYSTORE` / `KEYPASS` to sign with your own key. Without them, a local debug key is used.
`bin/libMP*.so` must be the current CI build so the bridge is in the game library.
