#!/usr/bin/env python3
# Makes the README / wiki feature GIFs. No screenshot is used: every frame is generated on a PC by running
# the mod's own code:
#   radar / sync : src/mp_radar.inc + src/mp_compass.inc (tools/preview/radar_preview.cpp), real SA map data
#   combat       : assets/MP/moonloader/mods/hitmarker.lua run in Lua (tools/preview/hitmarker_preview.py)
#   megavil_rp   : src/mp_rp_inv.inc and src/mp_rp_atm.inc (tools/preview/rp_inv_preview.cpp, rp_atm_preview.cpp)
#   phone        : src/mp_phone.inc (tools/preview/phone_preview.cpp), the phone and calls on every server
#   monetloader  : assets/MP/moonloader/mods_loader.lua run by test/mods_loader_test.py (HG on / off rules)
# The backgrounds are drawn here (gradient sky, street, buildings), so nothing comes from Screenshots/.
# Build the C++ previews first (see the "build:" line at the top of each .cpp) into /tmp/, then
#   python3 tools/preview/make_gifs.py [radar|sync|combat|rp|monetloader|phone]
import os, sys, math, random, subprocess
from PIL import Image, ImageDraw, ImageFont, ImageFilter

OUT = 'Screenshots/gifs'
W, H = 720, 324
BOLD = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
MONO = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'
def font(sz, f=BOLD):
    try: return ImageFont.truetype(f, sz)
    except Exception: return ImageFont.load_default()

# ---------------------------------------------------------------- generated backgrounds
def street_bg(w, h, seed=3, blur=6):
    """A dusk street drawn from scratch: sky gradient, sun, buildings with windows, road with lane marks."""
    rnd = random.Random(seed)
    im = Image.new('RGB', (w, h)); d = ImageDraw.Draw(im)
    hor = int(h * 0.55)
    for y in range(hor):
        t = y / hor
        d.line((0, y, w, y), fill=(int(40 + 200 * t), int(50 + 90 * t), int(110 + 30 * t)))
    d.ellipse((w * 0.62, hor - h * 0.16, w * 0.62 + h * 0.2, hor + h * 0.04), fill=(255, 196, 120))
    x = -20
    while x < w:
        bw = rnd.randint(int(w * 0.04), int(w * 0.11)); bh = rnd.randint(int(h * 0.12), int(h * 0.42))
        c = rnd.randint(28, 52); d.rectangle((x, hor - bh, x + bw, hor), fill=(c, c + 4, c + 14))
        for wy in range(hor - bh + 8, hor - 6, 14):
            for wx in range(x + 6, x + bw - 6, 12):
                if rnd.random() < 0.35: d.rectangle((wx, wy, wx + 5, wy + 7), fill=(255, 214, 140))
        x += bw + rnd.randint(2, 14)
    for y in range(hor, h):
        t = (y - hor) / (h - hor); g = int(46 + 30 * t); d.line((0, y, w, y), fill=(g, g, g + 6))
    vx = w * 0.5
    d.polygon([(vx - w * 0.02, hor), (vx + w * 0.02, hor), (w * 0.95, h), (w * 0.05, h)], fill=(58, 58, 64))
    for k in range(8):
        t0, t1 = (k / 8) ** 2, ((k + 0.45) / 8) ** 2
        y0, y1 = hor + (h - hor) * t0, hor + (h - hor) * t1
        d.polygon([(vx - 1 - 6 * t0, y0), (vx + 1 + 6 * t0, y0), (vx + 1 + 6 * t1, y1), (vx - 1 - 6 * t1, y1)], fill=(230, 210, 120))
    return im.filter(ImageFilter.GaussianBlur(blur)) if blur else im

def caption(fr, text):
    d = ImageDraw.Draw(fr, 'RGBA'); f = font(16)
    d.rectangle((0, fr.height - 28, fr.width, fr.height), fill=(0, 0, 0, 180))
    d.text((10, fr.height - 24), text, font=f, fill=(255, 255, 255)); return fr

def fit(im, fx=0.5, fy=0.5):
    im = im.convert('RGB'); sw, sh = im.size; s = max(W / sw, H / sh)
    im = im.resize((int(sw * s) + 1, int(sh * s) + 1), Image.LANCZOS)
    x = int((im.width - W) * fx); y = int((im.height - H) * fy)
    return im.crop((x, y, x + W, y + H))

def save(name, frames, ms, colors=128):
    pal = [f.quantize(colors=colors, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE) for f in frames]
    path = os.path.join(OUT, name + '.gif')
    pal[0].save(path, save_all=True, append_images=pal[1:], duration=ms, loop=0, optimize=True)
    print(path, os.path.getsize(path) // 1024, 'KB', len(frames), 'frames')

def hold_fade(imgs, hold=1800, fade=4, ms=70):
    frames, durs = [], []
    for k, im in enumerate(imgs):
        frames.append(im); durs.append(hold)
        nx = imgs[(k + 1) % len(imgs)]
        for t in range(fade): frames.append(Image.blend(im, nx, (t + 1) / (fade + 1))); durs.append(ms)
    return frames, durs

def run(cmd, env=None):
    e = dict(os.environ); e.update(env or {})
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=e, check=True)

