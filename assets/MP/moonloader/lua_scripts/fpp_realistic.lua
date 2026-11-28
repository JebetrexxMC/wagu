-- ZyZ First Person for MP (Android MonetLoader) 2.0-mp
-- Replaces "FPP Realistic" 1.2-mp (same file name, so the Script Manager keeps one first person script).
--
-- Rebuilt from "ZyZ First Person" 0.8.3 (libZyZFirstPerson64.so, AML mod by ZyZz reincarnate), itself a port of
-- "Ultimate First Person Mod [SA]" by peter_99. The native mod only exists for 64-bit and hooks the camera in C++;
-- this script does the same job from Lua so it runs on 32-bit and 64-bit MP alike. What was taken from the mod
-- (read out of the .so with Ghidra):
--   * the full settings list: the same ZyZFirstPerson.ini sections and keys (General / Camera / Switches / Input /
--     OnFoot / Vehicle / Body / Weapons / Effects) with the mod's own defaults and limits;
--   * the per-weapon recoil and sights table (pitch kick, roll, time, kick end, sights FOV) and the [Weapon.N]
--     overrides;
--   * the "cinematic" animation list (moonloader/config/ZyZFirstPerson/cinematic_anim_names.txt): while one of those
--     plays (climbing, getting up, falling, dancing, eating, shops...) the view goes third person, or follows the
--     body when Body > CinematicOnFoot is on;
--   * Source style movement (SourceSpeed: running speed, sprint x1.4, crouched x0.35, aiming x0.8; Accelerate,
--     Friction, AirAccelerate), head bob, drunk sway, look smoothing, recenter and look back in vehicles, bodycam
--     and glasses overlays, crosshair dot, menu button (tap = menu, hold = first person on / off).
-- Not possible from a script (native hooks only), so left out: gyroscope, lean buttons, camera roll, hiding the head
-- model (the eye sits in front of the face instead), drive-by camera, radio boost, explosion blur, first person
-- inside cutscenes, task based cinematic switching (cinematic_task_id.txt is read for the anim list only).
--
--   /fpp      first person on / off
--   /fppcfg   settings menu (also: tap the FP button on the left)

script_name('ZyZ First Person')
script_author('ZyZz reincarnate (mod), peter_99 (PC original), MP port')
script_version('2.0-mp')

local imgui = require 'mimgui'
local inicfg = require 'inicfg'
local ffi = require 'ffi'

