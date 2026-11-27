#!/usr/bin/env python3
# 5.02.77: the letters of every MP language for the neon title menu (it draws out of PNG glyph atlases, and
# assets/MP/neon/font_*.png only have ASCII). One atlas per language: ASCII + every other letter its table uses
# (and the CAPITAL form of each, the menu draws many words in capitals) -> assets/MP/neon/intl/<code>.png + .txt,
# the same format as font_*.txt (G code x y w h xoff yoff advance, code = the Unicode code point).
# libMP has them built in (src/embed/neonintl_blob.S) and loads the one of the language you pick.
import os, json
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
here = os.path.join(root, 'tools', 'i18n')
out = os.path.join(root, 'assets', 'MP', 'neon', 'intl')
NOTO = '/usr/share/fonts/truetype/noto/'
CJK = '/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc'
LATIN = NOTO + 'NotoSans-Bold.ttf'
FALLBACK = ['/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf']
FONTS = {'zh-CN': (CJK, 2), 'zh-TW': (CJK, 3), 'ja': (CJK, 0), 'ko': (CJK, 1),
         'th': (NOTO + 'NotoSansThai-Bold.ttf', 0), 'ar': (NOTO + 'NotoSansArabic-Bold.ttf', 0)}
PAD = 2

_cmaps = {}
def has(path, idx, cp):
    k = (path, idx)
    if k not in _cmaps:
        _cmaps[k] = set(TTFont(path, fontNumber=idx).getBestCmap().keys()) if path.endswith('.ttc') else set(TTFont(path).getBestCmap().keys())
    return cp in _cmaps[k]

def build(code, chars):
    path, idx = FONTS.get(code, (LATIN, 0))
    big = code in ('zh-CN', 'zh-TW', 'ja', 'ko')
    size = 34 if big else 40
    chain = [(path, idx)] + ([(LATIN, 0)] if path != LATIN else []) + [(p, 0) for p in FALLBACK]
    fonts = {k: ImageFont.truetype(k[0], size, index=k[1]) for k in chain}
    main = fonts[chain[0]]
    cps = sorted(set(range(32, 127)) | {ord(c) for c in chars if ord(c) >= 160})
    glyphs = []
    for cp in cps:
        k = next((k for k in chain if has(k[0], k[1], cp)), None)
        if not k: continue
        f = fonts[k]; ch = chr(cp)
        x0, y0, x1, y1 = f.getbbox(ch, anchor='ls')
        glyphs.append((cp, ch, f, x0, y0, x1, y1, f.getlength(ch)))
    aw = 2048 if big else 1024
    x = y = rh = 0; placed = []
    for g in glyphs:
        w, h = max(0, g[5] - g[3]), max(0, g[6] - g[4])
        if x + w + PAD * 2 > aw: x, y, rh = 0, y + rh, 0
        placed.append((g, x, y, w, h)); x += w + PAD * 2; rh = max(rh, h + PAD * 2)
    ah = 1
    while ah < y + rh: ah *= 2
    img = Image.new('L', (aw, ah), 0); d = ImageDraw.Draw(img); lines = []
    for (cp, ch, f, x0, y0, x1, y1, adv), px, py, w, h in placed:
        if w and h: d.text((px + PAD - x0, py + PAD - y0), ch, font=f, fill=255, anchor='ls')
        lines.append('G %d %d %d %d %d %d %d %.3f' % (cp, px + PAD, py + PAD, w, h, x0, y0, adv))
    rgba = Image.new('LA', img.size, 255); rgba.putalpha(img)
    rgba.convert('RGBA').save(os.path.join(out, code + '.png'), optimize=True)
    cap = main.getbbox('H', anchor='ls'); asc, desc = main.getmetrics()
    # the Latin ascent, so a line sits where the ASCII fonts put it
    with open(os.path.join(out, code + '.txt'), 'w', encoding='utf-8', newline='\n') as fo:
        fo.write('FONT %d %d %d %d %d %d\n' % (size, asc, desc, cap[3] - cap[1], aw, ah))
        fo.write('\n'.join(lines) + '\n')
    return len(glyphs), (aw, ah), os.path.getsize(os.path.join(out, code + '.png'))

if __name__ == '__main__':
    os.makedirs(out, exist_ok=True)
    for L in json.load(open(os.path.join(here, 'languages.json'), encoding='utf-8')):
        chars = set()   # what libMP reads: the .lng (Arabic there is already shaped, in visual order)
        for ln in open(os.path.join(root, 'assets', 'MP', 'lang', L['code'] + '.lng'), encoding='utf-8'):
            if ln.startswith('#name='): chars |= set(ln[6:].strip())
            elif '\t' in ln and not ln.startswith('#'):
                v = ln.rstrip('\n').split('\t', 1)[1]; chars |= set(v) | set(v.upper())
        print(L['code'], *build(L['code'], chars))
