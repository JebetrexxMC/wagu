# Server Management

Open MP Launcher > Server Management while you host a server.

## What you can manage

- **Permissions:** mission pairing, CheatMenu and MonetLoader cheats, and "Block menu cheats".
- **Players:** bans, moderators, rules and dialogs.
- **Extras:** Plugins and the Textdraw Editor.
- **Admin and moderator chat commands.**

## MonetLoader - HG menu (4.13.58)

- **Status:** this section shows whether HG can run right now. The possible states are:
  - on
  - off on featured servers
  - off with "Block menu cheats"
  - off because the host turned cheats off
- **"HG menu on by default for my custom servers":** on by default.
  - New custom servers you host start with HG / MonetLoader cheats allowed.
  - Switching it on while you are hosting applies at once.
- **Turning cheats off:** if you turn CheatMenu + MonetLoader cheats off for your server, HG stops for every player within about a second.
- **Default and featured servers** (Mega Vil, DayZ, MW3, Brief Case War, League) never allow it.

These rules are checked by tests on a real server and client (`test/cheatperm_test.cpp`) and on the real `mods_loader.lua` (`test/mods_loader_test.py`).
