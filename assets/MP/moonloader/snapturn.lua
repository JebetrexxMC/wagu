-- MP Snap Turn (MP 5.02.36) - MonetLoader only
-- While you aim a gun (normal aim or a sniper / rifle / camera scope), two small buttons show on the screen:
-- 180 and 360. Press one and the camera turns that far, smoothly (ease in / ease out), so you can check behind you
-- or spin around and keep aiming. /snapturn opens the settings (plain mimgui window, not an MP window).
script_name('MP Snap Turn')
script_author('JebetrexxMC')
script_version('1.0-mp')
script_description('/snapturn - smooth 180 / 360 camera snap turn while aiming')

local imgui = require 'mimgui'
local new = imgui.new

local CFG = getWorkingDirectory() .. '/config/snapturn.txt'
local cfg = { on = true, scopeOnly = false, ms180 = 260, ms360 = 480, left = false, bx = 0.80, by = 0.42, size = 100 }
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
  for _, k in ipairs({ 'on', 'scopeOnly', 'ms180', 'ms360', 'left', 'bx', 'by', 'size' }) do
    local v = cfg[k]; if type(v) == 'boolean' then v = v and 1 or 0 end
    f:write(k .. '=' .. tostring(v) .. '\n')
  end
  f:close()
end

local showCfg = new.bool(false)
local bOn, bScope, bLeft = new.bool(true), new.bool(false), new.bool(false)
local i180, i360, iSize = new.int(260), new.int(480), new.int(100)
local fBx, fBy = new.float(0.8), new.float(0.42)

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

-- the turn: setCameraPositionUnfixed(pitch, heading) - the sign / offset of MonetLoader's angles is measured on the
-- first frame, so the turn stays smooth whatever the game uses (the camera "beta" is usually heading + pi)
local turning = false
local offH, sgnP = math.pi, 1
local function snap(deg)
  if turning then return end
  turning = true
  lua_thread.create(function()
    local h0, p0 = camDir()
    setCameraPositionUnfixed(p0 * sgnP, h0 + offH); wait(0)
    local h1, p1 = camDir()
    local e = wrap(h1 - h0)
    if math.abs(e) > 0.05 then offH = wrap(offH - e) end              -- wrong offset: learn it
    if math.abs(p1 - p0) > 0.05 and math.abs(p1 + p0) < math.abs(p1 - p0) then sgnP = -sgnP end
    local total = math.rad(deg) * (cfg.left and 1 or -1)
    local dur = deg >= 360 and cfg.ms360 or cfg.ms180
    local t0 = os.clock()
    while true do
      local k = math.min(1, (os.clock() - t0) * 1000 / math.max(60, dur))
      local ease = k < 0.5 and 2 * k * k or 1 - ((-2 * k + 2) ^ 2) / 2   -- ease in / out
      setCameraPositionUnfixed(p0 * sgnP, h0 + offH + total * ease)
      if k >= 1 then break end
      wait(0)
    end
    turning = false
  end)
end

local aimNow = false
imgui.OnInitialize(function() imgui.GetIO().IniFilename = nil end)

-- the two buttons, only while aiming
local btn = imgui.OnFrame(function() return cfg.on and (aimNow or turning) and not showCfg[0] and not isPauseMenuActive() end, function(self)
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
    if imgui.Button('180##s', imgui.ImVec2(bw, bh)) then snap(180) end
    if imgui.Button('360##s', imgui.ImVec2(bw, bh)) then snap(360) end
    imgui.PopStyleColor(2)
  end
  imgui.End()
end)

imgui.OnFrame(function() return showCfg[0] end, function()
  local sw, sh = getScreenResolution()
  local s = math.max(1.0, sh / 720)
  imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.Appearing, imgui.ImVec2(0.5, 0.5))
  imgui.SetNextWindowSize(imgui.ImVec2(math.min(sw * 0.9, 640 * s), math.min(sh * 0.9, 560 * s)), imgui.Cond.Appearing)
  if imgui.Begin('Snap Turn##mpsnapcfg', showCfg, imgui.WindowFlags.NoCollapse) then
    imgui.SetWindowFontScale(s)
    local ch = false
    if imgui.Checkbox('Snap Turn on (buttons while aiming)', bOn) then cfg.on = bOn[0]; ch = true end
    if imgui.Checkbox('Only when aiming with a scope (sniper / rifle / rocket / camera)', bScope) then cfg.scopeOnly = bScope[0]; ch = true end
    if imgui.Checkbox('Turn to the left (off = to the right)', bLeft) then cfg.left = bLeft[0]; ch = true end
    if imgui.SliderInt('180 turn time (ms)', i180, 80, 1000) then cfg.ms180 = i180[0]; ch = true end
    if imgui.SliderInt('360 turn time (ms)', i360, 120, 1500) then cfg.ms360 = i360[0]; ch = true end
    if imgui.SliderInt('Button size (%)', iSize, 60, 200) then cfg.size = iSize[0]; ch = true end
    if imgui.SliderFloat('Buttons left / right', fBx, 0.05, 0.95, '%.2f') then cfg.bx = fBx[0]; ch = true end
    if imgui.SliderFloat('Buttons up / down', fBy, 0.05, 0.95, '%.2f') then cfg.by = fBy[0]; ch = true end
    if ch then saveCfg() end
    imgui.Separator()
    local w = (imgui.GetContentRegionAvail().x - imgui.GetStyle().ItemSpacing.x) / 2
    if imgui.Button('Test 180', imgui.ImVec2(w, 48 * s)) then snap(180) end
    imgui.SameLine()
    if imgui.Button('Test 360', imgui.ImVec2(w, 48 * s)) then snap(360) end
    if imgui.Button('Close', imgui.ImVec2(-1, 48 * s)) then showCfg[0] = false end
  end
  imgui.End()
end)

function main()
  loadCfg()
  bOn[0], bScope[0], bLeft[0] = cfg.on, cfg.scopeOnly, cfg.left
  i180[0], i360[0], iSize[0], fBx[0], fBy[0] = cfg.ms180, cfg.ms360, cfg.size, cfg.bx, cfg.by
  if isSampLoaded then
    lua_thread.create(function()
      while not isSampAvailable() do wait(100) end
      sampRegisterChatCommand('snapturn', function(a)
        a = (a or ''):lower()
        if a == '180' then snap(180) elseif a == '360' then snap(360) else showCfg[0] = not showCfg[0] end
      end)
    end)
  end
  local seen = 0
  while true do
    wait(0)
    local a = cfg.on and aimState() or false
    -- a short hold so the buttons don't flicker when the camera moves for a frame
    if a then seen = os.clock() end
    aimNow = a or (os.clock() - seen < 0.35)
  end
end
