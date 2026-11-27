# Christe Multiplayer (com.christe.mp)

The MP launcher for **GTA San Andreas (AML)** and **GTA Vice City** on Android: SA + VC + PC crossplay, always landscape.

- **Uses the games you already have.** Play starts the installed AML GTA SA (`com.rockstargames.gtasa`) or Vice City (`com.rockstargames.gtavc`, through ViceLoader when it is installed). They keep reading their own OBB (`Android/obb/com.rockstargames.gtasa/main.8…obb` + `patch.8…obb`), so nothing is downloaded or copied twice. The OBB check also looks in `Android_unprotected/obb` and `Android_unprotected/data/<game>/obb`.
- **Joins the server you pick.** The server goes to the game as Intent extras (`christe_join` = `mp|name|port|host` or `omp|host|port|name`, `christe_nick`); libMP **5.02.76+** joins it from the title screen (the neon join: Multiplayer load, then the join). Vice City: ViceLoader's bridge `127.0.0.1:47790` (`JOIN <port> <host>\t<pass>`).
- **One app for both CPUs.** arm64-v8a (GTA SA 2.10) or armeabi-v7a (GTA SA 2.00), picked from the installed game (else the phone); the matching libMP from the APK is copied to `date2/libs/<abi>/libMP.so`. Settings > Game can force one.
- **Own folders, no clash with GTA SA:** `Android/data/com.christe.mp/date2/` with `resources`, `libs`, `files`, `configs` (`christe.ini`), `mods`, `scripts`, `logs`. Own OBB (optional): `Android/obb/com.christe.mp/main.<n>.com.christe.mp.obb` (a zip, unpacked into `date2/resources`).
- **UI:** half Hellion / SA-MP 0.3.7 / Gamey / Shockbyte server browser, half MP neon. Sidebar (Home, Internet, Favorites, Explore Community, Communities, About, Settings), top bar (search or `host:port`, region pin, refresh, notifications, quick-settings gear, profile). Tapping the guest profile opens the full-screen MP account login / register (the same accounts as the game and Moquil), with an X back to the launcher. There is no Display page: display settings stay in libMP.
- **Boot:** an animated start-up screen (or your own `date2/resources/startup.mp4`, skippable) hands off to the launcher once the list is in. Skeleton rows while loading; "Something went wrong, try turning on Wi-Fi or mobile data" with Retry when offline, and a "Lost connection" bar.

Build: `bash christe/build.sh` (aapt2 + javac + d8, no Gradle) or the **Christe Multiplayer** workflow (`.github/workflows/christe.yml`, artifact `ChristeMultiplayer`).
Android 11+ does not let one app write into another app's `Android/data`, so MP files for the game still go into GTA SA's AML mods folder; `date2/libs` and `date2/mods` hold the copies.
