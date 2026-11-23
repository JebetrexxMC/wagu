-- mp_sfcompat 1.0 (MP 5.02.59): SAMPFUNCS functions PC scripts call that MonetLoader does not have.
--
-- A PC MoonLoader script died at the first call of a missing SAMPFUNCS function:
--   AbsoluteFix.lua:182: attempt to call global 'sampTextdrawDelete' (a nil value)
-- lua_scripts_loader.lua requires this module for every script it starts (before the script runs):
--  * the sampTextdraw* family: a textdraw table per script. Server textdraws that reach the script (onReceiveRpc
--    134/105/135) are recorded, sampTextdrawDelete hides one (emulated RPC 135) and forgets it, Create/Set*/Get*
--    work on the table, so a script that edits or reads its own textdraws keeps its state;
--  * any other missing samp* / sampfuncs* global: a stub that logs its name once and returns nil, so the rest of
--    the script keeps running instead of dying. Real functions are never replaced.
-- Loaded once per script state.

if rawget(_G, '__mp_sfcompat') then return _G.__mp_sfcompat end

local M = { VERSION = '1.0' }
_G.__mp_sfcompat = M

local RPC_SETSTRING, RPC_SHOW, RPC_HIDE = 105, 134, 135
local MAX_TD = 2048

local function have(name) return type(rawget(_G, name)) == 'function' end
local function def(name, f) if not have(name) then rawset(_G, name, f) end end

local function sname()
  local ok, s = pcall(function() return thisScript().name end)
  return ok and tostring(s) or '?'
end

-- ---------------------------------------------------------------- textdraws
local tds = {}
M.textdraws = tds

local function new(id)
  local t = { id = id, text = '', x = 0, y = 0, lw = 0.48, lh = 1.12, color = 0xFFFFFFFF, box = 0, boxColor = 0,
    bw = 0, bh = 0, shadow = 0, outline = 0, bg = 0xFF000000, style = 1, align = 1, prop = 1, selectable = 0,
    model = 0, rx = 0, ry = 0, rz = 0, zoom = 1, c1 = -1, c2 = -1 }
  tds[id] = t
  return t
end
local function get(id) id = tonumber(id) return id and tds[id] end

local function hide(id)
  if not (have('raknetNewBitStream') and have('raknetEmulRpcReceiveBitStream')) then return end
  pcall(function()
    local bs = raknetNewBitStream()
    raknetBitStreamWriteInt16(bs, id)
    raknetEmulRpcReceiveBitStream(RPC_HIDE, bs)
    raknetDeleteBitStream(bs)
  end)
end

def('sampTextdrawIsExists', function(id) return get(id) ~= nil end)
def('sampTextdrawCreate', function(id, text, x, y)
  id = tonumber(id) or 0
  local t = new(id)
  t.text, t.x, t.y = tostring(text or ''), tonumber(x) or 0, tonumber(y) or 0
end)
def('sampTextdrawDelete', function(id)
  id = tonumber(id)
  if not id or id < 0 or id >= MAX_TD then return end
  tds[id] = nil
  hide(id)
end)
def('sampTextdrawSetString', function(id, s) local t = get(id) if t then t.text = tostring(s or '') end end)
def('sampTextdrawGetString', function(id) local t = get(id) return t and t.text or '' end)
def('sampTextdrawSetPos', function(id, x, y) local t = get(id) if t then t.x, t.y = tonumber(x) or t.x, tonumber(y) or t.y end end)
def('sampTextdrawGetPos', function(id) local t = get(id) if t then return t.x, t.y end return 0, 0 end)
def('sampTextdrawSetLetterSizeAndColor', function(id, w, h, c) local t = get(id) if t then t.lw, t.lh, t.color = w or t.lw, h or t.lh, c or t.color end end)
def('sampTextdrawGetLetterSizeAndColor', function(id) local t = get(id) if t then return t.lw, t.lh, t.color end return 0, 0, 0 end)
def('sampTextdrawSetBoxColorAndSize', function(id, box, c, w, h) local t = get(id) if t then t.box, t.boxColor, t.bw, t.bh = box or t.box, c or t.boxColor, w or t.bw, h or t.bh end end)
def('sampTextdrawGetBoxEnabledColorAndSize', function(id) local t = get(id) if t then return t.box, t.boxColor, t.bw, t.bh end return 0, 0, 0, 0 end)
def('sampTextdrawSetAlign', function(id, a) local t = get(id) if t then t.align = a or t.align end end)
def('sampTextdrawGetAlign', function(id) local t = get(id) return t and t.align or 0 end)
def('sampTextdrawSetProportional', function(id, p) local t = get(id) if t then t.prop = p or t.prop end end)
def('sampTextdrawGetProportional', function(id) local t = get(id) return t and t.prop or 0 end)
def('sampTextdrawSetStyle', function(id, s) local t = get(id) if t then t.style = s or t.style end end)
def('sampTextdrawGetStyle', function(id) local t = get(id) return t and t.style or 0 end)
def('sampTextdrawSetShadow', function(id, s, c) local t = get(id) if t then t.shadow, t.bg = s or t.shadow, c or t.bg end end)
def('sampTextdrawGetShadowColor', function(id) local t = get(id) if t then return t.shadow, t.bg end return 0, 0 end)
def('sampTextdrawSetOutlineColor', function(id, o, c) local t = get(id) if t then t.outline, t.bg = o or t.outline, c or t.bg end end)
def('sampTextdrawGetOutlineColor', function(id) local t = get(id) if t then return t.outline, t.bg end return 0, 0 end)
def('sampTextdrawSetModelRotationZoomVehColor', function(id, m, rx, ry, rz, z, c1, c2)
  local t = get(id)
  if t then t.model, t.rx, t.ry, t.rz, t.zoom, t.c1, t.c2 = m or t.model, rx or t.rx, ry or t.ry, rz or t.rz, z or t.zoom, c1 or t.c1, c2 or t.c2 end
end)
def('sampTextdrawGetModelRotationZoomVehColor', function(id)
  local t = get(id)
  if t then return t.model, t.rx, t.ry, t.rz, t.zoom, t.c1, t.c2 end
  return 0, 0, 0, 0, 1, -1, -1
end)
def('sampTextdrawIsSelectable', function(id) local t = get(id) return t ~= nil and t.selectable ~= 0 end)
def('sampTextdrawGetLetterSize', function(id) local t = get(id) if t then return t.lw, t.lh end return 0, 0 end)

