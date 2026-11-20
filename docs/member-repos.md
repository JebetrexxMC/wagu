# Member repos (101) - what they are

Read one by one (README + file tree) for 5.02.15. Path: `third_party/omp-dev/members/<owner>/<repo>`.

## Findings for crossplay / kicks

- **Alasnkz/RakNet**: RakNet 2.52 as modified for open.mp. This is the protocol wagu's gate speaks (third_party/omp-raknet); kept for comparison.
- **gurkansahinn/ghostbuster.asi** and **NexiusTailer/Nex-AC**: server anti-cheats check money, health, armour and weapons against what the server gave. That is why 5.02.15 sends PlayerStatsSync (packet 205) and caps health, armour and weapons to what the server set.
- **ADRFranklin/samp-udp-proxy**: a UDP proxy in front of a SA-MP server. Some servers sit behind one, so the first packets can be dropped; the gate retries the join up to 5 times.
- **THE-FYP/SAMP.Lua**: MoonLoader (PC) SA-MP events. It shows which RPC ids PC dialogs and textdraws use, and matches the gate's RPC 61/134/105 handling.
- **AmyrAhmady/sampvoice**, **samp-extended**, **BigETI/SA-MP-PlusPlus**: Windows-only client plugins (x86 DLL/ASI). They can't be injected into the Android game; reference only.
- **multitheftauto/mtasa-blue**, **in0finite/SanAndreasUnity**: other GTA SA multiplayer engines; reference only.
- The rest are Pawn libraries, server plugins, gamemodes and tools. They don't change the phone client.

## Client / network (crossplay-relevant) (12)

