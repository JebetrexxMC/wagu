-- FPP Realistic for MP (Android MonetLoader) 1.2-mp
-- 1.2-mp:
--  * Looking around works again. 1.1 read the finger through mimgui (IsMouseDown / MouseDelta), but since MP 5.02.47
--    mimgui only gets a finger while one of its windows is open, so with no window up the view never turned. The view
--    now turns with onTouch (the touches MP passes to the scripts, in getScreenResolution's space): drag anywhere on
--    the right part of the screen. The game's buttons there still work.
--  * Your own head is no longer in the picture. The eye sits in front of the face along the body's direction (not the
--    view's), so turning the view never puts the camera beside or inside the head. The view can turn up to 100
--    degrees from the body; further than that the body turns with it (also while standing).
--  * Real walking: the camera follows a smoothed head position (no animation jitter), with a step bob that goes down on
--    every step, a small side sway every two steps and a slight nod, all scaled by your speed (walk / run / sprint)
--    and eased in and out when you start or stop. The view itself is smoothed a little too.
--  * Looking down shows your chest, arms and hands, not the inside of your head.
-- 1.1-mp: switching the script on in the Script Manager gives first person at once (1.0 shipped with enabled = false).
-- Inspired by the PC "FPP / Realistic real FPP" mods (https://libertycity.net/files/file239321.html); written new for
-- Android, because the PC script uses PC-only memory and keys.
--   /fpp      first person on / off
--   /fppcfg   settings window (plain imgui)

script_name('FPP Realistic')
script_author('MP port')
script_version('1.2-mp')

local imgui = require 'mimgui'
local inicfg = require 'inicfg'
local ffi = require 'ffi'

local CFG = 'fpp_realistic.ini'
local cfg = inicfg.load({
    main = {
        enabled = true,       -- on as soon as the script is switched on (/fpp toggles)
        sensitivity = 0.22,   -- degrees per pixel (at 1280 px screen width)
        invertY = false,
        headBob = true,
        bobAmount = 0.045,    -- step bob height (m) at a run
        sway = 0.025,         -- side sway (m) at a run
        smooth = 0.5,         -- 0 = raw, 1 = very smooth camera
        forward = 0.19,       -- eye in front of the head bone, along the body (m) - keeps the head out of view
        up = 0.05,
        vehicle = true,
        aimRestore = true,    -- third person while aiming
        bodyFollow = true,    -- turn the body to the view when walking
        maxNeck = 100,        -- the view may turn this far from the body before the body turns too (degrees)
        ver = 3,
    },
}, CFG)
local ver = tonumber(cfg.main.ver) or 1
if ver < 2 then cfg.main.enabled = true end
if ver < 3 then   -- 1.2: the old eye spot (0.12 m) was still inside the head
    if (tonumber(cfg.main.forward) or 0) < 0.19 then cfg.main.forward = 0.19 end
    cfg.main.up = 0.05
    if (tonumber(cfg.main.bobAmount) or 0) <= 0.035 then cfg.main.bobAmount = 0.045 end
end
cfg.main.ver = 3
inicfg.save(cfg, CFG)
local function save() inicfg.save(cfg, CFG) end
local function num(v, d) v = tonumber(v); if v == nil or v ~= v then return d end return v end

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
    local x, y, z = getCharCoordinates(ped)
    if getBone then
        local ok = pcall(function()
            getBone(ffi.cast('void*', getCharPointer(ped)), vec, BONE_HEAD, true)
        end)
        if ok and vec[0] == vec[0] and (vec[0] ~= 0 or vec[1] ~= 0) then
            local dx, dy, dz = vec[0] - x, vec[1] - y, vec[2] - z
            if dx * dx + dy * dy + dz * dz < 4 then return vec[0], vec[1], vec[2] end
        end
    end
    return x, y, z + 0.65
end

local function wrap180(a) a = (a + 180) % 360; return a - 180 end

local yaw, pitch = 0, 0          -- the wanted view (degrees, yaw 0 = north (+Y))
local vYaw, vPitch = 0, 0        -- the smoothed view the camera uses
local vehYaw = 0                 -- look offset inside a vehicle
local active = false             -- camera currently fixed by us
local lastClock = os.clock()
local sx, sy, sz = nil, nil, nil -- smoothed head position
local stepPhase, bobMix = 0, 0   -- walking cycle and how much of it is on (eased)

local function setYawFromPed()
    yaw = (-getCharHeading(PLAYER_PED)) % 360
    vYaw = yaw
    pitch, vPitch = 0, 0
end

local function aiming()
    local ok, t = pcall(isPlayerTargettingAnything, PLAYER_HANDLE)
    if ok and t then return true end
    local w = getCurrentCharWeapon(PLAYER_PED)
    return (w == 34 or w == 35 or w == 36 or w == 43) and isButtonPressed and isButtonPressed(0, 6)
end

local function release()
    if active then restoreCamera(); active = false end
    sx = nil
end

-- ---- looking around: a finger dragged on the right part of the screen ----
local lookFinger, lastX, lastY = nil, 0, 0
local gotTouch = false           -- onTouch works here (else the old mimgui path is used)
local function look(dx, dy)
    local sw = getScreenResolution()
    local s = num(cfg.main.sensitivity, 0.22) * (1280 / math.max(640, sw or 1280))
    if dx ~= dx or dy ~= dy or math.abs(dx) > 400 or math.abs(dy) > 400 then return end
    if isCharInAnyCar(PLAYER_PED) then
        vehYaw = math.max(-150, math.min(150, vehYaw + dx * s))
    else
        yaw = (yaw + dx * s) % 360
    end
    pitch = pitch + (cfg.main.invertY and dy or -dy) * s
    pitch = math.max(-85, math.min(80, pitch))
end

function onTouch(type, id, x, y)
    gotTouch = true
    if not cfg.main.enabled or win and win[0] then return end
    local sw = getScreenResolution()
    if type == 2 then                    -- push
        if lookFinger == nil and x > sw * 0.42 then lookFinger, lastX, lastY = id, x, y end
    elseif type == 3 then                -- move
        if id == lookFinger then look(x - lastX, y - lastY); lastX, lastY = x, y end
    elseif type == 1 then                -- release
        if id == lookFinger then lookFinger = nil end
    end
end

-- fallback for a MonetLoader without onTouch from MP: mimgui's finger (only while nothing else uses it)
imgui.OnFrame(function() return cfg.main.enabled and not gotTouch end, function(self)
    self.HideCursor = true
    local io = imgui.GetIO()
    local sw = getScreenResolution()
    if imgui.IsMouseDown(0) and io.MousePos.x > sw * 0.42 and not io.WantCaptureMouse then
        look(io.MouseDelta.x, io.MouseDelta.y)
    end
end)

local function update()
    local now = os.clock()
    local dt = math.min(0.1, math.max(0.001, now - lastClock))
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

    -- the head, smoothed: the walk / run animations shake the head bone a lot
    local hx, hy, hz = headPos(PLAYER_PED)
    local sm = math.max(0, math.min(0.95, num(cfg.main.smooth, 0.5)))
    if sx == nil or inCar or (hx - sx) ^ 2 + (hy - sy) ^ 2 + (hz - sz) ^ 2 > 4 then
        sx, sy, sz = hx, hy, hz              -- first frame / teleport / vehicle: no lag
    else
        local kxy = 1 - math.exp(-dt * (60 - 45 * sm))   -- follows the body closely
        local kz = 1 - math.exp(-dt * (30 - 26 * sm))    -- the up / down shake is smoothed most
        sx, sy, sz = sx + (hx - sx) * kxy, sy + (hy - sy) * kxy, sz + (hz - sz) * kz
    end

    -- the view, smoothed a little (a finger moves in steps)
    local kv = 1 - math.exp(-dt * (40 - 25 * sm))
    vYaw = (vYaw + wrap180(yaw - vYaw) * kv) % 360
    vPitch = vPitch + (pitch - vPitch) * kv

    local ly, bodyYaw
    local bobZ, bobSide, bobPitch = 0, 0, 0
    if inCar then
        local car = storeCarCharIsInNoSave(PLAYER_PED)
        bodyYaw = (-getCarHeading(car)) % 360
        ly = (bodyYaw + vehYaw) % 360
        if lookFinger == nil and not imgui.IsMouseDown(0) then vehYaw = vehYaw * math.max(0, 1 - dt * 2.5) end
    else
        ly = vYaw
        bodyYaw = (-getCharHeading(PLAYER_PED)) % 360
        local speed = getCharSpeed(PLAYER_PED)
        local moving = speed > 0.5
        -- the body follows the view while walking, and when the view turns past the neck limit
        local rel = wrap180(ly - bodyYaw)
        local neck = math.max(30, math.min(170, num(cfg.main.maxNeck, 100)))
        if cfg.main.bodyFollow and moving then
            bodyYaw = ly
            setCharHeading(PLAYER_PED, (-ly) % 360)
        elseif math.abs(rel) > neck then
            bodyYaw = (ly - (rel > 0 and neck or -neck)) % 360
            setCharHeading(PLAYER_PED, (-bodyYaw) % 360)
        end
        -- walking cycle: one step = half a turn of stepPhase
        local want = (cfg.main.headBob and moving and not (isCharInAir and isCharInAir(PLAYER_PED))) and 1 or 0
        bobMix = bobMix + (want - bobMix) * (1 - math.exp(-dt * (want > bobMix and 4 or 6)))
        if bobMix > 0.001 then
            local stepsPerSec = math.max(1.6, math.min(3.4, 1.4 + speed * 0.28))
            stepPhase = (stepPhase + dt * stepsPerSec * math.pi) % (2 * math.pi)
            local k = math.max(0.45, math.min(1.4, speed / 4.5)) * bobMix
            local amp = num(cfg.main.bobAmount, 0.045)
            bobZ = -amp * k * (1 - math.cos(2 * stepPhase)) * 0.5     -- down on every step
            bobSide = num(cfg.main.sway, 0.025) * k * math.sin(stepPhase)   -- left / right every two steps
            bobPitch = -0.9 * k * math.sin(2 * stepPhase)                -- a slight nod with the step
        else
            stepPhase = 0
        end
    end

    -- the eye: in front of the face along the BODY (the head never gets in the picture), the look along the VIEW
    local br = math.rad(bodyYaw)
    local bfx, bfy = math.sin(br), math.cos(br)            -- body forward
    local brx, bry = math.cos(br), -math.sin(br)           -- body right
    local fw, up = num(cfg.main.forward, 0.19), num(cfg.main.up, 0.05)
    if not inCar and vPitch < -30 then fw = fw + (-vPitch - 30) / 55 * 0.08 end   -- looking down: a bit more forward (chest, not the chin)
    local cx = sx + bfx * fw + brx * bobSide
    local cy = sy + bfy * fw + bry * bobSide
    local cz = sz + up + bobZ
    local r, p = math.rad(ly), math.rad(vPitch + bobPitch)
    local fx, fy, fz = math.sin(r) * math.cos(p), math.cos(r) * math.cos(p), math.sin(p)
    setFixedCameraPosition(cx, cy, cz, 0.0, 0.0, 0.0)
    pointCameraAtPoint(cx + fx * 10, cy + fy * 10, cz + fz * 10, 2)
    active = true
end

-- settings window
win = imgui.new.bool(false)
local V = {}
imgui.OnFrame(function() return win[0] end, function(self)
    self.HideCursor = false
    local sw, sh = getScreenResolution()
    imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.FirstUseEver, imgui.ImVec2(0.5, 0.5))
    imgui.SetNextWindowSize(imgui.ImVec2(480, 0), imgui.Cond.FirstUseEver)
    if imgui.Begin('FPP Realistic', win) then
        local ch = false
        for _, k in ipairs({ { 'enabled', 'First person' }, { 'headBob', 'Walking bob' }, { 'vehicle', 'First person in vehicles' },
                { 'aimRestore', 'Third person while aiming' }, { 'bodyFollow', 'Body turns with the view when walking' }, { 'invertY', 'Invert up/down' } }) do
            V[k[1]] = V[k[1]] or imgui.new.bool(false)
            V[k[1]][0] = cfg.main[k[1]] and true or false
            if imgui.Checkbox(k[2], V[k[1]]) then
                cfg.main[k[1]] = V[k[1]][0]; ch = true
                if k[1] == 'enabled' and V[k[1]][0] then setYawFromPed() end
            end
        end
        for _, d in ipairs({ { 'sensitivity', 'Look speed', 0.05, 1.0 }, { 'smooth', 'Smoothness', 0.0, 0.95 },
                { 'bobAmount', 'Step bob (m)', 0.0, 0.1 }, { 'sway', 'Side sway (m)', 0.0, 0.08 },
                { 'forward', 'Eye forward (m)', 0.1, 0.4 }, { 'up', 'Eye up (m)', -0.2, 0.3 }, { 'maxNeck', 'Neck turn (deg)', 30, 170 } }) do
            V[d[1]] = V[d[1]] or imgui.new.float(0)
            V[d[1]][0] = num(cfg.main[d[1]], d[3])
            if imgui.SliderFloat(d[2], V[d[1]], d[3], d[4], '%.3f') then cfg.main[d[1]] = V[d[1]][0]; ch = true end
        end
        imgui.TextDisabled(getBone and 'Head bone: CPed::GetBonePosition' or 'Head bone not found: using body position')
        imgui.TextDisabled('Drag on the right part of the screen to look around.')
        imgui.TextDisabled('Still see your head? Raise "Eye forward".')
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
        printStringNow('First person ON - drag right side to look, /fpp off, /fppcfg settings', 2500)
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