-- server textdraws that reach this script
if have('addEventHandler') then
  pcall(addEventHandler, 'onReceiveRpc', function(id, bs)
    if id ~= RPC_SHOW and id ~= RPC_HIDE and id ~= RPC_SETSTRING then return end
    pcall(function()
      local rd = raknetBitStreamGetReadOffset(bs)
      local td = raknetBitStreamReadInt16(bs)
      if id == RPC_HIDE then
        tds[td] = nil
      elseif id == RPC_SETSTRING then
        local t = tds[td] or new(td)
        local n = raknetBitStreamReadInt16(bs)
        t.text = raknetBitStreamReadString(bs, n)
      else
        local t = new(td)
        local flags = raknetBitStreamReadInt8(bs)
        t.lw, t.lh = raknetBitStreamReadFloat(bs), raknetBitStreamReadFloat(bs)
        t.color = raknetBitStreamReadInt32(bs)
        t.bw, t.bh = raknetBitStreamReadFloat(bs), raknetBitStreamReadFloat(bs)
        t.boxColor = raknetBitStreamReadInt32(bs)
        t.shadow, t.outline = raknetBitStreamReadInt8(bs), raknetBitStreamReadInt8(bs)
        t.bg = raknetBitStreamReadInt32(bs)
        t.style, t.selectable = raknetBitStreamReadInt8(bs), raknetBitStreamReadInt8(bs)
        t.x, t.y = raknetBitStreamReadFloat(bs), raknetBitStreamReadFloat(bs)
        t.model = raknetBitStreamReadInt16(bs)
        t.rx, t.ry, t.rz, t.zoom = raknetBitStreamReadFloat(bs), raknetBitStreamReadFloat(bs), raknetBitStreamReadFloat(bs), raknetBitStreamReadFloat(bs)
        t.c1, t.c2 = raknetBitStreamReadInt16(bs), raknetBitStreamReadInt16(bs)
        local n = raknetBitStreamReadInt16(bs)
        t.text = raknetBitStreamReadString(bs, n)
        t.box = (flags % 2 == 1) and 1 or 0
        t.align = (flags >= 8 and 3) or (flags >= 4 and 2) or 1
        t.prop = (math.floor(flags / 16) % 2 == 1) and 1 or 0
      end
      raknetBitStreamSetReadOffset(bs, rd)
    end)
  end)
end

-- ---------------------------------------------------------------- other missing SAMPFUNCS functions
-- A missing samp* / sampfuncs* global becomes a stub (logged once). Only these prefixes: everything else stays nil,
-- so `if someLibFunc then` checks of other libraries are not fooled.
local stubs, said = {}, {}
local mt = getmetatable(_G)
if mt == nil then mt = {} setmetatable(_G, mt) end
local prevIndex = mt.__index
mt.__index = function(g, k)
  if prevIndex then
    local v
    if type(prevIndex) == 'function' then v = prevIndex(g, k) else v = prevIndex[k] end
    if v ~= nil then return v end
  end
  if type(k) ~= 'string' then return nil end
  if not (k:match('^samp%u') or k:match('^sampfuncs%u')) then return nil end
  local f = stubs[k]
  if not f then
    f = function()
      if not said[k] then
        said[k] = true
        print(('[MP] %s: %s() is a PC SAMPFUNCS function MP does not have - skipped'):format(sname(), k))
      end
      return nil
    end
    stubs[k] = f
  end
  return f
end

return M