| Repo | Main files | About |
|---|---|---|
| [ADRFranklin/samp-udp-proxy](https://github.com/ADRFranklin/samp-udp-proxy) | .rs7 | The idea of this application is to provide a proxy inbetween the clients and a SA-MP server, in which certain |
| [Alasnkz/RakNet](https://github.com/Alasnkz/RakNet) | .h61 .cpp27 | This is a modified version of RakNet 2.52 used in open.mp. |
| [AmyrAhmady/multi-process-gtasa](https://github.com/AmyrAhmady/multi-process-gtasa) | .cpp1 | With this ASI mod you are able to run GTASA multiple times, without game pause on window focus loss |
| [AmyrAhmady/samp-extended](https://github.com/AmyrAhmady/samp-extended) | .h216 .cpp171 |  |
| [AmyrAhmady/sampvoice](https://github.com/AmyrAhmady/sampvoice) | .h215 .cpp171 | English / [Русский](https://github.com/AmyrAhmady/sampvoice/blob/master/README.ru.md) |
| [BigETI/SA-MP-PlusPlus](https://github.com/BigETI/SA-MP-PlusPlus) |  | Client and server plugin for a GTA: San Andreas Multiplayer modification (www.sa-mp.com) |
| [Fernando-A-Rocha/mta-add-models](https://github.com/Fernando-A-Rocha/mta-add-models) | .lua19 | This MTA resource makes use of the clientside allocated models ([engineRequestModel](https://wiki.multitheftau |
| [THE-FYP/SAMP.Lua](https://github.com/THE-FYP/SAMP.Lua) | .lua8 | SAMP.Lua is a lua library for MoonLoader that adds some features to make SA:MP modding simpler. |
| [gurkansahinn/ghostbuster.asi](https://github.com/gurkansahinn/ghostbuster.asi) | .cpp1 .asi1 | SA-MP için hazırlanmış bir anti hile eklentisi. |
| [gurkansahinn/lpz-asi](https://github.com/gurkansahinn/lpz-asi) | .h200 .cpp131 | LPZ GTA:SA asi source code. |
| [in0finite/SanAndreasUnity](https://github.com/in0finite/SanAndreasUnity) | .cs101 | <img src="https://i.imgur.com/aIojfPW.png" width="320" height="320"> |
| [multitheftauto/mtasa-blue](https://github.com/multitheftauto/mtasa-blue) | .h176 .cpp175 .lua4 | [Multi Theft Auto](https://www.multitheftauto.com/) (MTA) is a software project that adds network play functio |

## Pawn / server libraries and plugins (36)

| Repo | Main files | About |
|---|---|---|
| [Alasnkz/SA-MP-MySQL](https://github.com/Alasnkz/SA-MP-MySQL) | .cpp12 .pwn4 | / Travis CI / AppVeyor / Total downloads / Latest release / |
| [AmyrAhmady/CreateWater](https://github.com/AmyrAhmady/CreateWater) | .pwn1 .inc1 | Create CUSTOM WATERS in SA:MP and swim like a real one! |
| [AmyrAhmady/omp-bigint](https://github.com/AmyrAhmady/omp-bigint) | .inc1 .cpp1 | A lightweight open.mp server component that adds signed 64-bit integer support to Pawn scripts. It exposes a ` |
| [AmyrAhmady/omp-ipc](https://github.com/AmyrAhmady/omp-ipc) | .cpp18 .js1 |  |
| [AmyrAhmady/omp-node](https://github.com/AmyrAhmady/omp-node) | .cpp27 .js1 | * [CMake 3.19+](https://cmake.org/) |
| [AmyrAhmady/omp-ssl-test](https://github.com/AmyrAhmady/omp-ssl-test) | .cpp1 | open.mp SSLTest Component |
| [AmyrAhmady/samp-node-test](https://github.com/AmyrAhmady/samp-node-test) | .h147 .cpp11 .pwn1 | a SA-MP plugin to support nodejs apps and run them on your server |
| [AmyrAhmady/samp-player-gangzones](https://github.com/AmyrAhmady/samp-player-gangzones) | .inc2 .pwn1 | Creating per player gangzones to avoid normal (global) samp gangzones limit (1024). |
| [AmyrAhmady/samp-plugin-sdk](https://github.com/AmyrAhmady/samp-plugin-sdk) | .h5 .cpp1 | This is a copy of the SA-MP plugin SDK files extracted from MapAndreas (a plugin by Kalcor). It is the bare mi |
| [AmyrAhmady/samp-streamer-node](https://github.com/AmyrAhmady/samp-streamer-node) | .cpp45 .h21 .inc1 | This plugin streams objects, pickups, checkpoints, race checkpoints, map icons, 3D text labels, and actors at |
| [BigETI/DTest](https://github.com/BigETI/DTest) | .inc2 .pwn1 | Simply install to your project: |
| [BigETI/Dini](https://github.com/BigETI/Dini) | .inc2 .pwn1 | Simply install to your project: |
| [BigETI/SA-MP-Player-GTA-Folder-Checker](https://github.com/BigETI/SA-MP-Player-GTA-Folder-Checker) | .cs3 .pwn1 | This is a simple system which allow server admins/players to watch each other players gta folder files incase |
| [BigETI/pawn-list](https://github.com/BigETI/pawn-list) | .pwn1 .inc1 | This is a linked list implementation in PAWN. |
| [BigETI/pawn-map](https://github.com/BigETI/pawn-map) | .pwn1 .inc1 | This is a map data structure implementation in PAWN using binary search tree. |
| [BigETI/pawn-strtok](https://github.com/BigETI/pawn-strtok) | .pwn1 .inc1 | Impress your friends with your coding skills by using `strtok` |
| [BigETI/pawn-vector](https://github.com/BigETI/pawn-vector) | .pwn1 .inc1 | This is a vector data structure implementation in PAWN. |
| [BigETI/samp-releases](https://github.com/BigETI/samp-releases) | .inc19 .pwn14 .cpp3 | This is a collection of SA:MP releases I made. The earliest release was from 2011. |
| [BigETI/samp-shell](https://github.com/BigETI/samp-shell) | .h7 .cpp2 .inc1 | This plugin allows you to execute shell commands on your server. |
| [Ino-Bagaric/Dialog-Center-Text](https://github.com/Ino-Bagaric/Dialog-Center-Text) | .pwn1 .inc1 | SAMP Dialog Center Text |
| [JebetrexxMC/pawn-map](https://github.com/JebetrexxMC/pawn-map) | .pwn1 .inc1 | This is a map data structure implementation in PAWN using binary search tree. |
| [MichaelBelgium/PlayerCameraRotateFromPoint](https://github.com/MichaelBelgium/PlayerCameraRotateFromPoint) | .pwn1 .inc1 | A San Andreas Multiplayer include to let the camera of a player rotate around a position. |
| [Southclaws/samp-camera-sequencer](https://github.com/Southclaws/samp-camera-sequencer) | .pwn2 .inc2 | A library for creating camera sequences. A camera sequence is a set of camera |
| [adri1336/MAP-MOVER](https://github.com/adri1336/MAP-MOVER) | .inc6 .pwn2 |  |
| [alexchwoj/pawn-easing-functions](https://github.com/alexchwoj/pawn-easing-functions) | .pwn4 .inc1 | A library that implements easing functions into the Pawn language, and an animation system for textdraws. This |
| [dakyskye/pawn-env](https://github.com/dakyskye/pawn-env) | .rs3 .pwn1 .inc1 | Acces environment variables in PAWN |
| [gurkansahinn/EasyDialogs-for-SA-MP-Node](https://github.com/gurkansahinn/EasyDialogs-for-SA-MP-Node) | .inc1 | a SA-MP plugin to support nodejs apps and run them on your server. |
| [iPollo/PawnAlgorithms](https://github.com/iPollo/PawnAlgorithms) | .pwn20 | The repository is a collection of open-source implementation of a variety of algorithms implemented in PAWN an |
| [iPollo/stdlib](https://github.com/iPollo/stdlib) | .inc24 | Work in progress open.mp libraries. |
| [oscar-broman/PAWN-Boilerplate](https://github.com/oscar-broman/PAWN-Boilerplate) | .inc93 .pwn1 | Please note that this project is not yet officially released. |
| [oscar-broman/samp-introspect](https://github.com/oscar-broman/samp-introspect) | .inc5 .pwn1 | Load the AMX debug information during runtime. Access non-public variables and functions. |
| [peterszombati/route-tracing](https://github.com/peterszombati/route-tracing) | .pwn1 .inc1 | Path layout for San Andreas Multiplayer. |
| [pkfln/node-amx](https://github.com/pkfln/node-amx) | .ts18 .js12 | An AMX interpreter for Node.js. - Typed and enhanced version of Kester Everts' node-amx library. |
| [samp-incognito/samp-gvar-plugin](https://github.com/samp-incognito/samp-gvar-plugin) |  | SA-MP GVar Plugin |
| [sanpezlo/td-notification](https://github.com/sanpezlo/td-notification) | .inc2 .pwn1 | This Include allows you to create TextDraw Notifications |
| [udan11/samp-plugin-timerfix](https://github.com/udan11/samp-plugin-timerfix) | .h5 .cpp4 .inc1 | SA-MP Plugin - Timerfix |

## Tools / editors / launchers / docs (42)

| Repo | Main files | About |
|---|---|---|
| [Alasnkz/IDA_ParseSAMPNatives](https://github.com/Alasnkz/IDA_ParseSAMPNatives) | .py1 | This is a IDA python script to automatically parse SA-MP natives for easy use in IDA. |
| [Alasnkz/rook](https://github.com/Alasnkz/rook) | .rs7 | An experimental parser for the Pawn language. |
| [AmyrAhmady/DeAMX](https://github.com/AmyrAhmady/DeAMX) | .lua5 | DeAMX - .amx files decompiler for SA:MP originally made by trc_ in 2008 |
| [AmyrAhmady/omp-code-editor](https://github.com/AmyrAhmady/omp-code-editor) | .ts41 .rs2 .pwn2 | This template should help get you started developing with Tauri, React and Typescript in Vite. |
| [AmyrAhmady/ompide](https://github.com/AmyrAhmady/ompide) | .ts58 .rs26 .inc3 | <img src="assets/branding/ompide.png" alt="ompide logo" width="160" /> |
| [AmyrAhmady/openmp-models](https://github.com/AmyrAhmady/openmp-models) | .ts74 .js1 | The open.mp model library is a Next.js application for browsing vehicle, skin, and object catalogues and previ |
| [AmyrAhmady/samap](https://github.com/AmyrAhmady/samap) |  | San Andreas high quality satellite map from GTA SA (48000 x 48000) |
| [AmyrAhmady/samp-cmake-modules](https://github.com/AmyrAhmady/samp-cmake-modules) |  | Name                                             / Description |
| [AmyrAhmady/ssma](https://github.com/AmyrAhmady/ssma) | .js6 | ***Not finished!*** |
| [BigETI/CGLL](https://github.com/BigETI/CGLL) | .cs14 | The community game launcher library was designed to provide an easy way to create a game launcher with modding |
| [BigETI/GTA3ScriptSharp](https://github.com/BigETI/GTA3ScriptSharp) | .cs32 | GTA3Script sharp is a library to disassemble, compile or run GTA III, GTA Vice City, GTA San Andreas, GTA Libe |
| [BigETI/GTAAudioSharp](https://github.com/BigETI/GTAAudioSharp) | .cs23 | GTAAudioSharp is a C♯ library to read and write GTA audio files. |
| [BigETI/IMGSharp](https://github.com/BigETI/IMGSharp) | .cs12 | IMGSharp is a C♯ library to read and write GTA IMG files. |
| [BigETI/PawnSource](https://github.com/BigETI/PawnSource) | .c35 .h18 .cpp16 | This is a PAWN compiler used in the SA:MP community, which is called "the russian compiler". |
| [BigETI/San-Andreas-Sound-Test](https://github.com/BigETI/San-Andreas-Sound-Test) | .cs4 |  |
| [BigETI/SanAndreasOGGDumper](https://github.com/BigETI/SanAndreasOGGDumper) | .cs2 | San Andreas OGG Dumper is a command line tool to dump OGG data from a GTA San Andreas installation. |
| [BigETI/SanAndreasSFXDumper](https://github.com/BigETI/SanAndreasSFXDumper) | .cs2 | San Andreas SFX Dumper is a command line tool to dump SFX data from a GTA San Andreas installation. |
| [BigETI/lcmp-hashes](https://github.com/BigETI/lcmp-hashes) |  |  |
| [BigETI/samp-hashes](https://github.com/BigETI/samp-hashes) |  |  |
| [BigETI/vcmp-hashes](https://github.com/BigETI/vcmp-hashes) |  |  |
| [JebetrexxMC/SAMPServerAnnouncer](https://github.com/JebetrexxMC/SAMPServerAnnouncer) | .cs9 | This software is a drop-in replacement for the existing "announce" executables shipped with the San Andreas Mu |
| [JebetrexxMC/SAMPcmdNET](https://github.com/JebetrexxMC/SAMPcmdNET) | .cs3 | `SAMPcmdNET` is a command line tool to launch San Andreas Multiplayer. |
| [JebetrexxMC/sampcmd](https://github.com/JebetrexxMC/sampcmd) | .h1 .cpp1 | `sampcmd` is a command line tool to launch San Andreas Multiplayer. |
| [Se8870/SAMP-File-Archive](https://github.com/Se8870/SAMP-File-Archive) |  | The fork of the original repo, the purpose of this repo is just providing you easiest way to download the arch |
| [Southclaws/cj](https://github.com/Southclaws/cj) | .go143 .ts12 .js1 | This project is open to anyone who wants to contribute, large or small! Whether you noticed a typo or want to |
| [WoutProvost/pawn-const-correctness-fixer](https://github.com/WoutProvost/pawn-const-correctness-fixer) | .pwn2 | When you have [Perl](https://www.perl.org/) installed (Linux has this installed by default), execute the follo |
| [adib-yg/openmp-server-installation](https://github.com/adib-yg/openmp-server-installation) |  | **This tutorial is for those who want to transfer their gamemode from SA:MP server to open.mp server.** |
| [daddyDOT/pawn-lang-md](https://github.com/daddyDOT/pawn-lang-md) |  | This repository is about the files that I'm currently editing / creating. |
| [gurkansahinn/OPEN-Launcher](https://github.com/gurkansahinn/OPEN-Launcher) |  | SA-MP Open source server launcher. |
| [gurkansahinn/sweet-bot](https://github.com/gurkansahinn/sweet-bot) | .js250 .ts12 | (TR) SA-MP Geliştirici topluluğu için özel olarak geliştirilen açık kaynak kodlu basit discord botu. |
| [gurkansahinn/sweet_go](https://github.com/gurkansahinn/sweet_go) | .go9 | (TR) SA-MP Geliştirici topluluğu için geliştirilen Sweet(Node-JS)'in golang ile tekrar yazılmış sürüm |
| [katursis/samp-cmake-modules](https://github.com/katursis/samp-cmake-modules) |  | CMake modules for SA:MP |
| [midosvt/samp-sounds](https://github.com/midosvt/samp-sounds) |  | A lot of sounds aren't listed in the official SA-MP/Open.mp wiki, like at [this link](https://www.open.mp/docs |
| [openmultiplayer/pawn-sublime-language](https://github.com/openmultiplayer/pawn-sublime-language) | .py5 | Sublime Text is an extremely feature-rich and customisable plain-text editor. |
| [openmultiplayer/vscode-pawn](https://github.com/openmultiplayer/vscode-pawn) | .ts10 | VS-Code extension for Pawn language |
| [pawnkit/pawn-analysis](https://github.com/pawnkit/pawn-analysis) | .go62 | Shared Pawn preprocessing and semantic analysis for PawnKit tools. |
| [pawnkit/pawn-parser](https://github.com/pawnkit/pawn-parser) | .go85 .inc39 .pwn7 | Reusable Go lexer and concrete-syntax-tree parser for the Pawn language used by |
| [pawnkit/pawnlint](https://github.com/pawnkit/pawnlint) | .go214 | Static analysis and linting for Pawn v3 (SA-MP and open.mp). |
| [pawnkit/pawnlsp](https://github.com/pawnkit/pawnlsp) | .go23 | An LSP server for SA-MP/open.mp Pawn. It consumes shared PawnKit libraries for project discovery, analysis, AP |
| [punkochel/pawn-openmp-sublime-kit](https://github.com/punkochel/pawn-openmp-sublime-kit) | .py2 | This plugin is a fork of [Pawn syntax](https://github.com/Southclaws/pawn-sublime-language), visiting the pare |
| [samp-dev/node-generate](https://github.com/samp-dev/node-generate) | .ts16 | Generates type definition files for samp-node |
| [sampctl/runtimes](https://github.com/sampctl/runtimes) |  | Server runtime manifests that sampctl uses to download files - including but not limited to SA:MP. |

## Gamemodes / example scripts (11)

| Repo | Main files | About |
|---|---|---|
| [GalardoDev/samp-v-deathmatch](https://github.com/GalardoDev/samp-v-deathmatch) | .inc21 .pwn1 | Feel free to use it, |
| [Southclaws/samp-Hellfire](https://github.com/Southclaws/samp-Hellfire) | .pwn142 | This is a SA:MP gamemode script that aims to contain as many features as possible, |
| [blint96/mysantos-main](https://github.com/blint96/mysantos-main) | .inc170 .pwn1 .h1 | Kod źródłowy oprogramowania na platformie SA-MP do projektu mySantos |
| [bufu1337/GTA-Scripts](https://github.com/bufu1337/GTA-Scripts) | .pwn82 .inc81 .lua5 |  |
| [gurkansahinn/SA-MP-SaCounty-Roleplay](https://github.com/gurkansahinn/SA-MP-SaCounty-Roleplay) | .inc36 .pwn2 | Roleplay Gamemode - SAMP |
| [gurkansahinn/SA-MP-TypeScriptDeathmatch](https://github.com/gurkansahinn/SA-MP-TypeScriptDeathmatch) | .ts311 .js19 .pwn1 | SA-MP typescript deathmatch gamemode |
| [gurkansahinn/SA-MP-Zombieland-Roleplay](https://github.com/gurkansahinn/SA-MP-Zombieland-Roleplay) | .inc296 .pwn9 .h1 | NPC(AI ZOMBİE System), Craft System, Inventory System |
| [gurkansahinn/SAMPSON-EXAMPLE](https://github.com/gurkansahinn/SAMPSON-EXAMPLE) | .pwn2 | SAMPSON kütüphanesi için örnek giriş sistemleri. |
| [gurkansahinn/SMF-ZombieTemplate](https://github.com/gurkansahinn/SMF-ZombieTemplate) | .js1 | Default Theme: newbalance_20 |
| [gurkansahinn/lpz-gamemode-samp](https://github.com/gurkansahinn/lpz-gamemode-samp) | .inc49 .pwn1 | Last Project Z - Gamemode |
| [midosvt/MW3OMP](https://github.com/midosvt/MW3OMP) | .inc10 .pwn1 | Call Of Duty Modern Warefare 3 or MW3 is a TDM SA:MP gamemode. The aim of the game is to choose a team, captur |
