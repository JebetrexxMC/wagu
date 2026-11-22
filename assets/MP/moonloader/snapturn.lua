-- MP Snap Turn (MP 5.02.46) - MonetLoader only
-- Auto aim snap turn: while you aim a gun (normal aim or a scope), the camera turns smoothly (ease in / ease out) to
-- the nearest player or ped in front of you. "Auto" does it once each time you start aiming; the AIM button on the
-- screen does it again whenever you press it. /snapturn opens the settings (plain mimgui window).
-- 5.02.46: the 180 / 360 buttons are gone. On a server it works only where the server allows MonetLoader cheats
-- (like the cheat scripts in lua_scripts/); offline (single player) it always works.
script_name('MP Snap Turn')
script_author('JebetrexxMC')
script_version('2.0-mp')
script_description('/snapturn - auto aim snap turn: the camera turns to the nearest target while aiming')

local imgui = require 'mimgui'
local new = imgui.new

local CFG = getWorkingDirectory() .. '/config/snapturn.txt'
local cfg = { on = true, auto = true, scopeOnly = false, players = true, peds = true, ms = 220, range = 60, fov = 70,
  bx = 0.80, by = 0.42, size = 100 }
local KEYS = { 'on', 'auto', 'scopeOnly', 'players', 'peds', 'ms', 'range', 'fov', 'bx', 'by', 'size' }
local function loadCfg()
  local f = io.open(CFG, 'r'); if not f then return end
  for l in f:lines() do
    local k, v = l:match('^(%w+)=(.*)$')
    if k and cfg[k] ~= nil then
      if type(cfg[k]) == 'boolean' then cfg[k] = v == '1' else cfg[k] = tonumber(v) or cfg[k] end
    end
  end
  f:close()
end
local function saveCfg()
  local f = io.open(CFG, 'w'); if not f then return end
  f:write('# MP Snap Turn (/snapturn)\n')
  for _, k in ipairs(KEYS) do
    local v = cfg[k]; if type(v) == 'boolean' then v = v and 1 or 0 end
    f:write(k .. '=' .. tostring(v) .. '\n')
  end
  f:close()
end

local showCfg = new.bool(false)
local bOn, bAuto, bScope, bPl, bPed = new.bool(true), new.bool(true), new.bool(false), new.bool(true), new.bool(true)
local iMs, iRange, iFov, iSize = new.int(220), new.int(60), new.int(70), new.int(100)
local fBx, fBy = new.float(0.8), new.float(0.42)

