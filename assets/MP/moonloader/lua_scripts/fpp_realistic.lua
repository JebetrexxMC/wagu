-- FPP Realistic for MP (Android MonetLoader) 1.1-mp
-- 1.1-mp: switching the script on in the Script Manager now really gives first person. 1.0 shipped with
-- enabled = false, so the script loaded but the camera stayed third person until /fpp was typed.
-- A real first-person camera: the camera sits at the player's head bone, you look around by dragging on the right
-- half of the screen, the body turns with the view while walking, with head bob, an in-vehicle first person view and
-- an option to give the camera back to the game while aiming (so the game's own aiming works).
-- Inspired by the PC "FPP / Realistic real FPP" mods (https://libertycity.net/files/file239321.html); written new for
-- Android, because the PC script uses PC-only memory and keys.
--   /fpp      first person on / off
--   /fppcfg   settings window (plain imgui)

script_name('FPP Realistic')
script_author('MP port')
script_version('1.1-mp')

local imgui = require 'mimgui'
local inicfg = require 'inicfg'
local ffi = require 'ffi'

local CFG = 'fpp_realistic.ini'
local cfg = inicfg.load({
    main = {
        enabled = true,       -- 1.1: on as soon as the script is switched on (/fpp toggles)
        sensitivity = 0.25,   -- degrees per pixel
        invertY = false,
        headBob = true,
        bobAmount = 0.035,
        forward = 0.12,       -- move the eye in front of the face (m)
        up = 0.06,
        vehicle = true,
        aimRestore = true,    -- third person while aiming
        bodyFollow = true,    -- turn the body to the view when walking
    },
}, CFG)
-- 1.1: an fpp_realistic.ini from 1.0 saved enabled = false by itself: turn first person on once
if (tonumber(cfg.main.ver) or 1) < 2 then cfg.main.enabled = true; cfg.main.ver = 2 end
inicfg.save(cfg, CFG)
local function save() inicfg.save(cfg, CFG) end

-- CPed::GetBonePosition(RwV3d&, unsigned int bone, bool updateSkin)
local getBone = nil
pcall(function()
    local addr = getModuleProcAddress('libGTASA.so', '_ZN4CPed15GetBonePositionER5RwV3djb')
    if addr and addr ~= 0 then
        getBone = ffi.cast('void(*)(void*, float*, unsigned int, bool)', addr)
    end
end)
local vec = ffi.new('float[3]')
local BONE_HEAD = 8   -- ePedBones: 5 neck, 6-7 head parts, 8 head

local function headPos(ped)
    if getBone then
        local ok = pcall(function()
            getBone(ffi.cast('void*', getCharPointer(ped)), vec, BONE_HEAD, true)
        end)
        if ok and vec[0] == vec[0] and (vec[0] ~= 0 or vec[1] ~= 0) then
            local x, y, z = getCharCoordinates(ped)
            local dx, dy, dz = vec[0] - x, vec[1] - y, vec[2] - z
            if dx * dx + dy * dy + dz * dz < 4 then return vec[0], vec[1], vec[2] end
        end
    end
    local x, y, z = getCharCoordinates(ped)
    return x, y, z + 0.65
end

local yaw, pitch = 0, 0          -- degrees, yaw 0 = north (+Y)
local vehYaw = 0                 -- look offset inside a vehicle
local active = false             -- camera currently fixed by us
local bobT = 0
local lastClock = os.clock()

local function setYawFromPed()
    yaw = (-getCharHeading(PLAYER_PED)) % 360
    pitch = 0
end

local function aiming()
    local ok, t = pcall(isPlayerTargettingAnything, PLAYER_HANDLE)
    if ok and t then return true end
    local w = getCurrentCharWeapon(PLAYER_PED)
    return (w == 34 or w == 35 or w == 36 or w == 43) and isButtonPressed and isButtonPressed(0, 6)
end

local function release()
    if active then restoreCamera(); active = false end
end

-- touch drag on the right half of the screen turns the view
imgui.OnFrame(function() return cfg.main.enabled end, function(self)
    self.HideCursor = true
    local io = imgui.GetIO()
    local sw = getScreenResolution()
    if imgui.IsMouseDown(0) and io.MousePos.x > sw * 0.5 and not io.WantCaptureMouse then
        local s = tonumber(cfg.main.sensitivity) or 0.25
        local dx, dy = io.MouseDelta.x, io.MouseDelta.y
        if dx == dx and dy == dy and math.abs(dx) < 300 and math.abs(dy) < 300 then
            if isCharInAnyCar(PLAYER_PED) then
                vehYaw = math.max(-150, math.min(150, vehYaw + dx * s))
            else
                yaw = (yaw + dx * s) % 360
            end
            pitch = pitch + (cfg.main.invertY and dy or -dy) * s
            pitch = math.max(-80, math.min(80, pitch))
        end
    end
end)

local function update()
    local now = os.clock()
    local dt = math.min(0.1, now - lastClock)
    lastClock = now
    if not cfg.main.enabled or not doesCharExist(PLAYER_PED) or isCharDead(PLAYER_PED)
        or (isPauseMenuActive and isPauseMenuActive()) then
        release(); return
    end
    local inCar = isCharInAnyCar(PLAYER_PED)
    if inCar and not cfg.main.vehicle then release(); return end
    if not inCar and cfg.main.aimRestore and aiming() then
        if active then release() end
        setYawFromPed()
        return
    end

    local hx, hy, hz = headPos(PLAYER_PED)
    local ly
    if inCar then
        local car = storeCarCharIsInNoSave(PLAYER_PED)
        ly = ((-getCarHeading(car)) % 360 + vehYaw) % 360
        if not imgui.IsMouseDown(0) then vehYaw = vehYaw * math.max(0, 1 - dt * 2.5) end
    else
        ly = yaw
        local moving = getCharSpeed(PLAYER_PED) > 0.5
        if cfg.main.bodyFollow and moving then setCharHeading(PLAYER_PED, (-yaw) % 360) end
        if cfg.main.headBob and moving then
            bobT = bobT + dt * (getCharSpeed(PLAYER_PED) > 4 and 11 or 7)
            hz = hz + math.sin(bobT) * (tonumber(cfg.main.bobAmount) or 0.035)
        end
    end
    local r, p = math.rad(ly), math.rad(pitch)
    local fx, fy, fz = math.sin(r) * math.cos(p), math.cos(r) * math.cos(p), math.sin(p)
    local fw, up = tonumber(cfg.main.forward) or 0.12, tonumber(cfg.main.up) or 0.06
    local cx, cy, cz = hx + math.sin(r) * fw, hy + math.cos(r) * fw, hz + up
    setFixedCameraPosition(cx, cy, cz, 0.0, 0.0, 0.0)
    pointCameraAtPoint(cx + fx * 10, cy + fy * 10, cz + fz * 10, 2)
    active = true
end

-- settings window
local win = imgui.new.bool(false)
local V = {}
imgui.OnFrame(function() return win[0] end, function(self)
    self.HideCursor = false
    local sw, sh = getScreenResolution()
    imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.FirstUseEver, imgui.ImVec2(0.5, 0.5))
    imgui.SetNextWindowSize(imgui.ImVec2(460, 0), imgui.Cond.FirstUseEver)
    if imgui.Begin('FPP Realistic', win) then
        local ch = false
        for _, k in ipairs({ { 'enabled', 'First person' }, { 'headBob', 'Head bob' }, { 'vehicle', 'First person in vehicles' },
                { 'aimRestore', 'Third person while aiming' }, { 'bodyFollow', 'Body turns with the view' }, { 'invertY', 'Invert up/down' } }) do
            V[k[1]] = V[k[1]] or imgui.new.bool(false)
            V[k[1]][0] = cfg.main[k[1]] and true or false
            if imgui.Checkbox(k[2], V[k[1]]) then
                cfg.main[k[1]] = V[k[1]][0]; ch = true
                if k[1] == 'enabled' and V[k[1]][0] then setYawFromPed() end
            end
        end
        for _, d in ipairs({ { 'sensitivity', 'Look speed', 0.05, 1.0 }, { 'bobAmount', 'Bob amount', 0.0, 0.1 },
                { 'forward', 'Eye forward (m)', -0.1, 0.4 }, { 'up', 'Eye up (m)', -0.2, 0.3 } }) do
            V[d[1]] = V[d[1]] or imgui.new.float(0)
            V[d[1]][0] = tonumber(cfg.main[d[1]]) or d[3]
            if imgui.SliderFloat(d[2], V[d[1]], d[3], d[4], '%.3f') then cfg.main[d[1]] = V[d[1]][0]; ch = true end
        end
        imgui.TextDisabled(getBone and 'Head bone: CPed::GetBonePosition' or 'Head bone not found: using body position')
        imgui.TextDisabled('Drag on the right half of the screen to look around.')
        if ch then save() end
    end
    imgui.End()
end)

function main()
    while not isPlayerPlaying(PLAYER_HANDLE) do wait(200) end
    setYawFromPed()
    sampRegisterChatCommand('fpp', function()
        cfg.main.enabled = not cfg.main.enabled
        save()
        if cfg.main.enabled then setYawFromPed() else release() end
        printStringNow(cfg.main.enabled and 'First person ON' or 'First person OFF', 1200)
    end)
    sampRegisterChatCommand('fppcfg', function() win[0] = not win[0] end)
    if cfg.main.enabled then
        printStringNow('First person ON - /fpp off, /fppcfg settings', 2500)
        print('[fpp] first person on (/fpp toggles, /fppcfg settings)' .. (getBone and '' or ' - head bone not found, using the body position'))
    end
    while true do
        wait(0)
        local ok, err = pcall(update)
        if not ok then print('[fpp] ' .. tostring(err)); release(); wait(500) end
    end
end

function onScriptTerminate(s)
    if s == thisScript() and active then pcall(restoreCamera) end
end
