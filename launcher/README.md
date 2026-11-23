# MP Launcher (PC)

Windows launcher for SA-MP / open.mp servers and MP Crossplay, built with [Tauri 2](https://github.com/JebetrexxMC/tauri) (the fork, branch `dev`).

- Server list from api.open.mp, featured MP servers, favourites, live ping and players (SA-MP query `i` / `p`).
- NetWire status (`wires.json` from the netwire-status branch) picks the fastest crossplay path; fallback `wap-fresh.tun.ply.gg:56616`.
- Play: starts `gta_sa.exe -c -n <name> -h <ip> -p <port> [-z <password>]`, injects `samp.dll`, then `omp-client.dll` when it is in the game folder (the same join flow as the official launcher).
- The game path and name come from the SA-MP registry key (`HKCU\Software\SAMP`) or are set in Settings.

## Build

```
cd launcher/src-tauri
cargo build --release --target i686-pc-windows-msvc   # 32-bit, like gta_sa.exe
cargo install tauri-cli --version ^2 && cargo tauri build --target i686-pc-windows-msvc   # NSIS installer
```

CI: `.github/workflows/launcher.yml` builds the Windows exe and installer on every change in `launcher/`.

## Notes

- The join flow follows [openmultiplayer/launcher](https://github.com/openmultiplayer/launcher) (MPL-2.0). This launcher is its own code and is not affiliated with or endorsed by open.mp; it does not use the open.mp name or logos as branding.
- Join starts gta_sa.exe suspended, loads samp.dll (then omp-client.dll) with LoadLibraryW in a remote thread, then resumes it. The launcher is 32-bit for that reason. Windows only; on Linux / macOS the launcher shows the list and ping but cannot start the game.
- Not tested on a real Windows PC yet. Please report problems with a screenshot of the error message.