-- ---------------------------------------------------------------- settings (ZyZFirstPerson.ini)
-- { section, key, type ('b' bool, 'i' int, 'f' float), default, min, max }  - same as the native mod
local SPEC = {
    { 'General', 'ConfigVersion', 'i', 16, 1, 1000 }, { 'General', 'Enabled', 'b', true },
    { 'General', 'LogLevel', 'i', 1, 0, 2 }, { 'General', 'StartInFirstPerson', 'b', false },
    { 'General', 'MenuButton', 'b', true }, { 'General', 'MenuButtonX', 'f', 0.012, 0, 0.95 },
    { 'General', 'MenuButtonY', 'f', 0.36, 0, 0.95 }, { 'General', 'MenuButtonFade', 'f', 10, 0, 600 },
    { 'Camera', 'FOV', 'f', 90, 30, 150 }, { 'Camera', 'NearClip', 'f', 0.065, 0.02, 1 },
    { 'Camera', 'HeadBob', 'f', 0.5, 0, 1 }, { 'Camera', 'CrosshairDot', 'b', true },
    { 'Camera', 'CrosshairDotOnlyAiming', 'b', true },
    { 'Switches', 'ViewClamp', 'b', true }, { 'Switches', 'AutoSwitchScripted', 'b', true },
    { 'Switches', 'ThirdPersonBusted', 'b', false }, { 'Switches', 'ThirdPersonWasted', 'b', false },
    { 'Switches', 'HoldTime', 'i', 500, 100, 5000 },
    { 'Input', 'TouchSensitivity', 'f', 0.06, 0.001, 2 }, { 'Input', 'TouchDeadzone', 'f', 0.6, 0, 20 },
    { 'Input', 'LookSmoothing', 'f', 0.3, 0, 1 }, { 'Input', 'TouchSettleTime', 'i', 500, 0, 5000 },
    { 'Input', 'InvertX', 'b', false }, { 'Input', 'InvertY', 'b', false },
    { 'OnFoot', 'YawLimit', 'f', 110, 0, 110 }, { 'OnFoot', 'RotateBodyStandingStill', 'b', true },
    { 'OnFoot', 'LateralMult', 'f', 1.5, 1, 3 }, { 'OnFoot', 'SourceMovement', 'b', false },
    { 'OnFoot', 'SourceSpeed', 'f', 5, 2, 10 }, { 'OnFoot', 'SourceAccelerate', 'f', 10, 1, 20 },
    { 'OnFoot', 'SourceFriction', 'f', 4, 1, 10 }, { 'OnFoot', 'SourceAirAccelerate', 'f', 10, 0, 100 },
    { 'Vehicle', 'Enabled', 'b', true }, { 'Vehicle', 'YawLimit', 'f', 110, 0, 160 },
    { 'Vehicle', 'PitchUpLimit', 'f', 80, 0, 89 }, { 'Vehicle', 'Recenter', 'b', true },
    { 'Vehicle', 'RecenterOnlyMovingForward', 'b', true }, { 'Vehicle', 'RecenterSpeed', 'f', 80, 1, 200 },
    { 'Vehicle', 'AutoLookBack', 'b', true }, { 'Vehicle', 'LookBackSpeed', 'f', 0.03, 0.005, 0.5 },
    { 'Vehicle', 'LookBackOffset', 'f', 0.4, 0, 1 }, { 'Vehicle', 'PassengersLook', 'b', true },
    { 'Body', 'CinematicOnFoot', 'b', false }, { 'Body', 'CinematicInVehicle', 'b', false },
    { 'Body', 'CinematicSmoothness', 'f', 80, 1, 100 }, { 'Body', 'HeadPitchOffset', 'f', 3, -30, 30 },
    { 'Body', 'SprintFollowHeading', 'b', true }, { 'Body', 'SprintRecenterPitch', 'b', true },
    { 'Body', 'JetpackRecenter', 'b', true }, { 'Body', 'JetpackRecenterSpeed', 'f', 80, 1, 100 },
    { 'Body', 'HeadTurnLimit', 'f', 70, 0, 120 },
    { 'Weapons', 'Recoil', 'b', true }, { 'Weapons', 'AimDownSights', 'b', true },
    { 'Weapons', 'RecoilCrouched', 'f', 1, 0, 2 }, { 'Weapons', 'RecoilInVehicle', 'f', 1, 0, 2 },
    { 'Weapons', 'RecoilJetpack', 'f', 1, 0, 2 }, { 'Weapons', 'RecoilTwoPistols', 'f', 1, 0, 2 },
    { 'Effects', 'SniperShake', 'b', true }, { 'Effects', 'ShakeStrength', 'f', 5, 0.1, 10 },
    { 'Effects', 'DrunkSway', 'b', true }, { 'Effects', 'DrunkStrength', 'f', 1, 0, 3 },
    { 'Effects', 'Glasses', 'b', false }, { 'Effects', 'GlassesTintR', 'f', 0.55, 0, 1 },
    { 'Effects', 'GlassesTintG', 'f', 0.75, 0, 1 }, { 'Effects', 'GlassesTintB', 'f', 1, 0, 1 },
    { 'Effects', 'GlassesCenterTint', 'f', 1, 0, 1 }, { 'Effects', 'GlassesEdgeTint', 'f', 0.35, 0, 1 },
    { 'Effects', 'GlassesVignette', 'f', 0.65, 0, 1 }, { 'Effects', 'GlassesScratches', 'f', 0.12, 0, 1 },
    { 'Effects', 'Bodycam', 'b', false }, { 'Effects', 'BodycamLens', 'f', 0.6, 0, 1 },
    { 'Effects', 'BodycamGrain', 'f', 0.25, 0, 1 }, { 'Effects', 'BodycamText', 'b', true },
    -- MP only
    { 'MP', 'EyeForward', 'f', 0.19, 0.05, 0.5 }, { 'MP', 'EyeUp', 'f', 0.05, -0.2, 0.3 },
    { 'MP', 'FirstPersonAim', 'b', false },
}
-- weapon id -> { pitchMax, rollMax, time ms, kickEnd (part of the time), sights FOV (0 = no sights) }
-- the native mod's built-in table (no entry = no recoil)
local WEAPON = {
    [22] = { 1.6, 1.0, 260, 0.35, 70 }, [23] = { 1.2, 0.8, 260, 0.35, 70 }, [24] = { 3.5, 2.0, 380, 0.3, 70 },
    [25] = { 4.0, 2.5, 420, 0.3, 75 }, [26] = { 4.5, 3.0, 420, 0.3, 75 }, [27] = { 3.0, 2.0, 360, 0.3, 75 },
    [28] = { 0.8, 0.6, 140, 0.4, 70 }, [32] = { 0.8, 0.6, 140, 0.4, 70 }, [29] = { 0.7, 0.5, 140, 0.4, 70 },
    [30] = { 1.3, 0.9, 180, 0.4, 60 }, [31] = { 1.1, 0.8, 170, 0.4, 60 }, [33] = { 3.0, 1.5, 450, 0.3, 50 },
    [34] = { 3.5, 1.5, 500, 0.3, 0 }, [35] = { 4.0, 2.0, 600, 0.3, 0 }, [36] = { 4.0, 2.0, 600, 0.3, 0 },
    [38] = { 0.6, 0.6, 120, 0.4, 0 },
}

local INI = 'ZyZFirstPerson.ini'
local defaults = {}
for _, s in ipairs(SPEC) do
    defaults[s[1]] = defaults[s[1]] or {}
    defaults[s[1]][s[2]] = s[4]
end
local firstRun = not doesFileExist(getWorkingDirectory() .. '/config/' .. INI)
local cfg = inicfg.load(defaults, INI)
if firstRun then   -- coming from FPP Realistic: keep "first person on" and the invert choice
    local old = inicfg.load(nil, 'fpp_realistic.ini')
    if old and old.main then
        if old.main.enabled ~= nil then cfg.General.Enabled = old.main.enabled and true or false end
        if old.main.invertY ~= nil then cfg.Input.InvertY = old.main.invertY and true or false end
    end
end
local function save() inicfg.save(cfg, INI) end
save()
local function C(sec, key)
    local v = cfg[sec] and cfg[sec][key]
    if v == nil then v = defaults[sec][key] end
    return v
