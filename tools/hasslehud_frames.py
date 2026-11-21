#!/usr/bin/env python3
"""HassleHUD fist GIFs -> 60 fps sprite sheets (MP 5.02.42).

HassleHUD decodes its fist GIF on the phone (libGIF.so extractGif -> .gif/N.bmp) and then shows one frame
every 0.07 s (about 14 fps), whatever the GIF's own speed is. This tool does that work once, on the PC:

  resource/HassleHUD/fistN.gif -> resource/HassleHUD/frames/fistN/sheet0.png, sheet1.png ... + info.txt

* real GIF timing: every frame is held for its own delay, then the clip is resampled to 60 fps;
* the in-between frames cross-fade between two GIF frames, so a 14-25 fps GIF plays smoothly at 60 fps;
* the frames are packed into 2048 x 2048 sheets (one texture upload for up to 100 frames instead of 100 BMP files).

MP's lua_scripts_loader.lua then gives HassleHUD one placeholder frame and draws the right 60 fps cell itself.

  python3 tools/hasslehud_frames.py [Hasslehud/resource/HassleHUD] [--cell 192] [--fps 60]
"""
import math
import os
import sys

from PIL import Image, ImageSequence

SHEET = 2048


def frames_of(path):
    """Fully composited RGBA frames and their delays (s)."""
    im = Image.open(path)
    out, delays = [], []
    for fr in ImageSequence.Iterator(im):
        out.append(fr.convert('RGBA'))
        d = fr.info.get('duration', im.info.get('duration', 100)) or 100
        if d < 20:          # browsers treat 0-10 ms as 100 ms
            d = 100
        delays.append(d / 1000.0)
    return out, delays


def convert(gif, outdir, cell, fps):
    src, delays = frames_of(gif)
    total = sum(delays)
    n = max(1, int(round(total * fps)))
    w0, h0 = src[0].size
    s = min(1.0, cell / float(max(w0, h0)))
    cw, ch = max(1, int(round(w0 * s))), max(1, int(round(h0 * s)))
    small = [f.resize((cw, ch), Image.LANCZOS) for f in src]
    starts, t = [], 0.0
    for d in delays:
        starts.append(t)
        t += d
    cols, rows = SHEET // cw, SHEET // ch
    per = cols * rows
    sheets = int(math.ceil(n / float(per)))
    os.makedirs(outdir, exist_ok=True)
    for f in os.listdir(outdir):
        if f.startswith('sheet') or f == 'info.txt':
            os.remove(os.path.join(outdir, f))
    cur, dims = None, []
    for i in range(n):
        if i % per == 0:
            if cur is not None:
                cur.save(os.path.join(outdir, 'sheet%d.png' % (i // per - 1)), optimize=True)
            left = n - i
            r = int(math.ceil(min(left, per) / float(cols)))
            cur = Image.new('RGBA', (cols * cw if left >= cols else left * cw, r * ch), (0, 0, 0, 0))
            dims.append(cur.size)
        tt = i / float(fps)
        k = 0
        while k + 1 < len(starts) and starts[k + 1] <= tt:
            k += 1
        a = (tt - starts[k]) / delays[k]
        nxt = small[(k + 1) % len(small)]
        # hold most of the GIF frame, cross-fade only near its end (keeps the frames sharp)
        fade = max(0.0, min(1.0, (a - 0.5) / 0.5))
        img = small[k] if fade <= 0 else Image.blend(small[k], nxt, fade)
        j = i % per
        cur.paste(img, ((j % cols) * cw, (j // cols) * ch))
    cur.save(os.path.join(outdir, 'sheet%d.png' % ((n - 1) // per)), optimize=True)
    with open(os.path.join(outdir, 'info.txt'), 'w') as f:
        f.write('frames=%d\nfps=%d\ncols=%d\nrows=%d\ncw=%d\nch=%d\nsheets=%d\n' % (n, fps, cols, rows, cw, ch, sheets))
        for k, (w, h) in enumerate(dims):
            f.write('sheet%d=%d %d\n' % (k, w, h))
    return n, len(src), total, (cw, ch), sheets


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    root = args[0] if args else 'assets/MP/moonloader/lua_scripts/Hasslehud/resource/HassleHUD'
    cell = int(sys.argv[sys.argv.index('--cell') + 1]) if '--cell' in sys.argv else 192
    fps = int(sys.argv[sys.argv.index('--fps') + 1]) if '--fps' in sys.argv else 60
    for g in sorted(os.listdir(root)):
        if g.lower().endswith('.gif'):
            stem = g[:-4]
            n, k, t, sz, sh = convert(os.path.join(root, g), os.path.join(root, 'frames', stem), cell, fps)
            print('%-10s %3d GIF frames %.2f s -> %3d frames @%d fps, %dx%d, %d sheet(s)' % (g, k, t, n, fps, sz[0], sz[1], sh))


if __name__ == '__main__':
    main()
