#!/usr/bin/env python3
"""4.13.58: host test of assets/MP/moonloader/mods_loader.lua (the real file) in Lua via lupa.
MonetLoader is mocked: script.list/load/unload, lfs, ffi (MP_LuaCheatsAllowed), wait, sampAddChatMessage.
Scenario: offline (allowed) -> custom server, cheats ON -> host switches them OFF -> ON again -> featured server.
Checks: HG starts, stops, starts again; normal mods start once; the old root copy is stopped and renamed."""
import os, sys, shutil, tempfile
from lupa import LuaRuntime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets/MP/moonloader/mods_loader.lua")
tmp = tempfile.mkdtemp()
os.makedirs(tmp + "/mods")
open(tmp + "/mods/hitmarker.lua", "w").write("script_name('Hitmarker')\n")
open(tmp + "/mods/tvplayer.lua", "w").write("script_name('TV')\n")
open(tmp + "/mods/hg.lua", "w").write("-- MP_CHEAT_MOD\nscript_name('x')\n")
open(tmp + "/hitmarker.lua", "w").write("old copy\n")   # old copy in moonloader/

lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
ROOT = ...
''')
G = lua.globals()
G.ROOT = tmp
lua.execute(r'''
running = {}   -- path -> script object
loads, unloads, chat, clock = {}, {}, {}, 1000
allowed = 1
local function mk(path, name)
  local s = { path = path, name = name or path, dead = false }
  function s:unload() self.dead = true; running[self.path] = nil; unloads[#unloads + 1] = self.path end
  return s
end
running[ROOT .. '/hitmarker.lua'] = mk(ROOT .. '/hitmarker.lua', 'Hitmarker')   -- the old copy runs
script = {
  list = function() local t = {} for _, s in pairs(running) do t[#t + 1] = s end return t end,
  load = function(p) loads[#loads + 1] = p; running[p] = mk(p) end,
}
self = mk(ROOT .. '/mods_loader.lua', 'MP Mods Loader'); running[self.path] = self
function thisScript() return self end
function script_name() end function script_author() end function script_version() end function script_description() end
function getWorkingDirectory() return ROOT end
function sampAddChatMessage(t) chat[#chat + 1] = t end
_print = print; function print() end
os.time = function() return math.floor(clock / 1000) end
package.preload['lfs'] = function()
  return {
    dir = function(d) local list = { 'hg.lua', 'hitmarker.lua', 'tvplayer.lua', 'tvplayer' } local i = 0 return function() i = i + 1 return list[i] end end,
    attributes = function(p, k) if p:match('tvplayer$') then return 'directory' end return 'file' end,
  }
end
package.preload['ffi'] = function()
  return { cdef = function() end, abi = function() return true end,
           load = function() return setmetatable({}, { __index = function(_, k) if k == 'MP_LuaCheatsAllowed' then return function() return allowed end end end }) end }
end
''')
lua.execute(open(SRC, encoding="utf-8").read())
lua.execute(r'''
co = coroutine.create(main)
function wait(ms) clock = clock + ms; coroutine.yield() end
function step(n) for i = 1, n do local ok, e = coroutine.resume(co) if not ok then error(e) end end end
function count(t, pat) local n = 0 for _, v in ipairs(t) do if v:find(pat, 1, true) then n = n + 1 end end return n end
function hgOn() return running[ROOT .. '/mods/hg.lua'] ~= nil end
''')
fails = 0
def check(cond, what):
    global fails
    print(("PASS " if cond else "FAIL ") + what)
    if not cond: fails += 1

step = G.step; count = G.count
step(1)
check(os.path.exists(tmp + "/hitmarker.lua.old") and not os.path.exists(tmp + "/hitmarker.lua"), "old moonloader/hitmarker.lua renamed to .lua.old")
check(count(G.unloads, "/hitmarker.lua") == 1, "old root copy stopped")
check(count(G.loads, "/mods/hitmarker.lua") == 1 and count(G.loads, "/mods/tvplayer.lua") == 1, "normal mods started once")
check(G.hgOn(), "offline: HG started (cheats allowed)")
G.clock = G.clock + 20000; G.allowed = 1; step(3)
check(count(G.loads, "/mods/hg.lua") == 1, "custom server, cheats ON: HG still running, not loaded twice")
G.allowed = 0; step(2)
check(not G.hgOn(), "host switched MonetLoader cheats OFF: HG stopped")
check(count(G.chat, "Cheat mods stopped") == 1, "player told once that HG stopped")
step(5)
check(count(G.unloads, "/mods/hg.lua") == 1, "HG not stopped again while it stays off")
G.clock = G.clock + 11000; G.allowed = 1; step(2)
check(G.hgOn() and count(G.loads, "/mods/hg.lua") == 2, "cheats ON again: HG started again")
check(count(G.chat, "allowed here") == 1, "player told HG is allowed again")
G.allowed = 0; step(2)
check(not G.hgOn(), "featured / default server (MP answers 0): HG stopped")
check(count(G.loads, "/mods/tvplayer.lua") == 1, "normal mods never restarted")
shutil.rmtree(tmp)
print("%d failure(s)" % fails)
sys.exit(1 if fails else 0)
