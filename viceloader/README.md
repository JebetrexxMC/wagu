# VICE Loader (GTA Vice City) - 4.13.64

A display-overlay app (`SYSTEM_ALERT_WINDOW`) that gives GTA Vice City the MP launcher from the San Andreas side.
Vice City has no AML floating menu like SAUtils on San Andreas, so VICE Loader draws it over the game.

- ImGui overlay with the same SA launcher look (`vl_kit.inc` comes from `src/mp_launcher.inc` / `mp_social.inc`).
- San Andreas MP floating bar: ⋮ (dropdown: Help / Server / Player list), Chat (type field + MP virtual keyboard), mic (voice chat, hold = Voice page), MonetLoader. Drag = move.
- Pages: Internet / Featured (online list, the 24/7 VICE City LAN server first) / Favorites, Add server, Chat, Players, Voice chat, MonetLoader, Help, Mods, Settings (Player, Display, Chat, Sound, Connection), About.
- global_click on every button; chat with server colours, `{RRGGBB}` codes, name colours and timestamps.
- No authentication: nickname + CONNECT.
- Bridge to MP in the game: UDP `127.0.0.1:47790` (`src/vc/vc_overlay.inc`).
  - Overlay -> game: `HELLO`, `PING`, `CHAT`, `JOIN <port> <host>\t<pass>`, `NAME`, `LEAVE`, `MIC`.
  - Game -> overlay: `STATE`, `CHAT <ABGR> <text>`, `PLAYERS`, `MIC <state>`, `BYE`.
- VICE City LAN server: runs online 24/7 (`server/run_24_7.sh`, GitHub Actions), port 7777, tags `vc,lan`, separate from the SA servers. Join it from Internet.
- Bundle (`assets/bundle`, exported to Download/ViceLoader): AML (`prebuilt/`, built from AndroidModLoader + ARMPatch), libMP32/64 (MonetLoader 3.9.1-mp built in, no separate libmonetloader.so), moonloader/lib.

## Build

```bash
source ~/env.sh          # JAVA_HOME (17+), NDK_HOME, ANDROID_HOME (build-tools 34, platform 34)
bash viceloader/build_apk.sh     # -> viceloader/ViceLoader.apk (~55 MB)
```

Set `KEYSTORE` / `KEYPASS` to sign with your own key. Without them, a local debug key is used.
`bin/libMP*.so` must be the current CI build so the bridge is in the game library.
