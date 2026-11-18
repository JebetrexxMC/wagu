@everyone # Changelogs

- fixed players getting a second slot when they reconnect: no more "try to reconnecting" for someone who is still playing, and no more "left the server" + "joined the server" for a player who never left

- the 24/7 server handover and host takeovers no longer flood the chat with "joined the server" for players who were already there

- "{name} reconnected to the server successfully" when someone is back after a real network drop

- MonetLoader for MP 4.0.0: the old guessed lib folder and the remade HG menu, hitmarker and TV player mods are gone (old files are renamed to .old on the first start), moonloader/lib now has the real libraries from their sources

- new built-in Lua modules for both phones (32 and 64 bit): lpeg, lrexlib (regex with PCRE2), luasql sqlite3, lanes, xml

- PC MoonLoader scripts that use PC game addresses (readMemory, callFunction, memory.*) now reach the same function or variable in the Android game

- PC imgui scripts (require 'imgui', imgui.Process, OnDrawFrame) now run on mimgui

- .dll / .asi loaded from a Lua mod now look for the Android .so build next to the script

- Custom GTA HUD now works with Advanced Settings installed: MP writes the Compact HUD (clock above the money, health and armor right under it, next to the weapon icon) into the Advanced Settings ini, with Apply again / Restore my ini buttons in Settings > Display > GTA HUD

- the AMMO number on the right side (Settings > Textdraw > Ammo Info) is off on the default server

- new Add server option: Custom welcome message, shown to players who join your server instead of "Welcome to <server>" ({name} and {server} work)

- the payday chat line no longer says (CGRP)

- new notification system: admins and moderators on the default server (and host owners on custom servers with the new Notification system gamemode) can send pop-up notifications with /notify embed: logo: message: timer: start: end:, pick the logo from the list, set the start and end time with AM / PM, everyone gets a card sliding in from the left with a sound

- the default server HUD now matches the Broker MQQ logo: red and brown hexagons and a red top-right score bar

- fix servers showing offline (Brief Case War, VICE City and sometimes all of them): the HiveMQ public broker stopped delivering messages, now the game and the 24/7 servers detect a broker like that in 5 seconds and move to a working one (Mosquitto first), the server list comes back by itself

- fix login / sign up / Discord account sync hanging on "connecting" in the game, Moquil and MP Textdraw Editor: if the login bot doesn't answer in 6 seconds it tries the next broker

- the GTA HUD is now the compact one by default: clock right above the money, health and armor bars right under it next to the weapon icon, same sizes as the game, no Advanced Settings needed

- fix the custom GTA HUD radar drawing green spaghetti lines and making the pause menu map half size with no icons, the radar now moves as one piece

- fix the JOINED / Leave / Follow buttons covering the community info line in the launcher

- your MP account now stays logged in after a game restart, a rejoin or a mod update (game, Moquil and MP Textdraw Editor), the login is kept in MP/auth_service.txt and comes back by itself if the file is deleted

- new /create user: pass: gender: age: in Discord makes your game account right there, and /delete removes one so you can /create a new one on the same Discord

- /forgotpass sets a new password and your phones stay logged in, /forgotuser shows your usernames and your phones and writes MP/accounts.txt again

- fix Rocket League spawning you stuck inside the LV airport buildings when you don't have the SA-MP models, now everyone plays on the sky pitch

- the Moquil Library no longer shows the generated pictures of the mods and apps, now they show unknown

- fix players walking or running backwards was using the running animation (the Michael Jackson moonwalk), now they turn and face where they go like the real GTA SA, and turning while moving backwards is normal again

- fix other players arm not raising when they press the aim / scope button, now lock-on, free aim and the sniper / rocket / camera scope raise the arm on your screen too (spray can and extinguisher also)

- the map editor /mapedit find and /mapping find now search objects by how they look, type "wood pallet", "red barrel" or "bench" and it finds them even if you don't know the model name

- the websites and HiveMQ now use the MP app icon in the browser tab and search results instead of the placeholder title logo

- the Moquil app 1.4 is now included inside the full zip (server roles, channels, moderation, polls and the Discord-style avatar decorations)

[Multiplayer Client FULL 5.00.05](https://github.com/ThirtyFoxMCx00E1/wagu/raw/main/assets/MP/MP.zip)
