#!/usr/bin/env python3
"""Runs the real assets/MP/moonloader/mods/hitmarker.lua (MonetLoader mod) on a PC through lupa.

MonetLoader's render API is mocked: every renderDrawLine / renderDrawTextureTriangles / renderFontDrawText call
of one frame is recorded, then painted with PIL (textured triangles are sampled from the mod's own hitmarker.png
and hitmarker_head.png). Hits come in through a mocked libMP `MP_PopHit`, exactly like in the game.

    frames(events, seconds, fps, bg) -> list of PIL images (W x H), used by tools/preview/make_gifs.py
"""
import os, math
from lupa import LuaRuntime
from PIL import Image, ImageDraw, ImageFont
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MOD = os.path.join(ROOT, 'assets/MP/moonloader/mods')
SW, SH = 1600, 720
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'


class Recorder:
    def __init__(self):
        self.calls = []
        self.tex = {}
        self.fonts = {}

    def load_tex(self, path):
        if not os.path.exists(path):
            return None
        h = len(self.tex) + 1
        self.tex[h] = np.asarray(Image.open(path).convert('RGBA'), dtype=np.float32) / 255.0
        return h


def _argb(c):
    c = int(c)
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255, (c >> 24) & 255)


def paint(bg, calls, rec):
    """bg: RGB PIL image SW x SH. Returns a new image with the recorded calls drawn on it."""
    im = np.asarray(bg.convert('RGB'), dtype=np.float32) / 255.0
    im = im.copy()
    over = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    for kind, *a in calls:
        if kind == 'tri':
            tex, v, col = a
            T = rec.tex.get(tex)
            if T is None:
                continue
            r, g, b, al = [x / 255.0 for x in _argb(col)]
            th, tw = T.shape[:2]
            for i in range(0, len(v) - 11, 12):
                (x0, y0, u0, v0), (x1, y1, u1, v1), (x2, y2, u2, v2) = v[i:i + 4], v[i + 4:i + 8], v[i + 8:i + 12]
                minx, maxx = int(max(0, math.floor(min(x0, x1, x2)))), int(min(SW - 1, math.ceil(max(x0, x1, x2))))
                miny, maxy = int(max(0, math.floor(min(y0, y1, y2)))), int(min(SH - 1, math.ceil(max(y0, y1, y2))))
                area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
                if abs(area) < 1e-6 or maxx < minx or maxy < miny:
                    continue
                ys, xs = np.mgrid[miny:maxy + 1, minx:maxx + 1].astype(np.float32) + 0.5
                w0 = ((x1 - xs) * (y2 - ys) - (y1 - ys) * (x2 - xs)) / area
                w1 = ((x2 - xs) * (y0 - ys) - (y2 - ys) * (x0 - xs)) / area
                w2 = 1 - w0 - w1
                m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
                if not m.any():
                    continue
                uu = np.clip(((w0 * u0 + w1 * u1 + w2 * u2) * tw).astype(int), 0, tw - 1)
                vv = np.clip(((w0 * v0 + w1 * v1 + w2 * v2) * th).astype(int), 0, th - 1)
                t = T[vv, uu]
                src = t[..., :3] * np.array([r, g, b], np.float32)
                aa = (t[..., 3] * al * m)[..., None]
                reg = im[miny:maxy + 1, minx:maxx + 1]
                im[miny:maxy + 1, minx:maxx + 1] = reg * (1 - aa) + src * aa
    out = Image.fromarray((np.clip(im, 0, 1) * 255).astype(np.uint8)).convert('RGBA')
    d = ImageDraw.Draw(over)
    for kind, *a in calls:
        if kind == 'line':
            x0, y0, x1, y1, w, col = a
            d.line((x0, y0, x1, y1), fill=_argb(col), width=max(1, int(round(w))))
        elif kind == 'text':
            size, txt, x, y, col = a
            d.text((x, y), txt, font=ImageFont.truetype(FONT, int(size)), fill=_argb(col))
    out.alpha_composite(over)
    return out.convert('RGB')


