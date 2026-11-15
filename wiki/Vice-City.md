# GTA Vice City

The same libMP runs in GTA Vice City Mobile. A Vice City phone only joins Vice City servers, and a San Andreas phone only joins San Andreas ones.

## Install

1. Copy `libMP32.so` / `libMP64.so` to the AML mods folder of GTA Vice City.
2. Copy the MP folder to `files/TYFMC/MP` in the Vice City data folder.
3. Set the server with `Host=` in `files/TYFMC/MP/vc.ini`, or with the **[ MP ]** text button in the game.

## Controls

The text buttons at the top of the screen are `[ Chat ] [ Players ] [ MP ] [ Class ]`.

## What is synced

- **Players:** skins, walk / run / sprint / crouch, weapons in their hands, damage with armour and kill credit
- **Weapons:** bullets with tracers, grenades, molotovs and rockets, explosions and fires
- **Cars:** rotation, colours, health, passengers, carjacking
- **World:** interiors, dropped weapons, radar blips, time and weather
- **Interface:** name tags, chat, commands, server dialogs and textdraws

## Gamemodes

Vice City servers use VC:MP-style gamemodes:
- classes
- teams with no friendly fire
- server cars
- a kill feed

## Server

```
./mp_server --game vc --port 7777 "My Vice City Server"
```
