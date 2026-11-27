# AML Multiplayer (com.christe.mp, was Christe Multiplayer)

**1.2:**
- Renamed AML Multiplayer, with a new icon: black, an orange SA and a pink VC tied together.
- Connect never opens the GTA SA app on its own. The game's native libraries (lib/<abi>/ of the GTA SA APK and its splits) are unpacked into `app_gamelib/<abi>`, and readable AML mods next to GTA SA are copied into `Android/data/com.christe.mp/mods`.
- Server rows use the bg_server.jar pictures, with Gamemode and Loc. columns. Internet = MP servers, Featured = the MP 24/7 featured servers, Hosted = open.mp.
- Login shows your Discord profile picture.
- No Skip on the start-up screen.
- `AMLMultiplayer.apk` is signed with a fixed key.

**1.1:** GTA SA runs **inside Christe**, so Join no longer switches to the GTA SA app.
- The `com.christe.mp.Game` activity runs in the `:game` process with its own task. It extends the installed AML GTA SA's `com.rockstargames.gtasa.GTASA`, which is loaded from that APK at run time: `ChristeFactory` on Android 9+, `ChristeApp` on Android 5–8.
- It uses the game's resources and assets, Christe's own OBB (`Android/obb/com.christe.mp/main.8.com.christe.mp.obb` + `patch.8…`) and Christe's AML folders (`Android/data/com.christe.mp/mods` gets the bundled libMP, plus `files`, `configs`).
- Get the OBB in Storage & OBB: **Copy** from GTA SA, **Import** the .obb files, or **Allow access** on Android 11+. **Download** gets the full MP files into `files/TYFMC/MP`.
- Christe needs a GTA SA build that matches the phone's CPU: 2.10 for 64-bit, 2.00 for 32-bit. Otherwise, or when Settings > Game is set to "GTA SA app", it opens the installed game as 1.0 did. If the game crashes, the error is saved to `date2/logs/game_crash.txt` and shown the next time you open Christe.
- The UI is the SA-MP 0.3.7 browser with the MP launcher look: title bar, File/View/Servers/Tools/Help, tool bar + Name:, Internet/Featured/Favorites/Lan/open.mp, the Player/Score and Rule/Value pane, Favorites/Internet/Featured/Lan/Hosted, the status line and CONNECT. Boot and game start use the MP loading screen.
- Lan uses MP's own query (`PKT_QUERY` 14 → `PKT_INFO` 15) on ports 1922–1934, over Wi-Fi or a hotspot. MP Crossplay 24/7 [PC + Mobile] is not listed in Internet.
- `targetSdk` is 28 so the game's code keeps working.

The MP launcher for **GTA San Andreas (AML)** and **GTA Vice City** on Android: SA + VC + PC crossplay, always landscape.

- **Uses the games you already have.** Play starts the installed AML GTA SA (`com.rockstargames.gtasa`) or Vice City (`com.rockstargames.gtavc`, through ViceLoader when it is installed). They keep reading their own OBB (`Android/obb/com.rockstargames.gtasa/main.8…obb` + `patch.8…obb`), so nothing is downloaded or copied twice. The OBB check also looks in `Android_unprotected/obb` and `Android_unprotected/data/<game>/obb`.
- **Joins the server you pick.** The server goes to the game as Intent extras (`christe_join` = `mp|name|port|host` or `omp|host|port|name`, `christe_nick`); libMP **5.02.76+** joins it from the title screen (the neon join: Multiplayer load, then the join). Vice City: ViceLoader's bridge `127.0.0.1:47790` (`JOIN <port> <host>\t<pass>`).
- **One app for both CPUs.** arm64-v8a (GTA SA 2.10) or armeabi-v7a (GTA SA 2.00), picked from the installed game (else the phone); the matching libMP from the APK is copied to `date2/libs/<abi>/libMP.so`. Settings > Game can force one.
- **Own folders, no clash with GTA SA:** `Android/data/com.christe.mp/date2/` with `resources`, `libs`, `files`, `configs` (`christe.ini`), `mods`, `scripts`, `logs`. Own OBB (optional): `Android/obb/com.christe.mp/main.<n>.com.christe.mp.obb` (a zip, unpacked into `date2/resources`).
- **UI:** half Hellion / SA-MP 0.3.7 / Gamey / Shockbyte server browser, half MP neon. Sidebar (Home, Internet, Favorites, Explore Community, Communities, About, Settings), top bar (search or `host:port`, region pin, refresh, notifications, quick-settings gear, profile). Tapping the guest profile opens the full-screen MP account login / register (the same accounts as the game and Moquil), with an X back to the launcher. There is no Display page: display settings stay in libMP.
- **Boot:** an animated start-up screen (or your own `date2/resources/startup.mp4`, skippable) hands off to the launcher once the list is in. Skeleton rows while loading; "Something went wrong, try turning on Wi-Fi or mobile data" with Retry when offline, and a "Lost connection" bar.

Build: `bash christe/build.sh` (aapt2 + javac + d8, no Gradle) or the **Christe Multiplayer** workflow (`.github/workflows/christe.yml`, artifact `ChristeMultiplayer`).
Android 11+ does not let one app write into another app's `Android/data`, so MP files for the game still go into GTA SA's AML mods folder; `date2/libs` and `date2/mods` hold the copies.
