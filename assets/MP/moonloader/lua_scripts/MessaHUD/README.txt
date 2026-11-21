MessaHUD 1.2c-mp - Android port of mesahud 1.2c by hq~ (LibertyCity.Ru)
================================================================================

The original (Monet loader/messahud.zip) is a compiled PC MoonLoader script (messahud.luac) that draws with d3d9
render functions and moves the radar with radarrect.asi, set up with the right mouse button. None of that runs on
Android, so MessaHUD.lua is a rewrite with the same look and the original pictures, sounds and fonts (mesahud/).

  MessaHUD.lua              the mod (started by lua_scripts_loader.lua)
  mesahud/images/           icons and weapon icons of the original
  mesahud/sounds/           click, perfect, mod_godlike, holyshit (mini game)
  mesahud/fonts/            Sansation, GTEestiProText (pick one in the settings)
  config: moonloader/config/messahud.ini

Chat     /messahud          settings window (MessaHUD style: black, orange frame)
         /messahud on|off   show / hide the HUD
         /messahud move     drag the HUD and the FPS box, tap Done
         /messahud game     mini game: tap the orange target before it shrinks away

HUD      FPS + ping box, time and date, nickname (or your own text = "encrypted nick"), health / armour / sprint /
         breath bars (car health in a car) with numbers on or off, weapon icon + ammo in clip / rest, money with dots,
         six wanted stars. Every block can be turned off.
Game HUD the game's own player info (bars, weapon, money, clock, stars) is hidden while MessaHUD is on (libMP
         5.02.39 MP_HudHideInfo). Tapping the weapon still switches weapons. Not with Advanced Settings installed
         (it owns the HUD); then both show - turn its player info off there.
Not here the radar settings (radarrect.asi, PC only) and right mouse button setup; the radar stays the game's.