def run(events, seconds, fps=30, bg=None, scene=None):
    """events: list of (time_s, damage, weapon, flags) - flags 1 kill, 2 instant (headshot), 4 armour, 8 armour broke.
    scene(img, t) may draw the target on every frame before the mod draws on top."""
    rec = Recorder()
    lua = LuaRuntime(unpack_returned_tuples=True)
    G = lua.globals()
    G.MODDIR = MOD
    G.SW, G.SH = SW, SH
    G.py_tex = rec.load_tex
    G.py_call = lambda *a: rec.calls.append(tuple([float(x) for x in a[2].values()] if i == 2 and a[0] == 'tri' else x for i, x in enumerate(a)))
    lua.execute(r'''
clock = 0
hits = {}
playerHandle, playerPed = 1, 1
function isPlayerPlaying() return true end
function getGameTimer() return clock end
function getScreenResolution() return SW, SH end
function getCurrentCharWeapon() return 24 end
function isPauseMenuActive() return false end
function getWorkingDirectory() return MODDIR .. '/..' end
function thisScript() return { directory = MODDIR, name = 'Hitmarker', path = MODDIR .. '/hitmarker.lua' } end
function script_name() end function script_author() end function script_version() end function script_description() end
function renderLoadTextureFromFile(p) local f = io.open(p, 'rb') if not f then return nil end f:close() return py_tex(p) end
function renderReleaseTexture() end
function doesFileExist(p) local f = io.open(p, 'rb') if f then f:close() return true end return false end
function renderCreateFont(name, size) return size end
function renderFontDrawText(f, t, x, y, col) py_call('text', f, t, x, y, col) end
function renderGetFontDrawTextLength(f, t) return #t * f * 0.62 end
function renderDrawLine(x0, y0, x1, y1, w, col) py_call('line', x0, y0, x1, y1, w, col) end
function renderDrawTextureTriangles(tex, v, col) local t = {} for i = 1, #v do t[i] = v[i] end py_call('tri', tex, t, col) end
function loadAudioStream() return nil end function setAudioStreamVolume() end function setAudioStreamState() end
function isCharShooting() return true end
function getCharCoordinates() return 0, 0, 0 end
function getAllChars() return {} end
function doesCharExist() return false end
_print = print; function print() end
local function cell(v) return { [0] = v } end
package.preload['ffi'] = function()
  return {
    cdef = function() end, abi = function() return true end,
    new = function() return { [0] = 0 } end, cast = function(_, p) return p end,
    load = function(n)
      if not tostring(n):find('MP') then error('not here') end
      return { MP_PopHit = function(a, b, c, d, e)
        local h = hits[1]
        if not h or h[1] > clock / 1000 then return 0 end
        table.remove(hits, 1)
        a[0] = 2; b[0] = 1; c[0] = h[2]; d[0] = h[3]; e[0] = h[4]
        return 1
      end }
    end,
  }
end
''')
    lua.execute(open(os.path.join(MOD, 'hitmarker.lua'), encoding='utf-8').read())
    lua.execute(r'''
co = coroutine.create(main)
function wait(ms) coroutine.yield() end
function step() local ok, e = coroutine.resume(co) if not ok then error(e) end end
function addhit(t, dmg, w, fl) hits[#hits + 1] = { t, dmg, w, fl } end
''')
    for t, dmg, w, fl in events:
        G.addhit(t, dmg, w, fl)
    frames = []
    G.clock = 0
    G.step()  # loads textures, enters the loop
    n = int(seconds * fps)
    for i in range(n):
        G.clock = int(i * 1000 / fps) + 1
        rec.calls.clear()
        G.step()
        base = bg.copy() if bg is not None else Image.new('RGB', (SW, SH), (40, 44, 52))
        if scene:
            scene(base, i / fps)
        frames.append(paint(base, list(rec.calls), rec))
    return frames


if __name__ == '__main__':
    fr = run([(0.2, 24, 24, 0), (0.6, 31, 24, 4), (1.0, 46, 24, 12), (1.5, 120, 34, 3)], 2.2)
    fr[8].save('/tmp/hm_test.png'); fr[45].save('/tmp/hm_test2.png'); print(len(fr))
