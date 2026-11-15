#!/usr/bin/env python3
# Makes the README feature GIFs from the real in-game screenshots in Screenshots/ (slow pan/zoom + crossfade)
# and from the real radar / compass drawing code (tools/preview/radar_preview.cpp) along a driven path.
# run from the repo root: python3 tools/preview/make_gifs.py   (needs /tmp/radar_preview built, see radar_preview.cpp)
import os, math, subprocess
from PIL import Image, ImageDraw, ImageFont
SS = 'Screenshots'; OUT = 'Screenshots/gifs'
FS = sorted(f for f in os.listdir(SS) if f.lower().endswith(('.png', '.jpg', '.jpeg', '.webp')))
W, H = 560, 252
try: FONT = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 15)
except Exception: FONT = ImageFont.load_default()

def fit(im, z, fx, fy):
    im = im.convert('RGB'); sw, sh = im.size
    s = max(W / sw, H / sh) * z; nw, nh = int(sw * s) + 1, int(sh * s) + 1
    im = im.resize((nw, nh), Image.LANCZOS)
    x = int((nw - W) * fx); y = int((nh - H) * fy)
    return im.crop((x, y, x + W, y + H))

def caption(fr, text):
    d = ImageDraw.Draw(fr, 'RGBA'); d.rectangle((0, H - 24, W, H), fill=(0, 0, 0, 170))
    d.text((8, H - 21), text, font=FONT, fill=(255, 255, 255)); return fr

def slideshow(name, items, hold_ms=1900, fade=4, ms=80):
    # every picture is held (one frame, long duration) and crossfades into the next
    imgs = [caption(fit(Image.open(os.path.join(SS, FS[i])), 1.0, 0.5, 0.5), t) for i, t in items]
    frames, durs = [], []
    for k, im in enumerate(imgs):
        frames.append(im); durs.append(hold_ms)
        nx = imgs[(k + 1) % len(imgs)]
        for t in range(fade): frames.append(Image.blend(im, nx, (t + 1) / (fade + 1))); durs.append(ms)
    save(name, frames, durs)

def save(name, frames, ms):
    pal = [f.convert('P', palette=Image.ADAPTIVE, colors=96, dither=Image.NONE) for f in frames]
    path = os.path.join(OUT, name + '.gif')
    pal[0].save(path, save_all=True, append_images=pal[1:], duration=ms, loop=0, optimize=True)
    print(path, os.path.getsize(path) // 1024, 'KB', len(frames), 'frames')

def radar(name):
    # blurred street shot as the background, so the game's own radar in the screenshot doesn't show through
    from PIL import ImageFilter
    Image.open(os.path.join(SS, FS[12])).convert('RGB').resize((3200, 1440)).filter(ImageFilter.GaussianBlur(18)).save('/tmp/_rbg.png')
    bg = '/tmp/_rbg.png'
    path = [(2495, -1670), (2230, -1670), (2230, -1450), (2050, -1450)]
    pts = []
    for (ax, ay), (bx, by) in zip(path, path[1:]):
        n = 14
        for i in range(n): pts.append((ax + (bx - ax) * i / n, ay + (by - ay) * i / n, math.degrees(math.atan2(-(bx - ax), by - ay)) % 360))
    frames = []
    for mode, label in ((1, 'MP radar 2D: real roads from the game map data'), (2, 'MP radar 3D: tilted map that turns with you'), (3, 'MP compass: heading, waypoints and distance')):
        for x, y, hd in pts:
            subprocess.run(['/tmp/radar_preview', bg, '/tmp/_r.ppm', str(mode), str(x), str(y), '13', str(hd)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            im = Image.open('/tmp/_r.ppm').convert('RGB')
            im = im.crop((0, 760, 1680, 1440)) if mode != 3 else im.crop((700, 0, 2500, 810))
            frames.append(caption(fit(im, 1.0, 0.0, 0.5), label))
    save(name, frames, 90)

os.makedirs(OUT, exist_ok=True)
import sys
if sys.argv[1:] == ['radar']: radar('radar'); sys.exit()
slideshow('launcher', [(42, 'MP Launcher: Internet / Favorites / LAN servers, ping, players, mode'), (39, 'Server dialogs and rules'), (40, 'Server dialogs and rules'), (76, 'Server Management and scoreboard')])
slideshow('freeroam_sync', [(9, 'Freeroam: chat, voice, players and blips in sync'), (14, 'Players, animations and clothes synced'), (12, 'Synced players, vehicles and weather / time'), (17, 'Online with friends'), (35, 'Vehicles, passengers and damage synced')])
slideshow('combat', [(36, 'Combat: hits, damage and hit markers'), (48, 'Driveby and vehicle combat'), (37, 'Weapons and deaths synced'), (95, 'Car HUD: speedometer, fuel, health')])
slideshow('megavil_rp', [(13, 'Mega Vil Futuristic RP: spawn and city'), (19, 'RP interaction wheel'), (21, 'RP inventory and items'), (44, 'Jobs and interiors'), (46, 'Quintara Showroom'), (77, 'RP interaction menus')])
slideshow('dayz', [(50, 'Project DayZ: survival monitor'), (61, 'DayZ inventory and loot'), (64, 'Hunting in the wild'), (68, 'Radar, kills and stats'), (70, 'Survive the roads')])
slideshow('mw3', [(54, 'Call of Duty: Modern Warfare 3 Team DM'), (57, 'Killstreaks and pickups'), (59, 'Scoreboard')])
slideshow('maps_hud', [(23, 'Full map with player and property blips'), (29, 'Map with players, GPS and waypoints'), (98, 'HUD, chat and car speedometer'), (93, 'League: car football')])
slideshow('loading', [(74, 'Loading screen'), (92, 'Loading screen'), (99, 'Pause menu')])
slideshow('monetloader', [(101, 'MonetLoader mods: TV Player in a house'), (103, 'TV Player'), (105, 'TV Player'), (18, 'CLEO menu (offline / where allowed)')])
radar('radar')