-- the server decides (MP_LuaCheatsAllowed, like lua_scripts_loader.lua); single player always may
local cheatsAllowed
do
  local okf, ffi = pcall(require, 'ffi')
  if okf and ffi then
    pcall(ffi.cdef, 'int MP_LuaCheatsAllowed(void);')
    local so = ffi.abi('64bit') and 'libMP64.so' or 'libMP32.so'
    local names = { so:sub(4, -4), so }
    local maps = io.open('/proc/self/maps', 'r')
    if maps then
      for l in maps:lines() do local p = l:match('(/%S*/' .. so:gsub('%.', '%%.') .. ')$') if p then names[#names + 1] = p break end end
      maps:close()
    end
    for _, n in ipairs(names) do
      local okl, lib = pcall(ffi.load, n)
      if okl and lib and pcall(function() return lib.MP_LuaCheatsAllowed end) then
        cheatsAllowed = function() return lib.MP_LuaCheatsAllowed() ~= 0 end
        break
      end
    end
  end
end
local function online()
  if not isSampAvailable then return false end
  local ok, v = pcall(isSampAvailable)
  if not (ok and v) then return false end
  local ok2, st = pcall(sampGetGamestate)
  return not ok2 or st == 3
end
local function allowed()
  if not online() then return true end
  return cheatsAllowed ~= nil and cheatsAllowed()
end

-- what the camera looks at now: heading (radians, world) and pitch
local function camDir()
  local cx, cy, cz = getActiveCameraCoordinates()
  local px, py, pz = getActiveCameraPointAt()
  local dx, dy, dz = px - cx, py - cy, pz - cz
  local h = math.sqrt(dx * dx + dy * dy)
  return math.atan2(dy, dx), math.atan2(dz, h), cx, cy, cz
end
local function wrap(a) while a > math.pi do a = a - 2 * math.pi end while a < -math.pi do a = a + 2 * math.pi end return a end

-- guns that aim (slots 2..7 + the camera 43); scope = sniper 34, country rifle 33 (first person), rocket 35/36, camera 43
local SCOPE = { [33] = true, [34] = true, [35] = true, [36] = true, [43] = true }
local function aimState()
  if not doesCharExist(PLAYER_PED) or isCharInAnyCar(PLAYER_PED) or isCharDead(PLAYER_PED) then return false end
  local w = getCurrentCharWeapon(PLAYER_PED)
  local slot = getWeapontypeSlot(w)
  if not ((slot >= 2 and slot <= 7) or w == 43) then return false end
  local _, _, cx, cy, cz = camDir()
  local x, y, z = getCharCoordinates(PLAYER_PED)
  local d = math.sqrt((cx - x) ^ 2 + (cy - y) ^ 2 + (cz - z) ^ 2)
  -- the normal on-foot camera is ~3.5-5 m behind you; aiming pulls it to the shoulder (~1-2.5 m), a scope into your head
  local scoped = SCOPE[w] and d < 0.9
  local aiming = scoped or d < 2.7 or isPlayerTargettingAnything(PLAYER_HANDLE)
  if cfg.scopeOnly then return scoped, true end
  return aiming, scoped
end

-- the nearest target in front of the camera: the smallest turn wins, then the shortest distance
local function isPlayerPed(h)
  if not (sampGetPlayerIdByCharHandle and online()) then return false end
  local ok, has = pcall(sampGetPlayerIdByCharHandle, h)
  return ok and has == true
end
local function findTarget()
  if not getAllChars then return nil end
  local h0, _, cx, cy, cz = camDir()
  local px, py, pz = getCharCoordinates(PLAYER_PED)
  local best, bestScore
  local ok, list = pcall(getAllChars)
  if not ok or type(list) ~= 'table' then return nil end
  for _, h in ipairs(list) do
    if h ~= PLAYER_PED and doesCharExist(h) and not isCharDead(h) then
      local pl = isPlayerPed(h)
      if (pl and cfg.players) or (not pl and cfg.peds) then
        local x, y, z = getCharCoordinates(h)
        local dist = math.sqrt((x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2)
        if dist > 1.0 and dist <= cfg.range then
          z = z + 0.45   -- the chest, not the feet
          local dx, dy, dz = x - cx, y - cy, z - cz
          local hd = math.atan2(dy, dx)
          local turn = math.abs(wrap(hd - h0))
          if turn <= math.rad(cfg.fov) then
            local score = turn * 20 + dist
            if not bestScore or score < bestScore then
              bestScore = score
              best = { h = hd, p = math.atan2(dz, math.sqrt(dx * dx + dy * dy)), ch = h }
            end
          end
        end
      end
    end
  end
  return best
end

-- the turn: setCameraPositionUnfixed(pitch, heading) - the sign / offset of MonetLoader's angles is measured on the
-- first frame, so the turn stays smooth whatever the game uses (the camera "beta" is usually heading + pi)
local turning = false
local offH, sgnP = math.pi, 1
local lastMsg = 0
local function tell(t)
  if os.clock() - lastMsg < 3 then return end
  lastMsg = os.clock()
  if sampAddChatMessage and online() then pcall(sampAddChatMessage, '{9AD0FF}[Snap Turn]{FFFFFF} ' .. t, -1) end
end
local function snapToTarget(quiet)
  if turning then return end
  if not allowed() then if not quiet then tell('auto aim is off on this server (it does not allow MonetLoader cheats)') end return end
  local tg = findTarget()
  if not tg then if not quiet then tell('no target in front of you') end return end
  turning = true
  lua_thread.create(function()
    local h0, p0 = camDir()
    setCameraPositionUnfixed(p0 * sgnP, h0 + offH); wait(0)
    local h1, p1 = camDir()
    local e = wrap(h1 - h0)
    if math.abs(e) > 0.05 then offH = wrap(offH - e) end              -- wrong offset: learn it
    if math.abs(p1 - p0) > 0.05 and math.abs(p1 + p0) < math.abs(p1 - p0) then sgnP = -sgnP end
    local h2, p2 = camDir()
    local dH, dP = wrap(tg.h - h2), tg.p - p2
    local t0 = os.clock()
    while true do
      local k = math.min(1, (os.clock() - t0) * 1000 / math.max(60, cfg.ms))
      local ease = k < 0.5 and 2 * k * k or 1 - ((-2 * k + 2) ^ 2) / 2   -- ease in / out
      setCameraPositionUnfixed((p2 + dP * ease) * sgnP, h2 + offH + dH * ease)
      if k >= 1 then break end
      wait(0)
    end
    turning = false
  end)
end

local aimNow = false
imgui.OnInitialize(function() imgui.GetIO().IniFilename = nil end)

-- the AIM button, only while aiming
imgui.OnFrame(function() return cfg.on and (aimNow or turning) and not showCfg[0] and not isPauseMenuActive() end, function(self)
  self.HideCursor = true
  local sw, sh = getScreenResolution()
  local s = math.max(1.0, sh / 720) * cfg.size / 100
  local bw, bh = 96 * s, 64 * s
  imgui.SetNextWindowPos(imgui.ImVec2(sw * cfg.bx, sh * cfg.by), imgui.Cond.Always, imgui.ImVec2(0.5, 0.5))
  imgui.SetNextWindowBgAlpha(0.0)
  local fl = imgui.WindowFlags.NoTitleBar + imgui.WindowFlags.NoResize + imgui.WindowFlags.NoMove + imgui.WindowFlags.NoScrollbar +
             imgui.WindowFlags.AlwaysAutoResize + imgui.WindowFlags.NoSavedSettings + imgui.WindowFlags.NoFocusOnAppearing
  if imgui.Begin('##mpsnap', nil, fl) then
    imgui.SetWindowFontScale(s)
    imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.05, 0.05, 0.08, 0.55))
    imgui.PushStyleColor(imgui.Col.ButtonActive, imgui.ImVec4(0.86, 0.15, 0.15, 0.85))
    if imgui.Button('AIM##s', imgui.ImVec2(bw, bh)) then snapToTarget(false) end
    imgui.PopStyleColor(2)
  end
  imgui.End()
end)