end
local function F(sec, key) local v = tonumber(C(sec, key)); return (v and v == v) and v or defaults[sec][key] end
local function B(sec, key) local v = C(sec, key); return v == true or v == 1 or v == 'true' end
local function log(lvl, s) if F('General', 'LogLevel') >= lvl then print('[zyzfp] ' .. s) end end

-- per weapon: built-in table, overridden by a [Weapon.N] section (RecoilPitch, RecoilRoll, RecoilTime, RecoilKick,
-- Recoil=0 to switch off, AimDownSights=0, SightsFOV)
local function weaponData(w)
    local d = WEAPON[w]
    local o = cfg['Weapon.' .. w]
    if not d and not o then return nil end
    d = d and { d[1], d[2], d[3], d[4], d[5] } or { 1, 0.6, 200, 0.35, 0 }
    local on, ads = true, d[5] > 0
    if o then
        d[1] = tonumber(o.RecoilPitch) or d[1]; d[2] = tonumber(o.RecoilRoll) or d[2]
        d[3] = tonumber(o.RecoilTime) or d[3]; d[4] = tonumber(o.RecoilKick) or d[4]
        d[5] = tonumber(o.SightsFOV) or d[5]
        if o.Recoil ~= nil then on = (o.Recoil == true or tonumber(o.Recoil) == 1) end
        if o.AimDownSights ~= nil then ads = (o.AimDownSights == true or tonumber(o.AimDownSights) == 1) end
    end
    return { pitch = d[1], roll = d[2], time = d[3] / 1000, kick = d[4], fov = d[5], recoil = on, ads = ads and d[5] > 0 }
end