# ---------------------------------------------------------------- radar (2D, 3D, compass)
ROUTE = [(2495, -1670), (2230, -1670), (2230, -1450), (2050, -1450)]
def route_points(n=14):
    pts = []
    for (ax, ay), (bx, by) in zip(ROUTE, ROUTE[1:]):
        for i in range(n): pts.append((ax + (bx - ax) * i / n, ay + (by - ay) * i / n, math.degrees(math.atan2(-(bx - ax), by - ay)) % 360))
    return pts

def radar():
    street_bg(3200, 1440, seed=5, blur=10).save('/tmp/_bg.png')
    frames = []
    for mode, label in ((1, 'MP radar 2D: roads, water and parks from the game map data'), (2, 'MP radar 3D: the tilted map turns with you'), (3, 'MP compass: heading, waypoints and distance')):
        for x, y, hd in route_points():
            run(['/tmp/radar_preview', '/tmp/_bg.png', '/tmp/_r.ppm', str(mode), str(x), str(y), '13', str(hd)])
            im = Image.open('/tmp/_r.ppm').convert('RGB')
            im = im.crop((0, 905, 1000, 1355)) if mode != 3 else im.crop((700, 0, 2500, 810))
            frames.append(caption(fit(im, 0.0, 0.5), label))
    save('radar', frames, 90)

# ---------------------------------------------------------------- sync: other players' blips at the 20 Hz sync rate
def sync():
    street_bg(3200, 1440, seed=9, blur=10).save('/tmp/_bg.png')
    pts = route_points(16)
    me = pts
    # three other players, positions only change when a sync packet "arrives" (every 50 ms = MP_SYNC_RATE_MS,
    # at 90 ms per GIF frame that is every frame) - one walks behind you, one drives the other way, one waits at the corner
    others = [lambda i: pts[max(0, i - 6)][:2], lambda i: pts[len(pts) - 1 - min(len(pts) - 1, i * 2 % len(pts))][:2], lambda i: (2232, -1600)]
    cols = ['F59E0BFF', 'A855F7FF', 'EF4444FF']
    frames = []
    for i, (x, y, hd) in enumerate(me):
        pins = ';'.join('%.1f,%.1f,%s' % (*o(i), c) for o, c in zip(others, cols))
        run(['/tmp/radar_preview', '/tmp/_bg.png', '/tmp/_r.ppm', '1', str(x), str(y), '13', str(hd), '220'], {'MP_PINS': pins})
        im = fit(Image.open('/tmp/_r.ppm').convert('RGB').crop((0, 905, 1000, 1355)), 0.0, 0.5)
        d = ImageDraw.Draw(im, 'RGBA'); f = font(15)
        d.rounded_rectangle((W - 250, 14, W - 14, 118), 10, fill=(0, 0, 0, 150))
        d.text((W - 238, 22), 'Players online: 4', font=f, fill=(255, 255, 255))
        for k, (n, c) in enumerate((('Senji_Macase', cols[0]), ('Bea_Cruz', cols[1]), ('Miko_Reyes', cols[2]))):
            rgb = tuple(int(c[j:j + 2], 16) for j in (0, 2, 4))
            d.ellipse((W - 238, 48 + k * 22, W - 226, 60 + k * 22), fill=rgb)
            d.text((W - 218, 44 + k * 22), n, font=f, fill=(255, 255, 255))
        frames.append(caption(im, 'Sync: every player and car is sent 20 times a second (port 1922)'))
    save('freeroam_sync', frames, 90)

# ---------------------------------------------------------------- combat: the real Hitmarker mod
def combat():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hitmarker_preview as hm
    bg = street_bg(hm.SW, hm.SH, seed=11, blur=4)
    cx, cy = hm.SW * 0.53, hm.SH * 0.40
    def target(img, t):   # a generated stand-in for the ped under the crosshair
        d = ImageDraw.Draw(img); s = 1.0
        d.ellipse((cx - 14, cy - 66, cx + 14, cy - 38), fill=(70, 52, 44))
        d.rounded_rectangle((cx - 26, cy - 38, cx + 26, cy + 34), 10, fill=(52, 96, 60))
        d.rectangle((cx - 22, cy + 34, cx - 4, cy + 100), fill=(30, 30, 36)); d.rectangle((cx + 4, cy + 34, cx + 22, cy + 100), fill=(30, 30, 36))
    ev = [(0.25, 24, 24, 0), (0.65, 24, 24, 0), (1.15, 31, 31, 4), (1.6, 38, 31, 12), (2.3, 46, 34, 2), (3.1, 120, 34, 3)]
    labels = [(0.0, 'Body hit: damage number'), (0.6, 'Combo: x2, x3 ...'), (1.1, 'Armour hit (blue), then the armour breaks'), (2.2, 'Headshot (gold) with the skull'), (3.0, 'Kill: red marker, ring and sparks')]
    raw = hm.run(ev, 4.0, fps=20, bg=bg, scene=target)
    frames = []
    for i, im in enumerate(raw):
        t = i / 20; lab = [l for s, l in labels if s <= t][-1]
        c = im.crop((int(cx - 300), int(cy - 135), int(cx + 300), int(cy + 135))).resize((W, H), Image.LANCZOS)
        frames.append(caption(c, 'Hitmarker mod: ' + lab))
    save('combat', frames, 50)

