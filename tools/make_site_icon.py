#!/usr/bin/env python3
"""Website icon: SA (chrome, as in the title) above VC (pink) on a TRANSPARENT background (4.13.48).

usage: python3 tools/make_site_icon.py            (reads research/title.png, writes docs/ icons)

The letters are cut from the real title art (research/title.png, already transparent). The SA drop
shadow in that art is clipped into hard rectangles, which showed as grey boxes on dark/coloured
backgrounds, so only the soft dark shadow pixels (semi transparent + dark) are removed. The black
letter fill, the chrome rim and the VC pink glow stay as they are.
"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'research', 'title.png')
DOCS = os.path.join(ROOT, 'docs')
SA_BOX = (440, 18, 1060, 306)
VC_BOX = (1100, 25, 1475, 300)


def no_shadow(im):
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if 0 < a < 235 and max(r, g, b) < 90:      # soft dark drop shadow -> gone
                px[x, y] = (r, g, b, 0)
    return im


def fit(im, w, h):
    s = min(w / im.width, h / im.height)
    return im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)


def tight(im):
    bb = im.getchannel('A').point(lambda v: 255 if v > 8 else 0).getbbox()
    return im.crop(bb) if bb else im


def build(size=1024):
    t = Image.open(SRC).convert('RGBA')
    sa = tight(no_shadow(t.crop(SA_BOX)))
    vc = tight(t.crop(VC_BOX))
    c = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    pad = int(size * 0.03)
    sa = fit(sa, size - 2 * pad, int(size * 0.47))
    vc = fit(vc, int(size * 0.72), int(size * 0.47))
    gap = int(size * 0.02)
    top = (size - (sa.height + gap + vc.height)) // 2
    c.alpha_composite(sa, ((size - sa.width) // 2, top))
    c.alpha_composite(vc, ((size - vc.width) // 2, top + sa.height + gap))
    return c


def main():
    big = build()
    big.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, 'research', 'icon-sa-vc.png'))
    for name, s in (('icon-512.png', 512), ('icon-192.png', 192), ('favicon-48.png', 48), ('favicon-32.png', 32)):
        big.resize((s, s), Image.LANCZOS).save(os.path.join(DOCS, name), optimize=True)
    # iOS draws transparent touch icons on black, so the home-screen icon keeps the art on a dark tile
    tile = Image.new('RGBA', (180, 180), (17, 17, 17, 255))
    tile.alpha_composite(big.resize((164, 164), Image.LANCZOS), (8, 8))
    tile.convert('RGB').save(os.path.join(DOCS, 'apple-touch-icon.png'), optimize=True)
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(DOCS, 'favicon.ico'),
                                                sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print('icons written (transparent background)')


if __name__ == '__main__':
    main()