-- cinematic animations (the mod's list; the file can be edited)
local CINE = {}
local function loadCinematic()
    CINE = {}
    local f = io.open(getWorkingDirectory() .. '/config/ZyZFirstPerson/cinematic_anim_names.txt', 'r')
    if f then
        for raw in f:lines() do
            local l = raw:gsub('[%s\r]+', '')
            if l ~= '' and l:sub(1, 1) ~= '#' and l:sub(1, 2) ~= '//' then CINE[#CINE + 1] = l end
        end
        f:close()
    end
    if #CINE == 0 then
        CINE = { 'Crouch_Roll_R', 'Crouch_Roll_L', 'CLIMB_idle', 'CLIMB_jump', 'CLIMB_jump_B', 'CLIMB_Pull',
            'CLIMB_Stand', 'getup', 'getup_front', 'FALL_collapse', 'FALL_fall', 'FALL_front', 'FLOOR_hit',
            'FLOOR_hit_f', 'FALL_SKYDIVE_L', 'FALL_SKYDIVE_R', 'FALL_SKYDIVE_ACCEL', 'PARA_OPEN', 'WEAPON_throw',
            'WEAPON_throwu', 'VEND_Use', 'VEND_Drink_P', 'VEND_Eat_P', 'EAT_Burger', 'EAT_Chicken', 'EAT_Pizza' }
    end
    log(1, #CINE .. ' cinematic animations')
end

-- ---------------------------------------------------------------- game helpers (all guarded)
local function try(fn, ...)
    if type(fn) ~= 'function' then return nil end
    local ok, a, b, c, d = pcall(fn, ...)
    if ok then return a, b, c, d end
    return nil
end

local getBone, setNear, sceneCam = nil, nil, nil
pcall(function()
    local a = getModuleProcAddress('libGTASA.so', '_ZN4CPed15GetBonePositionER5RwV3djb')
    if a and a ~= 0 then getBone = ffi.cast('void(*)(void*, float*, unsigned int, bool)', a) end
end)
pcall(function()
    local a = getModuleProcAddress('libGTASA.so', '_Z24RwCameraSetNearClipPlaneP8RwCameraf')
    local s = getModuleProcAddress('libGTASA.so', 'Scene')
    if a and a ~= 0 and s and s ~= 0 then
        setNear = ffi.cast('void*(*)(void*, float)', a)
        sceneCam = ffi.cast('void**', s) + 1      -- RwScene { RpWorld *world; RwCamera *camera; }
    end
end)
local vec = ffi.new('float[3]')
local function headPos(ped)
    local x, y, z = getCharCoordinates(ped)
    if getBone then
        local ok = pcall(function() getBone(ffi.cast('void*', getCharPointer(ped)), vec, 8, true) end)
        if ok and vec[0] == vec[0] and (vec[0] ~= 0 or vec[1] ~= 0) then
            local dx, dy, dz = vec[0] - x, vec[1] - y, vec[2] - z
            if dx * dx + dy * dy + dz * dz < 4 then return vec[0], vec[1], vec[2] end
        end
    end
    return x, y, z + 0.65
end
local function nearClip(v)
    if setNear and sceneCam then pcall(function() local c = sceneCam[0]; if c ~= nil then setNear(c, v) end end) end
end

local function wrap180(a) a = (a + 180) % 360; return a - 180 end
local function clamp(v, a, b) return v < a and a or (v > b and b or v) end
local function bodyYawOf() return (-getCharHeading(PLAYER_PED)) % 360 end
local function stick()
    local lx, ly = try(getPositionOfAnalogueSticks, 0)
    return tonumber(lx) or 0, tonumber(ly) or 0
end
local function aiming()
    if try(isPlayerTargettingAnything, PLAYER_HANDLE) then return true end
    local w = getCurrentCharWeapon(PLAYER_PED)
    return (w >= 22 and w <= 38 or w == 43) and isButtonPressed and try(isButtonPressed, 0, 6) and true or false
end
local function busted()
    return try(isPlayerArrested, PLAYER_HANDLE) or try(hasCharBeenArrested, PLAYER_PED) or false
end

-- ---------------------------------------------------------------- state
local fpOn = false                -- first person wanted (General.Enabled / StartInFirstPerson / toggle)
local active = false              -- camera fixed by us right now
local yaw, pitch, vYaw, vPitch = 0, 0, 0, 0
local vehYaw, vehPitch = 0, 0
local sx, sy, sz
local stepPhase, bobMix = 0, 0
local lastClock = os.clock()
local lastTouch = 0               -- os.clock() of the last look drag
local recoilT, recoilD = 99, nil  -- time since the last shot, its weapon data
local recoilRoll = 0
local lastAmmo, lastWeapon = -1, -1
local cineUntil, cineCheck = 0, 0
local curFov = 70
local menu = imgui.new.bool(false)
local btnShownAt = os.clock()

local function setYawFromPed()
    yaw = bodyYawOf(); vYaw = yaw
    pitch, vPitch, vehYaw, vehPitch = 0, 0, 0, 0
end
local function release()
    if active then
        try(cameraPersistFov, false)
        restoreCamera()
        nearClip(0.1)
        active = false
    end
    sx = nil
end
local function setFov(f)
    if math.abs(f - curFov) > 0.01 or active then
        try(cameraSetLerpFov, f, f, 1, true)
        try(cameraPersistFov, true)
        curFov = f
    end
end

-- ---------------------------------------------------------------- touch: look, menu button
local lookFinger, lastX, lastY, moved = nil, 0, 0, 0
local btnFinger, btnDown = nil, 0
local function btnRect()
    local sw, sh = getScreenResolution()
    local s = math.floor(sh * 0.075)
    local x, y = F('General', 'MenuButtonX') * sw, F('General', 'MenuButtonY') * sh
    return x, y, s
end
local function look(dx, dy)
    local sw = getScreenResolution()
    local s = F('Input', 'TouchSensitivity') * (11 / 3) * (1280 / math.max(640, sw or 1280))
    if dx ~= dx or dy ~= dy or math.abs(dx) > 400 or math.abs(dy) > 400 then return end
    if B('Input', 'InvertX') then dx = -dx end
    if not B('Input', 'InvertY') then dy = -dy end
    if isCharInAnyCar(PLAYER_PED) then
        vehYaw = vehYaw + dx * s
        vehPitch = vehPitch + dy * s
    else
        yaw = (yaw + dx * s) % 360
        pitch = clamp(pitch + dy * s, -85, 80)
    end
    lastTouch = os.clock()
end
local function toggle(on)
    if on == nil then on = not fpOn end
    fpOn = on
    cfg.General.Enabled = on; save()
    if on then setYawFromPed() else release() end
    printStringNow(on and 'First person ON' or 'First person OFF', 1200)
end

function onTouch(type, id, x, y)
    if menu[0] then return end
    local sw = getScreenResolution()
    if type == 2 then                -- push
        btnShownAt = os.clock()
        if B('General', 'MenuButton') then
            local bx, by, bs = btnRect()
            if x >= bx and x <= bx + bs and y >= by and y <= by + bs then
                btnFinger, btnDown = id, os.clock()
                return
            end
        end
        if fpOn and lookFinger == nil and x > sw * 0.42 then lookFinger, lastX, lastY, moved = id, x, y, 0 end
    elseif type == 3 then            -- move
        if id == lookFinger then
            local dx, dy = x - lastX, y - lastY
            local dz = F('Input', 'TouchDeadzone')
            if moved < dz * 4 then
                moved = moved + math.abs(dx) + math.abs(dy)
                if moved < dz * 4 then return end
            end
            look(dx, dy); lastX, lastY = x, y
        end
    elseif type == 1 then            -- release
        if id == lookFinger then lookFinger = nil end
        if id == btnFinger then
            btnFinger = nil
            if (os.clock() - btnDown) * 1000 < F('Switches', 'HoldTime') then menu[0] = true end
        end
    end
end

-- ---------------------------------------------------------------- per frame
local function cinematicPlaying()
    local now = os.clock()
    if now >= cineCheck then
        cineCheck = now + 0.2
        cineUntil = 0
        for i = 1, #CINE do
            if try(isCharPlayingAnim, PLAYER_PED, CINE[i]) then cineUntil = now + 0.35; break end
        end
    end
    return now < cineUntil
end

local function sourceMove(dt, bodyYaw)
    if not B('OnFoot', 'SourceMovement') then return end
    local vx, vy, vz = try(getCharVelocity, PLAYER_PED)
    if not vx then return end
    local lx, ly = stick()
    local r = math.rad(vYaw)
    local fx, fy, rx, ry = math.sin(r), math.cos(r), math.cos(r), -math.sin(r)
    local f, s = -ly / 128, lx / 128 * F('OnFoot', 'LateralMult')
    local wx, wy = fx * f + rx * s, fy * f + ry * s
    local wl = math.sqrt(wx * wx + wy * wy)
    local speed = F('OnFoot', 'SourceSpeed')
    if try(isButtonPressed, 0, 16) then speed = speed * 1.4 end
    if try(isCharDucking, PLAYER_PED) then speed = speed * 0.35 end
    if aiming() then speed = speed * 0.8 end
    local inAir = try(isCharInAir, PLAYER_PED)
    if not inAir then       -- friction
        local sp = math.sqrt(vx * vx + vy * vy)
        if sp > 0.0001 then
            local drop = math.max(sp, 1.0) * F('OnFoot', 'SourceFriction') * dt
            local k = math.max(0, sp - drop) / sp
            vx, vy = vx * k, vy * k
        end
    end
    if wl > 0.05 then
        wx, wy = wx / wl, wy / wl
        local wish = speed * math.min(1, wl)
        local acc = inAir and F('OnFoot', 'SourceAirAccelerate') or F('OnFoot', 'SourceAccelerate')
        if inAir then wish = math.min(wish, 0.76) end
        local add = wish - (vx * wx + vy * wy)
        if add > 0 then
            local a = math.min(add, acc * dt * speed)
            vx, vy = vx + a * wx, vy + a * wy
        end
    end
    try(setCharVelocity, PLAYER_PED, vx, vy, vz)
end

local function update()
    local now = os.clock()
    local dt = clamp(now - lastClock, 0.001, 0.1)
    lastClock = now
    if not fpOn or not doesCharExist(PLAYER_PED) or (isPauseMenuActive and isPauseMenuActive()) then release(); return end
    local dead = isCharDead(PLAYER_PED)
    if dead and B('Switches', 'ThirdPersonWasted') then release(); return end
    if busted() and B('Switches', 'ThirdPersonBusted') then release(); return end
    if B('Switches', 'AutoSwitchScripted') and not dead and try(isPlayerControlOn, PLAYER_HANDLE) == false then
        release(); return                                   -- a script / mission has the camera
    end
    local inCar = isCharInAnyCar(PLAYER_PED)
    if inCar and not B('Vehicle', 'Enabled') then release(); return end

    -- shots: recoil and sniper shake (ammo going down = a shot)
    local w = getCurrentCharWeapon(PLAYER_PED)
    local ammo = try(getAmmoInCharWeapon, PLAYER_PED, w) or -1
    if w == lastWeapon and ammo >= 0 and ammo < lastAmmo then
        local d = weaponData(w)
        if d and d.recoil and B('Weapons', 'Recoil') then
            recoilT, recoilD = 0, d
            recoilRoll = (math.random() * 2 - 1) * d.roll
        end
        if w == 34 and B('Effects', 'SniperShake') then try(shakeCam, math.floor(F('Effects', 'ShakeStrength') * 8)) end
    end
    lastWeapon, lastAmmo = w, ammo
    recoilT = recoilT + dt
    local recP, recY = 0, 0
    if recoilD and recoilT < recoilD.time then
        local t = recoilT / recoilD.time
        local k = t < recoilD.kick and (t / recoilD.kick) or (1 - (t - recoilD.kick) / (1 - recoilD.kick))
        local m = 1
        if inCar then m = F('Weapons', 'RecoilInVehicle')
        elseif try(isCharDucking, PLAYER_PED) then m = F('Weapons', 'RecoilCrouched')
        elseif try(isPlayerUsingJetpack, PLAYER_HANDLE) then m = F('Weapons', 'RecoilJetpack') end
        recP, recY = recoilD.pitch * k * m, recoilRoll * k * m * 0.5
    end

    -- aiming: the game's aim camera (shots go where the game aims), zoomed to the weapon's sights
    local aim = not inCar and not dead and aiming()
    if aim and not B('MP', 'FirstPersonAim') then
        if active then release() end
        local d = weaponData(w)
        if d and d.ads and B('Weapons', 'AimDownSights') then try(cameraSetLerpFov, d.fov, d.fov, 1, true) end
        setYawFromPed()
        return
    end

    -- cinematic animations: third person, or the view follows the body
    local cine = not dead and cinematicPlaying()
    local followBody = false
    if cine then
        if (inCar and not B('Body', 'CinematicInVehicle')) or (not inCar and not B('Body', 'CinematicOnFoot')) then
            if active then release() end
            setYawFromPed()
            return
        end
        followBody = true
    end

    -- the head, smoothed
    local hx, hy, hz = headPos(PLAYER_PED)
    local ls = clamp(F('Input', 'LookSmoothing'), 0, 1)
    if sx == nil or inCar or (hx - sx) ^ 2 + (hy - sy) ^ 2 + (hz - sz) ^ 2 > 4 then
        sx, sy, sz = hx, hy, hz
    else
        local kxy = 1 - math.exp(-dt * 40)
        local kz = 1 - math.exp(-dt * 18)
        sx, sy, sz = sx + (hx - sx) * kxy, sy + (hy - sy) * kxy, sz + (hz - sz) * kz
    end
    local settled = (now - lastTouch) * 1000 > F('Input', 'TouchSettleTime') and lookFinger == nil

    local ly, lp, bodyYaw
    local bobZ, bobSide, bobPitch = 0, 0, 0
    local eyeSide = 0
    if inCar then
        local car = storeCarCharIsInNoSave(PLAYER_PED)
        bodyYaw = (-getCarHeading(car)) % 360
        local driver = try(getDriverOfCar, car) == PLAYER_PED
        local yl = F('Vehicle', 'YawLimit')
        if not driver and not B('Vehicle', 'PassengersLook') then vehYaw, vehPitch = 0, 0 end
        -- moving forward / backward (the car's speed along its nose)
        local cvx, cvy = try(getCarSpeedVector, car)
        local br = math.rad(bodyYaw)
        local fwd = (tonumber(cvx) or 0) * math.sin(br) + (tonumber(cvy) or 0) * math.cos(br)
        if driver and B('Vehicle', 'AutoLookBack') and settled and fwd < -1.5 then
            local want = vehYaw >= 0 and 180 or -180
            vehYaw = vehYaw + (want - vehYaw) * clamp(F('Vehicle', 'LookBackSpeed') * dt * 60, 0, 1)
            eyeSide = (vehYaw >= 0 and 1 or -1) * F('Vehicle', 'LookBackOffset') * 0.5 * math.abs(vehYaw) / 180
        else
            if math.abs(vehYaw) > yl then vehYaw = clamp(wrap180(vehYaw), -yl, yl) end
            if B('Vehicle', 'Recenter') and settled and (fwd > 1 or not B('Vehicle', 'RecenterOnlyMovingForward')) then
                local step = F('Vehicle', 'RecenterSpeed') * dt
                if math.abs(vehYaw) <= step then vehYaw = 0 else vehYaw = vehYaw - (vehYaw > 0 and step or -step) end
                if math.abs(vehPitch) <= step then vehPitch = 0 else vehPitch = vehPitch - (vehPitch > 0 and step or -step) end
            end
        end
        vehPitch = clamp(vehPitch, -60, F('Vehicle', 'PitchUpLimit'))
        if followBody then vehYaw, vehPitch = vehYaw * 0.9, vehPitch * 0.9 end
        ly, lp = (bodyYaw + vehYaw) % 360, vehPitch
        vYaw, vPitch = ly, lp
    else
        bodyYaw = bodyYawOf()
        local speed = getCharSpeed(PLAYER_PED)
        local moving = speed > 0.5
        local lx, sly = stick()
        local sprinting = moving and speed > 6
        if followBody then
            local k = 1 - math.exp(-dt * (101 - F('Body', 'CinematicSmoothness')) * 0.15)
            yaw = (yaw + wrap180(bodyYaw - yaw) * k) % 360
            pitch = pitch + (0 - pitch) * k
        elseif try(isPlayerUsingJetpack, PLAYER_HANDLE) and B('Body', 'JetpackRecenter') and settled then
            local step = F('Body', 'JetpackRecenterSpeed') * dt
            local d = wrap180(bodyYaw - yaw)
            yaw = (yaw + clamp(d, -step, step)) % 360
        elseif sprinting and settled then
            if B('Body', 'SprintFollowHeading') and sly > -30 then
                yaw = (yaw + wrap180(bodyYaw - yaw) * (1 - math.exp(-dt * 4))) % 360
            end
            if B('Body', 'SprintRecenterPitch') then pitch = pitch * math.max(0, 1 - dt * 2) end
        end
        if B('Effects', 'DrunkSway') then
            local dr = tonumber(try(getPlayerDrunkenness, PLAYER_HANDLE)) or 0
            if dr > 0 then
                local a = F('Effects', 'DrunkStrength') * math.min(1, dr / 255) * 6
                yaw = (yaw + math.sin(now * 0.9) * a * dt) % 360
                pitch = pitch + math.sin(now * 1.3) * a * 0.5 * dt
            end
        end
        local kv = 1 - math.exp(-dt * (40 - 35 * ls))
        vYaw = (vYaw + wrap180(yaw - vYaw) * kv) % 360
        vPitch = vPitch + (pitch - vPitch) * kv
        ly, lp = vYaw, vPitch
        -- the body: faces the view when walking forward; turns once the view passes the neck limit
        local rel = wrap180(ly - bodyYaw)
        local neck = clamp(F('OnFoot', 'YawLimit'), 10, 110)
        if not dead and not followBody then
            if moving and sly < -30 then
                bodyYaw = ly; setCharHeading(PLAYER_PED, (-ly) % 360)
            elseif math.abs(rel) > neck then
                if moving or B('OnFoot', 'RotateBodyStandingStill') then
                    bodyYaw = (ly - (rel > 0 and neck or -neck)) % 360
                    setCharHeading(PLAYER_PED, (-bodyYaw) % 360)
                elseif B('Switches', 'ViewClamp') then
                    yaw = (bodyYaw + (rel > 0 and neck or -neck)) % 360; vYaw = yaw; ly = yaw
                end
            end
            sourceMove(dt, bodyYaw)
        end
        -- walking bob
        local hb = F('Camera', 'HeadBob')
        local want = (hb > 0 and moving and not try(isCharInAir, PLAYER_PED)) and 1 or 0
        bobMix = bobMix + (want - bobMix) * (1 - math.exp(-dt * (want > bobMix and 4 or 6)))
        if bobMix > 0.001 then
            local sps = clamp(1.4 + speed * 0.28, 1.6, 3.4)
            stepPhase = (stepPhase + dt * sps * math.pi) % (2 * math.pi)
            local k = clamp(speed / 4.5, 0.45, 1.4) * bobMix * hb * 2
            bobZ = -0.045 * k * (1 - math.cos(2 * stepPhase)) * 0.5
            bobSide = 0.025 * k * math.sin(stepPhase)
            bobPitch = -0.9 * k * math.sin(2 * stepPhase)
        else
            stepPhase = 0
        end
    end

    -- the eye: in front of the face along the body (the head stays out of the picture), the look along the view
    local br = math.rad(bodyYaw)
    local bfx, bfy, brx, bry = math.sin(br), math.cos(br), math.cos(br), -math.sin(br)
    local fw, up = F('MP', 'EyeForward'), F('MP', 'EyeUp')
    if not inCar and lp < -30 then fw = fw + (-lp - 30) / 55 * 0.08 end
    local side = bobSide + eyeSide
    local cx, cy, cz = sx + bfx * fw + brx * side, sy + bfy * fw + bry * side, sz + up + bobZ
    local r = math.rad(ly + recY)
    local p = math.rad(clamp(lp + bobPitch + recP + F('Body', 'HeadPitchOffset') * 0.1, -89, 89))
    local fx, fy, fz = math.sin(r) * math.cos(p), math.cos(r) * math.cos(p), math.sin(p)
    setFixedCameraPosition(cx, cy, cz, 0.0, 0.0, 0.0)
    pointCameraAtPoint(cx + fx * 10, cy + fy * 10, cz + fz * 10, 2)
    active = true
    local fov = F('Camera', 'FOV')
    if B('Effects', 'Bodycam') then fov = fov + F('Effects', 'BodycamLens') * 20 end
    if aim then   -- FirstPersonAim on: zoom to the sights in first person
        local d = weaponData(w)
        if d and d.ads and B('Weapons', 'AimDownSights') then fov = d.fov end
    end
    setFov(clamp(fov, 30, 150))
    nearClip(F('Camera', 'NearClip'))
end

-- ---------------------------------------------------------------- overlays: button, crosshair dot, glasses, bodycam
local function col(r, g, b, a) return imgui.ColorConvertFloat4ToU32(imgui.ImVec4(r, g, b, a)) end
imgui.OnFrame(function() return true end, function(self)
    self.HideCursor = true
    local dl = imgui.GetBackgroundDrawList()
    local sw, sh = getScreenResolution()
    if active then
        if B('Effects', 'Glasses') then
            local tr, tg, tb = F('Effects', 'GlassesTintR'), F('Effects', 'GlassesTintG'), F('Effects', 'GlassesTintB')
            local ca = (1 - F('Effects', 'GlassesCenterTint')) * 0.35 + 0.05
            dl:AddRectFilled(imgui.ImVec2(0, 0), imgui.ImVec2(sw, sh), col(tr, tg, tb, ca))
            local ea = F('Effects', 'GlassesVignette') * (1 - F('Effects', 'GlassesEdgeTint')) * 0.9
            local e, n = col(0, 0, 0, ea), col(0, 0, 0, 0)
            local bw = sw * 0.18
            dl:AddRectFilledMultiColor(imgui.ImVec2(0, 0), imgui.ImVec2(bw, sh), e, n, n, e)
            dl:AddRectFilledMultiColor(imgui.ImVec2(sw - bw, 0), imgui.ImVec2(sw, sh), n, e, e, n)
            local sc = F('Effects', 'GlassesScratches')
            if sc > 0 then
                math.randomseed(7)
                for _ = 1, math.floor(sc * 40) do
                    local x, y = math.random() * sw, math.random() * sh
                    dl:AddLine(imgui.ImVec2(x, y), imgui.ImVec2(x + math.random(-40, 40), y + math.random(-6, 6)), col(1, 1, 1, 0.12), 1)
                end
                math.randomseed(os.time())
            end
        end
        if B('Effects', 'Bodycam') then
            local e, n = col(0, 0, 0, 0.55 * F('Effects', 'BodycamLens') + 0.2), col(0, 0, 0, 0)
            local bw, bh = sw * 0.2, sh * 0.2
            dl:AddRectFilledMultiColor(imgui.ImVec2(0, 0), imgui.ImVec2(bw, sh), e, n, n, e)
            dl:AddRectFilledMultiColor(imgui.ImVec2(sw - bw, 0), imgui.ImVec2(sw, sh), n, e, e, n)
            dl:AddRectFilledMultiColor(imgui.ImVec2(0, 0), imgui.ImVec2(sw, bh), e, e, n, n)
            dl:AddRectFilledMultiColor(imgui.ImVec2(0, sh - bh), imgui.ImVec2(sw, sh), n, n, e, e)
            for _ = 1, math.floor(F('Effects', 'BodycamGrain') * 600) do
                local x, y = math.random() * sw, math.random() * sh
                local v = math.random()
                dl:AddRectFilled(imgui.ImVec2(x, y), imgui.ImVec2(x + 2, y + 2), col(v, v, v, 0.18))
            end
            if B('Effects', 'BodycamText') then
                dl:AddText(imgui.ImVec2(sw - sw * 0.34, sh * 0.05), col(1, 1, 1, 0.9),
                    os.date('%Y-%m-%d T%H:%M:%S') .. '\nAXON BODY 3  X6031' .. string.format('%04d', (PLAYER_PED or 0) % 10000))
            end
        end
        if B('Camera', 'CrosshairDot') and not B('Camera', 'CrosshairDotOnlyAiming') then
            local w = getCurrentCharWeapon(PLAYER_PED)
            if w >= 22 and w <= 38 then dl:AddCircleFilled(imgui.ImVec2(sw / 2, sh / 2), 3, col(1, 1, 1, 0.85)) end
        end
    end
    if B('General', 'MenuButton') and not menu[0] then
        local bx, by, bs = btnRect()
        local fade = F('General', 'MenuButtonFade')
        local a = (fade > 0 and os.clock() - btnShownAt > fade) and 0.25 or 0.75
        if btnFinger and (os.clock() - btnDown) * 1000 >= F('Switches', 'HoldTime') then
            btnFinger = nil; toggle()
        end
        dl:AddRectFilled(imgui.ImVec2(bx, by), imgui.ImVec2(bx + bs, by + bs), col(0.05, 0.05, 0.08, a * 0.8), bs * 0.25)
        dl:AddRect(imgui.ImVec2(bx, by), imgui.ImVec2(bx + bs, by + bs), fpOn and col(0.2, 1, 0.6, a) or col(1, 1, 1, a * 0.6), bs * 0.25, 15, 2)
        local t = 'FP'
        local ts = imgui.CalcTextSize(t)
        dl:AddText(imgui.ImVec2(bx + (bs - ts.x) / 2, by + (bs - ts.y) / 2), col(1, 1, 1, a), t)
    end
end)

-- ---------------------------------------------------------------- menu
local V = {}
local TABS = { 'General', 'Camera', 'Switches', 'Input', 'OnFoot', 'Vehicle', 'Body', 'Weapons', 'Effects', 'MP' }
local function label(k) return (k:gsub('(%l)(%u)', '%1 %2')) end
imgui.OnFrame(function() return menu[0] end, function(self)
    self.HideCursor = false
    local sw, sh = getScreenResolution()
    imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.FirstUseEver, imgui.ImVec2(0.5, 0.5))
    imgui.SetNextWindowSize(imgui.ImVec2(math.min(sw * 0.6, 760), sh * 0.75), imgui.Cond.FirstUseEver)
    if imgui.Begin('ZyZ First Person 2.0-mp', menu) then
        local on = imgui.new.bool(fpOn)
        if imgui.Checkbox('First person', on) then toggle(on[0]) end
        imgui.SameLine(); imgui.TextDisabled('(ZyZz reincarnate, peter_99)')
        if imgui.BeginTabBar('t') then
            for _, tab in ipairs(TABS) do
                if imgui.BeginTabItem(tab) then
                    local ch = false
                    for _, s in ipairs(SPEC) do
                        if s[1] == tab and s[2] ~= 'ConfigVersion' then
                            local id = s[1] .. '.' .. s[2]
                            if s[3] == 'b' then
                                V[id] = V[id] or imgui.new.bool(false); V[id][0] = B(s[1], s[2])
                                if imgui.Checkbox(label(s[2]) .. '##' .. id, V[id]) then cfg[s[1]][s[2]] = V[id][0]; ch = true end
                            elseif s[3] == 'i' then
                                V[id] = V[id] or imgui.new.int(0); V[id][0] = math.floor(F(s[1], s[2]))
                                if imgui.SliderInt(label(s[2]) .. '##' .. id, V[id], s[5], s[6]) then cfg[s[1]][s[2]] = V[id][0]; ch = true end
                            else
                                V[id] = V[id] or imgui.new.float(0); V[id][0] = F(s[1], s[2])
                                if imgui.SliderFloat(label(s[2]) .. '##' .. id, V[id], s[5], s[6], '%.3f') then cfg[s[1]][s[2]] = V[id][0]; ch = true end
                            end
                        end
                    end
                    if tab == 'Weapons' then imgui.TextDisabled('Per weapon: add [Weapon.<id>] to ZyZFirstPerson.ini (RecoilPitch, RecoilRoll, RecoilTime, RecoilKick, SightsFOV, Recoil, AimDownSights).') end
                    if tab == 'MP' then imgui.TextDisabled('First Person Aim keeps first person while aiming (shots may not follow the view). Off = the game aim camera, zoomed to the sights.') end
                    if tab == 'Body' and imgui.Button('Reload cinematic list') then loadCinematic() end
                    if ch then save() end
                    imgui.EndTabItem()
                end
            end
            imgui.EndTabBar()
        end
        if imgui.Button('Defaults') then
            for _, s in ipairs(SPEC) do cfg[s[1]][s[2]] = s[4] end
            save()
        end
        imgui.SameLine(); imgui.TextDisabled('Drag on the right part of the screen to look around.')
    end
    imgui.End()
end)

-- ---------------------------------------------------------------- main
function main()
    while not isPlayerPlaying(PLAYER_HANDLE) do wait(200) end
    loadCinematic()
    fpOn = B('General', 'Enabled') or B('General', 'StartInFirstPerson')
    setYawFromPed()
    sampRegisterChatCommand('fpp', function() toggle() end)
    sampRegisterChatCommand('fppcfg', function() menu[0] = not menu[0] end)
    if fpOn then
        printStringNow('First person ON - drag right side to look, tap FP for the menu, hold FP or /fpp to switch', 2500)
    end
    log(1, 'ZyZ First Person 2.0-mp' .. (getBone and '' or ' (head bone not found, using the body position)'))
    while true do
        wait(0)
        local ok, err = pcall(update)
        if not ok then log(0, tostring(err)); release(); wait(500) end
    end
end

function onScriptTerminate(s)
    if s == thisScript() and active then pcall(restoreCamera); pcall(cameraPersistFov, false) end
end