# ---------------------------------------------------------------- Mega Vil RP: inventory, documents, ATM
def rp():
    street_bg(3200, 1440, seed=21, blur=12).save('/tmp/_bg.png')
    shots = [('/tmp/rp_inv_preview', 0, 'Mega Vil RP: quick panel (vehicle, phone, documents, bag, status)'), ('/tmp/rp_inv_preview', 1, 'Mega Vil RP: inventory, items and weight'),
             ('/tmp/rp_inv_preview', 2, 'Mega Vil RP: ID card and documents'), ('/tmp/rp_atm_preview', 0, 'Mega Vil RP: ATM'),
             ('/tmp/rp_atm_preview', 1, 'Mega Vil RP: ATM - typing an amount'), ('/tmp/rp_atm_preview', 5, 'Mega Vil RP: ATM - cash out')]
    imgs = []
    for exe, mode, lab in shots:
        run([exe, '/tmp/_bg.png', '/tmp/_r.ppm', str(mode)])
        imgs.append(caption(fit(Image.open('/tmp/_r.ppm').crop((420, 150, 2780, 1212))), lab))
    frames, durs = hold_fade(imgs, 2000)
    save('megavil_rp', frames, durs, colors=160)

# ---------------------------------------------------------------- MonetLoader: HG rules, the real mods_loader.lua
def monetloader():
    out = subprocess.run([sys.executable, 'test/mods_loader_test.py'], capture_output=True, text=True).stdout.splitlines()
    lines = [l for l in out if l.startswith(('PASS', 'FAIL'))] + [l for l in out if 'failure' in l]
    f, fh = font(15, MONO), font(17)
    frames, durs = [], []
    for k in range(len(lines) + 1):
        im = Image.new('RGB', (W, H), (16, 18, 24)); d = ImageDraw.Draw(im)
        d.rectangle((0, 0, W, 34), fill=(36, 40, 52))
        d.text((12, 8), 'moonloader/mods_loader.lua - HG on / off rules (test/mods_loader_test.py)', font=fh, fill=(255, 255, 255))
        for j, l in enumerate(lines[:k][-14:]):
            col = (90, 220, 120) if l.startswith('PASS') else ((240, 80, 80) if l.startswith('FAIL') else (240, 220, 120))
            d.text((14, 44 + j * 19), l, font=f, fill=col)
        frames.append(caption(im, 'HG runs offline and on custom servers, stops when cheats are off'))
        durs.append(2600 if k == len(lines) else 380)
    save('monetloader', frames, durs, colors=32)

# ---------------------------------------------------------------- phone + calls on every server: the real src/mp_phone.inc
def phone():
    # the phone widgets come from src/mp_rp_phone.inc (lines 59-94), the phone itself is src/mp_phone.inc
    with open('src/mp_rp_phone.inc') as f: open('/tmp/phone_widgets.inc', 'w').write(''.join(f.readlines()[58:94]))
    run(['g++', '-O2', '-std=c++17', '-w', '-Isrc', '-Iimgui', '-I/tmp', 'tools/preview/phone_preview.cpp', 'imgui/imgui.cpp', 'imgui/imgui_draw.cpp',
         'imgui/imgui_tables.cpp', 'imgui/imgui_widgets.cpp', '-o', '/tmp/phone_preview'])
    street_bg(3200, 1440, seed=27, blur=12).save('/tmp/_bg.png')
    shots = [(0, 'Phone (/phone): players online, tap Call'), (1, 'Calling Carl_Johnson... CJ holds the phone to his ear'),
             (2, "Carl's phone rings: Answer or Decline"), (3, 'On the call: voice goes to the call anywhere on the map'),
             (4, 'Call ended'), (6, 'The other player is busy on another call'), (5, 'Phone put away: the icon shakes while it rings')]
    imgs = []
    for mode, lab in shots:
        run(['/tmp/phone_preview', '/tmp/_bg.png', '/tmp/_r.ppm', str(mode)])
        imgs.append(caption(fit(Image.open('/tmp/_r.ppm').crop((440, 100, 3196, 1340))), lab))
    frames, durs = hold_fade(imgs, 2000)
    save('phone_call', frames, durs, colors=128)

os.makedirs(OUT, exist_ok=True)
JOBS = {'radar': radar, 'sync': sync, 'combat': combat, 'rp': rp, 'monetloader': monetloader, 'phone': phone}
for j in (sys.argv[1:] or JOBS):
    JOBS[j]()