imgui.OnFrame(function() return showCfg[0] end, function()
  local sw, sh = getScreenResolution()
  local s = math.max(1.0, sh / 720)
  imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.Appearing, imgui.ImVec2(0.5, 0.5))
  imgui.SetNextWindowSize(imgui.ImVec2(math.min(sw * 0.9, 640 * s), math.min(sh * 0.9, 600 * s)), imgui.Cond.Appearing)
  if imgui.Begin('Snap Turn##mpsnapcfg', showCfg, imgui.WindowFlags.NoCollapse) then
    imgui.SetWindowFontScale(s)
    local ch = false
    if imgui.Checkbox('Snap Turn on (AIM button while aiming)', bOn) then cfg.on = bOn[0]; ch = true end
    if imgui.Checkbox('Auto: turn to the nearest target when you start aiming', bAuto) then cfg.auto = bAuto[0]; ch = true end
    if imgui.Checkbox('Only when aiming with a scope (sniper / rifle / rocket / camera)', bScope) then cfg.scopeOnly = bScope[0]; ch = true end
    if imgui.Checkbox('Target players', bPl) then cfg.players = bPl[0]; ch = true end
    if imgui.Checkbox('Target peds (not players)', bPed) then cfg.peds = bPed[0]; ch = true end
    if imgui.SliderInt('Turn time (ms)', iMs, 60, 1000) then cfg.ms = iMs[0]; ch = true end
    if imgui.SliderInt('Range (m)', iRange, 5, 200) then cfg.range = iRange[0]; ch = true end
    if imgui.SliderInt('Angle (degrees each side)', iFov, 10, 180) then cfg.fov = iFov[0]; ch = true end
    if imgui.SliderInt('Button size (%)', iSize, 60, 200) then cfg.size = iSize[0]; ch = true end
    if imgui.SliderFloat('Button left / right', fBx, 0.05, 0.95, '%.2f') then cfg.bx = fBx[0]; ch = true end
    if imgui.SliderFloat('Button up / down', fBy, 0.05, 0.95, '%.2f') then cfg.by = fBy[0]; ch = true end
    if ch then saveCfg() end
    imgui.Separator()
    imgui.TextWrapped(allowed() and 'Auto aim works here.' or 'This server does not allow MonetLoader cheats: auto aim is off here.')
    if imgui.Button('Test (turn to the nearest target)', imgui.ImVec2(-1, 48 * s)) then snapToTarget(false) end
    if imgui.Button('Close', imgui.ImVec2(-1, 48 * s)) then showCfg[0] = false end
  end
  imgui.End()
end)

function main()
  loadCfg()
  bOn[0], bAuto[0], bScope[0], bPl[0], bPed[0] = cfg.on, cfg.auto, cfg.scopeOnly, cfg.players, cfg.peds
  iMs[0], iRange[0], iFov[0], iSize[0], fBx[0], fBy[0] = cfg.ms, cfg.range, cfg.fov, cfg.size, cfg.bx, cfg.by
  if isSampLoaded then
    lua_thread.create(function()
      while not isSampAvailable() do wait(100) end
      sampRegisterChatCommand('snapturn', function(a)
        a = (a or ''):lower()
        if a == 'aim' then snapToTarget(false) else showCfg[0] = not showCfg[0] end
      end)
    end)
  end
  local seen, was = 0, false
  while true do
    wait(0)
    local a = cfg.on and aimState() or false
    -- Auto: once each time aiming starts
    if a and not was and cfg.auto then snapToTarget(true) end
    was = a and true or false
    -- a short hold so the button doesn't flicker when the camera moves for a frame
    if a then seen = os.clock() end
    aimNow = a or (os.clock() - seen < 0.35)
  end
end
