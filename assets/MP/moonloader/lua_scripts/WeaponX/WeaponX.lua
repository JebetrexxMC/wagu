require 'lib.moonloader'
local imgui = require 'mimgui'
local memory = require 'memory'
local inicfg = require 'inicfg'
local vkeys = require 'vkeys'
local encoding = require 'encoding'
local sound_state = require 'moonloader'.audiostream_state
encoding.default = 'CP1251'
local u8 = encoding.UTF8

script_name('SA-MP Weapon Wheel (mimgui)')
script_author('mistus_')
script_version('1.4.6-mp')

-- MP 5.02.37 (Android / MonetLoader port of WeaponX 1.4.6 by mistus_):
--   * /weaponx opens the wheel for touch: tap a segment to equip, tap outside to close. A small "WX" button
--     (on by default, drag it anywhere, its place is saved) opens it too. Hold X still works with a keyboard.
--   * /weaponx cfg (or /wheelcfg) opens a plain imgui settings window; /wheel turns the wheel on / off.
--   * ammo in clip / owned weapons: PC reads CPed + 0x5A0; on Android lib/mp_weaponslots.lua finds the real offset.
--   * the snap sound is in this folder (resource/audio/GameSounds/notification.mp3).
local MP_ANDROID = (MONET_VERSION ~= nil) or (jit and jit.os == 'Linux')
local okws, ws = pcall(require, 'mp_weaponslots')
if not okws then ws = nil end

-- =========================
-- Config
-- =========================
local cfg_path = 'weapon_wheel.ini'
local cfg = inicfg.load({
    main = {
        enabled = true,
        hold_key = vkeys.VK_X,     -- hold to open (default: X)
        radius = 210,              -- outer radius
        inner_radius = 70,         -- dead zone (no selection)
        ring_thickness = 42,       -- visual thickness
        segments = 12,
        activate_on_release = true, -- equip when key released (otherwise click)
        show_cursor = true,
        allow_movement = true,          -- allow moving while wheel is open (uses cursor mode 2 when available)
        combo_shortcuts = true,          -- X + [1..] quick slot selection
        combo_instant_equip = false,     -- if true: X+key instantly equips and closes (otherwise just preselects)
        block_when_chat = true,

        -- while hovering a segment, weapon changes immediately (debounced)
        preview_on_hover = true,
        preview_debounce_ms = 120,

        -- Aesthetics / UX
        accent_color = 0xFFE3A14F, -- ARGB (warm gold)
        anim_speed = 14.0,         -- higher = snappier open/close fade
        selected_pop = 10,         -- px pop for selected segment
        show_icons = true,         -- show short text "icons"
    
        -- Weapon icon textures (optional)
        use_weapon_icons = true,         -- show PNG weapon icons (0.png..46.png) if available
        icon_dir = 'weapon_wheel',       -- moonloader/resource/<icon_dir>/
        center_icon_scale = 0.95,        -- relative to inner radius (0.6..1.3)
        segment_icon_scale = 10.0,      -- icon size relative to ring thickness (0.4..0.9)
        base_icon_boost = 1.63,        -- base boost for non-hovered icons (+55%)
        hover_icon_scale = 1.8,       -- hovered icon scale multiplier (1.8 = +80%)
        smooth_icon_scale = true,       -- smooth lerp for icon scale on hover changes
        icon_scale_speed = 18.0,        -- higher = snappier scale interpolation
        rotate_icons = true,            -- rotate icons slightly toward cursor direction
        rotate_strength = 0.35,         -- 0..1 (how much it rotates toward cursor)
        hover_snap_sound = true,        -- play a tiny sound on hover change
        snap_mode = 'audiostream',      -- 'audiostream' (mp3/wav via loadAudioStream) or 'wav' (playSound)
        snap_audio_path = 'resource\\audio\\GameSounds\\notification.mp3', -- used for audiostream mode (MP: in this folder)
        snap_sound_path = 'resource\\\\weapon_wheel\\\\snap.wav', -- relative to moonloader/
        show_slot_text_in_center = true, -- show slot name in center when hovering
        show_ammo_in_center = true,      -- show ammo in center (under slot name)
        show_segment_labels = false,     -- show text labels around wheel (icons-only look)
        dim_unselected_icons = true,   -- dim icons when not hovered
        dim_alpha = 0.45,              -- 0.0 = invisible, 1.0 = full brightness
        icon_fade_close_mult = 2.4,    -- icon fades faster on close (smooth)

        -- Ammo formatting (optional)
        show_clip_ammo = true,           -- show clip|reserve when possible

        -- Segment glow / gradient
        enable_glow = true,
        glow_strength = 1.0,             -- 0..2
        enable_gradient = true,
        gradient_strength = 1.0,         -- 0..2

        -- MP (Android): on-screen button that opens the wheel for touch
        touch_button = true,
        button_x = -1,                   -- -1 = right edge, middle height
        button_y = -1,
        button_size = 64,
}
}, cfg_path)
inicfg.save(cfg, cfg_path)
local AudioStreamClick = nil
-- =========================
-- Textures (weapon icons)
-- =========================
local weaponTex = {}
local weaponTexReady = false

imgui.OnInitialize(function()
    imgui.SwitchContext()
    -- Load weapon icons like HassleHUD does (0.png..46.png)
    local dir = getWorkingDirectory() .. "\\resource\\" .. tostring(cfg.main.icon_dir or "weapon_wheel")
    if doesDirectoryExist(dir) then
        for i = 0, 46 do
            local path = string.format("%s\\%d.png", dir, i)
            if doesFileExist(path) then
                weaponTex[i] = imgui.CreateTextureFromFile(path)
            end
        end
        weaponTexReady = true
    else
        -- No folder = no icons; wheel still works
        weaponTexReady = false
    end
end)


-- =========================
-- Wheel slots (12) based on GTA SA weapon slots/categories.
-- Each segment is a CATEGORY; we pick the best weapon you actually own from a priority list.
-- =========================

local SLOTS = {

-- Slot 1: Hand
{ name = 'Hand', weapons = { 0, 1 } },

-- Slot 2: Melee Weapons
{ name = 'Melee', weapons = { 4, 2, 6, 7, 3, 5, 8, 9 } },

-- Slot 3: Handguns
{ name = 'Handguns', weapons = { 22, 23, 24 } },

-- Slot 4: Shotguns
{ name = 'Shotguns', weapons = { 25, 26, 27 } },

-- Slot 5: Submachine Guns
{ name = 'SMGs', weapons = { 32, 28, 29 } },

-- Slot 6: Assault Rifles
{ name = 'Assault Rifles', weapons = { 30, 31 } },

-- Slot 7: Rifles
{ name = 'Rifles', weapons = { 34, 33 } },

-- Slot 8: Heavy Weapons
{ name = 'Heavy', weapons = { 37, 35, 36, 38 } },

-- Slot 9: Explosives
{ name = 'Explosives', weapons = { 17, 18, 16, 39 } },

-- Slot 10: Handheld Items
{ name = 'Handheld', weapons = { 42, 41, 43 } },

-- Slot 11: Gifts
{ name = 'Gifts', weapons = { 14, 15, 10, 11, 12, 13 } },

-- Slot 12: Gear
{ name = 'Gear', weapons = { 44, 45, 46 } },

-- Slot 13: Others
{ name = 'Others', weapons = { 40 } }

}


-- Keep segments count in sync with slot layout
if cfg and cfg.main and cfg.main.segments ~= #SLOTS then
    cfg.main.segments = 13
    inicfg.save(cfg, cfg_path)
end

-- Short icon labels-- Short icon labels (text-based) to keep the wheel readable even without textures.
-- (We can replace with real weapon icons later if you want.)
local ICONS = {
    'HN', -- Hand
    'ML', -- Melee
    'HG', -- Handguns
    'SG', -- Shotguns
    'SM', -- SMGs
    'AR', -- Assault Rifles
    'RF', -- Rifles
    'HV', -- Heavy
    'EX', -- Explosives
    'HH', -- Handheld
    'GF', -- Gifts
    'GE', -- Gear
    'OT', -- Others
}


-- Weapon ID -> name (GTA: SA)
local WEAPON_NAME = {
    [0]='Fist',[1]='Brass Knuckles',[2]='Golf Club',[3]='Nightstick',[4]='Knife',[5]='Baseball Bat',[6]='Shovel',[7]='Pool Cue',[8]='Katana',[9]='Chainsaw',
    [10]='Dildo',[11]='Dildo',[12]='Vibrator',[13]='Vibrator',[14]='Flowers',[15]='Cane',
    [16]='Grenade',[17]='Tear Gas',[18]='Molotov',
    [22]='9mm',[23]='Silenced 9mm',[24]='Desert Eagle',
    [25]='Shotgun',[26]='Sawn-Off Shotgun',[27]='Combat Shotgun',
    [28]='Micro SMG',[29]='SMG',[30]='AK-47',[31]='M4',[32]='Tec-9',
    [33]='Rifle',[34]='Sniper Rifle',
    [35]='Rocket Launcher',[36]='Heat-Seeking Rocket Launcher',[37]='Flamethrower',[38]='Minigun',
    [39]='Satchel Charge',[40]='Detonator',
    [41]='Spray Can',[42]='Fire Extinguisher',[43]='Camera',
    [44]='Night Vision',[45]='Thermal Vision',[46]='Parachute'
}
local function weaponDisplayName(id)
    id = tonumber(id) or 0
    return WEAPON_NAME[id] or ('Weapon '..tostring(id))
end
-- =========================
-- Helpers
-- =========================
local function clamp(x, a, b)
    if x < a then return a end
    if x > b then return b end
    return x
end

local function withAlpha(argb, mul)
    -- argb: 0xAARRGGBB (LuaJIT-safe: no bitwise ops)
    mul = mul or 1.0
    if mul <= 0 then return 0x00000000 end
    local a = math.floor(argb / 0x1000000) % 256
    local rgb = argb % 0x1000000
    local na = math.floor(math.max(0, math.min(255, a * mul + 0.5)))
    return na * 0x1000000 + rgb
end

local function easeOutCubic(t)
    if t < 0 then t = 0 elseif t > 1 then t = 1 end
    local u = 1 - t
    return 1 - (u*u*u)
end

local function isUiBlocked()
    if not cfg.main.block_when_chat then return false end
    local ok, r
    if sampIsChatInputActive then ok, r = pcall(sampIsChatInputActive); if ok and r then return true end end
    if sampIsDialogActive then ok, r = pcall(sampIsDialogActive); if ok and r then return true end end
    if sampIsScoreboardOpen then ok, r = pcall(sampIsScoreboardOpen); if ok and r then return true end end
    if isPauseMenuActive and isPauseMenuActive() then return true end
    return false
end


local function hasWeaponBySlotMemory(weapId)
    -- Some SA:MP builds can report 0 ammo for throwables/spray/extinguisher even when owned.
    -- Check CPed weapon slot array directly (CPed + 0x5A0).
    if not weapId or weapId == 0 then return true end
    if not (memory and memory.getuint32) then return false end
    if not (getCharPointer and getWeapontypeSlot) then return false end

    if MP_ANDROID then
        if not ws then return false end
        local okSlot, slot = pcall(getWeapontypeSlot, weapId)
        if not okSlot or type(slot) ~= 'number' then return false end
        return ws.slotType(PLAYER_PED, slot) == weapId
    end
    local okPtr, ptr = pcall(getCharPointer, PLAYER_PED)
    if not okPtr or type(ptr) ~= 'number' then return false end

    local okSlot, slot = pcall(getWeapontypeSlot, weapId)
    if not okSlot or type(slot) ~= 'number' then return false end

    local base = ptr + 0x5A0
    local address = base + slot * 0x1C

    local okRead, wtype = pcall(memory.getuint32, address + 0x0)
    if okRead and type(wtype) == 'number' then
        return wtype == weapId
    end
    return false
end

local function hasWeapon(weapId)
    -- weapon 0 (fist / unarmed) is always available
    if weapId == 0 then return true end

    -- If you are currently holding this weapon, treat it as owned.
    if getCurrentCharWeapon then
        local ok, cw = pcall(getCurrentCharWeapon, PLAYER_PED)
        if ok and type(cw) == 'number' and cw == weapId then return true end
    end

    -- Memory fallback for SA:MP weird ammo/ownership for throwables & equipment

    -- Prefer native ownership check
    if hasCharGotWeapon then
        local ok, r = pcall(hasCharGotWeapon, PLAYER_PED, weapId)
        if ok and r then return true end
    end

    -- Fallback for ranged weapons: ammo > 0 implies owned/usable
    if getAmmoInCharWeapon then
        local ok, ammo = pcall(getAmmoInCharWeapon, PLAYER_PED, weapId)
        if ok and type(ammo) == 'number' and ammo > 0 then return true end
    end

    -- Final fallback: read CPed weapon slots directly (helps with throwables/spray/extinguisher on some SA:MP builds)
    if hasWeaponBySlotMemory(weapId) then return true end

    return false
end

local function pickBestWeapon(list)
    if type(list) ~= 'table' then return 0 end
    for _, wid in ipairs(list) do
        if hasWeapon(wid) then
            return wid
        end
    end
    return 0
end

local function getWeaponAmmoSafe(weapId)
    if not weapId or weapId == 0 then return nil end
    if weapId <= 15 then return nil end
    if getAmmoInCharWeapon then
        local ok, ammo = pcall(getAmmoInCharWeapon, PLAYER_PED, weapId)
        if ok and type(ammo) == 'number' then return ammo end
    end
    return nil
end

local function getAmmoInClipSafe(weapId)
    -- Based on HassleHUD memory method, but safe-guarded.
    if not weapId or weapId == 0 then return nil end
    if weapId <= 15 then return nil end -- melee / misc
    if MP_ANDROID then return ws and ws.clip(PLAYER_PED, weapId) or nil end
    if not (memory and memory.getuint32) then return nil end
    if not (getCharPointer and getWeapontypeSlot) then return nil end

    local okPtr, ptr = pcall(getCharPointer, PLAYER_PED)
    if not okPtr or type(ptr) ~= 'number' then return nil end

    local okSlot, slot = pcall(getWeapontypeSlot, weapId)
    if not okSlot or type(slot) ~= 'number' then return nil end

    -- CPed + 0x5A0 => weapon slot array, each entry 0x1C, ammo-in-clip at +0x8
    local base = ptr + 0x5A0
    local address = base + slot * 0x1C
    local okRead, clip = pcall(memory.getuint32, address + 0x8)
    if okRead and type(clip) == 'number' then return clip end
    return nil
end

local function formatAmmo(weapId)
    local total = getWeaponAmmoSafe(weapId)
    if total == nil then return nil end

    if cfg.main.show_clip_ammo then
        local clip = getAmmoInClipSafe(weapId)
        if clip ~= nil then
            local reserve = total - clip
            if reserve < 0 then reserve = 0 end
            return string.format("%d | %d", clip, reserve)
        end
    end
    return tostring(total)
end

local function equipWeapon(weapId)
    if not cfg.main.enabled then return end
    if not isCharOnFoot(PLAYER_PED) and weapId == 0 then return end
    if not hasWeapon(weapId) then return end
    pcall(setCurrentCharWeapon, PLAYER_PED, weapId)
end

local function keyDown(vk)
    return isKeyDown(vk)
end


-- Combo shortcuts: hold_key + number keys to select slots quickly
-- Mapping (top row & numpad):
--  1..9 => slots 1..9
--  0    => slot 10
--  -    => slot 11
--  =    => slot 12
--  Backspace => slot 13
local COMBO_KEYS = {
    [vkeys.VK_1] = 1, [vkeys.VK_2] = 2, [vkeys.VK_3] = 3, [vkeys.VK_4] = 4, [vkeys.VK_5] = 5,
    [vkeys.VK_6] = 6, [vkeys.VK_7] = 7, [vkeys.VK_8] = 8, [vkeys.VK_9] = 9, [vkeys.VK_0] = 10,
    [vkeys.VK_OEM_MINUS] = 11, [vkeys.VK_OEM_PLUS] = 12, [vkeys.VK_BACK] = 13,

    [vkeys.VK_NUMPAD1] = 1, [vkeys.VK_NUMPAD2] = 2, [vkeys.VK_NUMPAD3] = 3, [vkeys.VK_NUMPAD4] = 4, [vkeys.VK_NUMPAD5] = 5,
    [vkeys.VK_NUMPAD6] = 6, [vkeys.VK_NUMPAD7] = 7, [vkeys.VK_NUMPAD8] = 8, [vkeys.VK_NUMPAD9] = 9, [vkeys.VK_NUMPAD0] = 10,
    [vkeys.VK_SUBTRACT] = 11, [vkeys.VK_ADD] = 12,
}

local comboLastDown = {}
local function comboPressed(vk)
    local d = isKeyDown(vk)
    local last = comboLastDown[vk] or false
    comboLastDown[vk] = d
    return d and not last
end


local function playSnap()
    if not cfg.main.hover_snap_sound then return end

    local mode = tostring(cfg.main.snap_mode or 'audiostream')

    -- Preferred: audiostream (mp3/wav) - reliable & repeatable
    if mode == 'audiostream' then
        if AudioStreamClick and sound_state and setAudioStreamState then
            -- retrigger
            pcall(setAudioStreamState, AudioStreamClick, sound_state.STOP)
            pcall(setAudioStreamState, AudioStreamClick, sound_state.PLAY)
        end
        return
    end

    -- Optional: playSound wav (needs absolute path, may not exist on some builds)
    if mode == 'wav' and playSound then
        local rel = tostring(cfg.main.snap_sound_path or 'resource\\weapon_wheel\\snap.wav')
        local abs = getWorkingDirectory() .. '\\' .. rel
        if doesFileExist and doesFileExist(abs) then
            pcall(playSound, abs)
        end
    end
end

local function nowMs()
    if getGameTimer then
        local ok, t = pcall(getGameTimer)
        if ok and type(t) == 'number' then return t end
    end
    return math.floor(os.clock() * 1000)
end

-- Cursor helpers (SA:MP)
local function setCursor(on)
    if not cfg.main.show_cursor then return end
    if sampSetCursorMode then
        pcall(sampSetCursorMode, on and ((cfg.main.allow_movement and 2) or 1) or 0)
        return
    end
    if sampToggleCursor then
        pcall(sampToggleCursor, on)
        return
    end
end

-- =========================
-- Wheel state
-- =========================
local wheelOpen = imgui.new.bool(false)
local selectedIdx = 0
local lastValidIdx = 0
local lastPreviewIdx = 0
local lastPreviewTick = 0
local lastHold = false
-- MP: touch mode (opened by /weaponx or the WX button): tap a segment to equip, tap outside to close
local touchMode = false
local touchOpenedAt = 0
local settingsOpen = imgui.new.bool(false)
local function openTouch()
    if not cfg.main.enabled then cfg.main.enabled = true; inicfg.save(cfg, cfg_path) end
    wheelOpen[0] = true
    touchMode = true
    touchOpenedAt = os.clock()
end
local function closeWheel()
    wheelOpen[0] = false
    touchMode = false
end

-- Per-slot icon scale smoothing
local iconScale = {}
local lastHoverIdx = 0

-- Animation state (fade + slight scale)
local wheelAlpha = 0.0
local iconAlpha = 0.0 -- separate fade for center icon

-- =========================
-- Drawing / selection math
-- =========================
local function computeSelection(cx, cy, mouseX, mouseY, n, innerR, outerR)
    local dx = mouseX - cx
    local dy = mouseY - cy
    local dist = math.sqrt(dx*dx + dy*dy)
    if dist < innerR or dist > outerR then
        return 0, dist
    end
    -- angle where 0 is "up", clockwise
    local ang = math.atan2(dx, -dy) -- swap to rotate
    if ang < 0 then ang = ang + (math.pi * 2) end
    local idx = math.floor(ang / (math.pi * 2) * n) + 1
    idx = clamp(idx, 1, n)
    return idx, dist
end

local function drawSegment(dl, cx, cy, r0, r1, a0, a1, colFill, colLine, lineW)
    dl:PathClear()
    dl:PathArcTo(imgui.ImVec2(cx, cy), r1, a0, a1, 24)
    dl:PathArcTo(imgui.ImVec2(cx, cy), r0, a1, a0, 24)
    dl:PathFillConvex(colFill)
    dl:PathClear()
    dl:PathArcTo(imgui.ImVec2(cx, cy), r1, a0, a1, 24)
    dl:PathArcTo(imgui.ImVec2(cx, cy), r0, a1, a0, 24)
    dl:PathStroke(colLine, true, lineW)
end

local function mulAlpha(argb, alphaMul)
    return withAlpha(argb, alphaMul)
end

local function drawSegmentFancy(dl, cx, cy, r0, r1, a0, a1, baseFill, baseLine, lineW, isSel, locked, alphaMul, scale)
    alphaMul = alphaMul or 1.0
    scale = scale or 1.0

    local enableGrad = (cfg.main.enable_gradient ~= false) and (not locked)
    local gstr = cfg.main.gradient_strength or 1.0
    if gstr < 0 then gstr = 0 end
    if gstr > 2 then gstr = 2 end

    -- Base fill
    drawSegment(dl, cx, cy, r0, r1, a0, a1, mulAlpha(baseFill, alphaMul), mulAlpha(baseLine, alphaMul), lineW)

    if enableGrad and gstr > 0 then
        local thick = (r1 - r0)
        -- Inner shadow band
        local ir1 = r0 + thick * 0.28
        drawSegment(dl, cx, cy, r0, ir1, a0, a1, mulAlpha(0x1A000000, alphaMul * gstr), 0x00000000, 0)

        -- Outer highlight band
        local or0 = r0 + thick * 0.55
        drawSegment(dl, cx, cy, or0, r1, a0, a1, mulAlpha(0x14FFFFFF, alphaMul * gstr), 0x00000000, 0)
    end

    -- Glow for selected
    local enableGlow = (cfg.main.enable_glow ~= false)
    local g = cfg.main.glow_strength or 1.0
    if g < 0 then g = 0 end
    if g > 2 then g = 2 end

    if enableGlow and isSel and not locked and g > 0 then
        local glowCol = cfg.main.accent_color or 0xFFE3A14F
        -- wide soft stroke behind
        drawSegment(dl, cx, cy, r0, r1 + 1.5 * scale, a0, a1, 0x00000000, mulAlpha(glowCol, alphaMul * 0.35 * g), lineW + 3.5 * scale)
        -- thin bright stroke
        drawSegment(dl, cx, cy, r0, r1, a0, a1, 0x00000000, mulAlpha(0xFFFFFFFF, alphaMul * 0.55 * g), lineW + 0.5 * scale)
    end
end

-- =========================
-- ImGui frame
-- =========================
imgui.OnFrame(
    function() return wheelOpen[0] or wheelAlpha > 0.01 end,
    function(self)
        self.HideCursor = not cfg.main.show_cursor

        local io = imgui.GetIO()
        local dt = io.DeltaTime or 0.016

        -- Smooth open/close
        local target = wheelOpen[0] and 1.0 or 0.0
        local speed = cfg.main.anim_speed or 14.0
        wheelAlpha = wheelAlpha + (target - wheelAlpha) * math.min(1.0, dt * speed)

-- Center icon fades a bit faster when closing (still smooth)
local iconTarget = wheelOpen[0] and 1.0 or 0.0
local closeMul = cfg.main.icon_fade_close_mult or 2.4
if closeMul < 1.0 then closeMul = 1.0 end
local iconSpeed = (iconTarget > iconAlpha) and speed or (speed * closeMul)
iconAlpha = iconAlpha + (iconTarget - iconAlpha) * math.min(1.0, dt * iconSpeed)


        local p = easeOutCubic(wheelAlpha)
        local scale = 0.92 + 0.08 * p

        local sw, sh = getScreenResolution()
        local cx, cy = sw / 2, sh / 2
        local n = #SLOTS
        local baseOuter = cfg.main.radius
        local baseInner = cfg.main.inner_radius
        local baseR0 = cfg.main.radius - cfg.main.ring_thickness

        local outerR = baseOuter * scale
        local r0 = baseR0 * scale
        local innerR = baseInner * scale

        -- mouse position in screen coords
        local mx, my = io.MousePos.x, io.MousePos.y
        local idx, dist = computeSelection(cx, cy, mx, my, n, innerR, outerR)
        selectedIdx = idx

        -- Hover change snap sound
        if wheelOpen[0] and idx ~= lastHoverIdx then
            if idx > 0 then playSnap() end
            lastHoverIdx = idx
        end

        if selectedIdx > 0 and SLOTS[selectedIdx] then
            lastValidIdx = selectedIdx
        end

        -- live preview (change weapon while hovering)
        if cfg.main.preview_on_hover and selectedIdx > 0 and SLOTS[selectedIdx] then
            local tick = nowMs()
            if selectedIdx ~= lastPreviewIdx and (tick - lastPreviewTick) >= (cfg.main.preview_debounce_ms or 120) then
                local wid = pickBestWeapon(SLOTS[selectedIdx].weapons)
                if wid ~= 0 or SLOTS[selectedIdx].name == 'Hand' then
                    equipWeapon(wid)
                end
                lastPreviewIdx = selectedIdx
                lastPreviewTick = tick
            end
        end

        local dl = imgui.GetForegroundDrawList()

        -- background dim (fade)
        dl:AddRectFilled(imgui.ImVec2(0, 0), imgui.ImVec2(sw, sh), withAlpha(0x8A000000, wheelAlpha))

        -- wheel core (fade)
        dl:AddCircleFilled(imgui.ImVec2(cx, cy), outerR + 3, withAlpha(0x4A000000, wheelAlpha), 64)
        dl:AddCircleFilled(imgui.ImVec2(cx, cy), innerR, withAlpha(0xAA000000, wheelAlpha), 64)
        dl:AddCircle(imgui.ImVec2(cx, cy), outerR, withAlpha(0x90FFFFFF, wheelAlpha), 64, 2)

        local accent = cfg.main.accent_color or 0xFFE3A14F

        -- State colors
        local colIdle = 0x22FFFFFF
        local colPick = accent
        local colLine = 0x35FFFFFF
        local colLinePick = 0xD0FFFFFF
        local colLockedFill = 0x22000000
        local colLockedLine = 0x20000000

        local step = (math.pi * 2) / n
        for i = 1, n do
            local a0 = (i - 1) * step - (math.pi / 2)
            local a1 = i * step - (math.pi / 2)

            local slot = SLOTS[i]
            local wid = slot and pickBestWeapon(slot.weapons) or 0
            local got = slot and ((wid ~= 0) or (slot.name == 'Hand')) or false
            local locked = not got
            local isSel = (i == selectedIdx)

            local pop = (isSel and (cfg.main.selected_pop or 10) or 0) * scale
            local r1 = outerR + pop

            local fill = locked and colLockedFill or (isSel and colPick or colIdle)
            local line = locked and colLockedLine or (isSel and colLinePick or colLine)
            local lw = (isSel and 2.5 or 2.0) * scale

            drawSegmentFancy(dl, cx, cy, r0, r1, a0, a1, fill, line, lw, isSel, locked, wheelAlpha, scale)
            -- Weapon icon inside segment (icons-only look)
            local mid = (a0 + a1) / 2
            local tx = cx + math.cos(mid) * (r0 + (outerR - r0) * 0.58 + pop * 0.15)
            local ty = cy + math.sin(mid) * (r0 + (outerR - r0) * 0.58 + pop * 0.15)

            local label = slot and slot.name or ('Slot '..i)

            local tcol = locked and 0x80FFFFFF or 0xFFFFFFFF
            tcol = withAlpha(tcol, wheelAlpha)

            local iconDrawn = false
            if cfg.main.use_weapon_icons and weaponTexReady and not locked then
                -- Use the "best weapon you own" for that slot to choose icon
                local iconId = wid
                if iconId ~= nil and weaponTex[iconId] ~= nil then
                    
local ringThick = math.max(16.0, (outerR - r0))
                    local baseMul = cfg.main.base_icon_boost or 1.63
                    local baseSize = ringThick * (cfg.main.segment_icon_scale or 10.0) * baseMul

                    local targetMul = (isSel and (cfg.main.hover_icon_scale or 1.8) or 1.0)
                    local targetSize = baseSize * targetMul

                    -- Smooth icon size interpolation per-slot
                    local cur = iconScale[i] or baseSize
                    if cfg.main.smooth_icon_scale then
                        local spd = cfg.main.icon_scale_speed or 18.0
                        cur = cur + (targetSize - cur) * math.min(1.0, dt * spd)
                    else
                        cur = targetSize
                    end
                    iconScale[i] = cur
                    local iconSize = cur

                    -- Rotation (slight tilt toward cursor / direction)
                    local rot = 0.0
                    if cfg.main.rotate_icons then
                        local rs = cfg.main.rotate_strength or 0.35
                        if rs < 0 then rs = 0 elseif rs > 1 then rs = 1 end
                        -- angle from center to segment midpoint, plus a small bias toward cursor angle
                        local angMid = mid
                        local angCursor = math.atan2(mx - cx, -(my - cy))
                        rot = (angCursor - angMid) * 0.25 * rs
                    end

                    local alphaMul = wheelAlpha
                    if cfg.main.dim_unselected_icons and not isSel then
                        alphaMul = alphaMul * (cfg.main.dim_alpha or 0.45)
                    end

                    local center = imgui.ImVec2(tx, ty)
                    local half = iconSize / 2
                    local p0 = imgui.ImVec2(tx - half, ty - half)
                    local p1 = imgui.ImVec2(tx + half, ty - half)
                    local p2 = imgui.ImVec2(tx + half, ty + half)
                    local p3 = imgui.ImVec2(tx - half, ty + half)

                    local function rotPt(p)
                        local s = math.sin(rot)
                        local c = math.cos(rot)
                        local x = p.x - center.x
                        local y = p.y - center.y
                        return imgui.ImVec2(center.x + x*c - y*s, center.y + x*s + y*c)
                    end

                    -- Draw rotated quad (AddImageQuad) if available, else fallback
                    if dl.AddImageQuad then
                        dl:AddImageQuad(weaponTex[iconId], rotPt(p0), rotPt(p1), rotPt(p2), rotPt(p3),
                            imgui.ImVec2(0,0), imgui.ImVec2(1,0), imgui.ImVec2(1,1), imgui.ImVec2(0,1),
                            withAlpha(0xFFFFFFFF, alphaMul))
                    else
                        local A = imgui.ImVec2(tx - half, ty - half)
                        local B = imgui.ImVec2(tx + half, ty + half)
                        dl:AddImage(weaponTex[iconId], A, B, imgui.ImVec2(0,0), imgui.ImVec2(1,1), withAlpha(0xFFFFFFFF, alphaMul))
                    end

iconDrawn = true
                end
            end

            if not iconDrawn then
                -- Fallback to short text icon or LOCKED
                local text = locked and 'LOCKED' or (ICONS[i] or label)
                local ts = imgui.CalcTextSize(text)
                dl:AddText(imgui.ImVec2(tx - ts.x/2 + 1, ty - ts.y/2 + 1), withAlpha(0x90000000, wheelAlpha), text)
                dl:AddText(imgui.ImVec2(tx - ts.x/2,     ty - ts.y/2),     tcol, text)
            end

            -- Optional segment labels (slot name under icon)
            if cfg.main.show_segment_labels then
                local lsz = imgui.CalcTextSize(label)
                dl:AddText(imgui.ImVec2(tx - lsz.x/2 + 1, ty + (outerR - r0) * 0.25 + 1), withAlpha(0x90000000, wheelAlpha), label)
                dl:AddText(imgui.ImVec2(tx - lsz.x/2,     ty + (outerR - r0) * 0.25),     tcol, label)
            end

        end

        -- center info (weapon name + ID)
        local centerState = nil
        local centerTitle = ''
        local ammoStr = nil

        if selectedIdx > 0 and SLOTS[selectedIdx] then
            local w = SLOTS[selectedIdx]
            local wid = pickBestWeapon(w.weapons)
            if wid ~= 0 or w.name == 'Hand' then
                centerTitle = string.format("%s  [ID: %d]", weaponDisplayName(wid), wid)
                ammoStr = (cfg.main.show_ammo_in_center ~= false) and formatAmmo(wid) or nil
            else
                centerState = 'LOCKED'
            end
        else
            centerState = 'Move mouse to select'
        end

        if centerState then
            local s = centerState
            local ts = imgui.CalcTextSize(s)
            dl:AddText(imgui.ImVec2(cx - ts.x/2 + 1, cy - ts.y/2 + 1), withAlpha(0xA0000000, wheelAlpha), s)
            dl:AddText(imgui.ImVec2(cx - ts.x/2,     cy - ts.y/2),     withAlpha(0xE0FFFFFF, wheelAlpha), s)
        else
            -- Title at SAME vertical position as LOCKED (centered)
            local t = centerTitle
            local tsz = imgui.CalcTextSize(t)
            dl:AddText(imgui.ImVec2(cx - tsz.x/2 + 1, cy - tsz.y/2 + 1), withAlpha(0xA0000000, wheelAlpha), t)
            dl:AddText(imgui.ImVec2(cx - tsz.x/2,     cy - tsz.y/2),     withAlpha(0xE0FFFFFF, wheelAlpha), t)

            -- Ammo slightly below (still centered)
            if ammoStr ~= nil and ammoStr ~= '' then
                local asz = imgui.CalcTextSize(ammoStr)
                dl:AddText(imgui.ImVec2(cx - asz.x/2 + 1, cy + tsz.y/2 + 6 * scale + 1), withAlpha(0xA0000000, wheelAlpha), ammoStr)
                dl:AddText(imgui.ImVec2(cx - asz.x/2,     cy + tsz.y/2 + 6 * scale),     withAlpha(0xFFFFFFFF, wheelAlpha), ammoStr)
            end
        end

        -- hint
        local hint
        if cfg.main.preview_on_hover then
            hint = cfg.main.activate_on_release and '' or ''
        else
            hint = touchMode and 'Tap a weapon - tap outside to close' or (cfg.main.activate_on_release and 'Release key to equip' or 'Left click to equip')
        end
        if hint ~= nil and hint ~= '' then local hs = imgui.CalcTextSize(hint) dl:AddText(imgui.ImVec2(cx - hs.x/2, cy + innerR + 18 * scale), withAlpha(0xC0FFFFFF, wheelAlpha), hint) end

        -- MP touch mode: equip on finger release (the finger position is right then), outside the ring closes
        if touchMode and wheelOpen[0] and (os.clock() - touchOpenedAt) > 0.3 then
            if imgui.IsMouseReleased(0) then
                if selectedIdx > 0 and SLOTS[selectedIdx] then
                    local wid = pickBestWeapon(SLOTS[selectedIdx].weapons)
                    if wid ~= 0 or SLOTS[selectedIdx].name == 'Hand' then equipWeapon(wid) end
                end
                closeWheel()
                setCursor(false)
            end
        end
        -- click-to-equip mode
        if not cfg.main.activate_on_release and not touchMode then
            if imgui.IsMouseClicked(0) and selectedIdx > 0 and SLOTS[selectedIdx] then
                local wid = pickBestWeapon(SLOTS[selectedIdx].weapons)
                if wid ~= 0 or SLOTS[selectedIdx].name == 'Hand' then
                    equipWeapon(wid)
                end
                wheelOpen[0] = false
                setCursor(false)
            end
            if imgui.IsMouseClicked(1) then
                wheelOpen[0] = false
                setCursor(false)
            end
        end
    end
)

-- =========================
-- MP: WX button + settings window (plain imgui)
-- =========================
imgui.OnFrame(
    function()
        return cfg.main.enabled and cfg.main.touch_button and not wheelOpen[0] and not isUiBlocked()
            and isCharOnFoot(PLAYER_PED) and not (isPauseMenuActive and isPauseMenuActive())
    end,
    function(self)
        self.HideCursor = true
        local sw, sh = getScreenResolution()
        local sz = cfg.main.button_size or 64
        local bx, by = cfg.main.button_x or -1, cfg.main.button_y or -1
        if bx < 0 then bx = sw - sz - 12 end
        if by < 0 then by = sh * 0.42 end
        imgui.SetNextWindowPos(imgui.ImVec2(bx, by), imgui.Cond.Once)
        imgui.SetNextWindowBgAlpha(0.35)
        local fl = imgui.WindowFlags.NoTitleBar + imgui.WindowFlags.NoResize + imgui.WindowFlags.NoScrollbar
            + imgui.WindowFlags.AlwaysAutoResize + imgui.WindowFlags.NoSavedSettings + imgui.WindowFlags.NoFocusOnAppearing
        if imgui.Begin('##weaponx_btn', nil, fl) then
            if imgui.Button('WX', imgui.ImVec2(sz, sz)) then openTouch() end
            local p = imgui.GetWindowPos()
            if math.abs(p.x - bx) > 2 or math.abs(p.y - by) > 2 then
                cfg.main.button_x, cfg.main.button_y = math.floor(p.x), math.floor(p.y)
                inicfg.save(cfg, cfg_path)
            end
        end
        imgui.End()
    end
)

local S = {}
imgui.OnFrame(
    function() return settingsOpen[0] end,
    function(self)
        self.HideCursor = false
        local sw, sh = getScreenResolution()
        imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.FirstUseEver, imgui.ImVec2(0.5, 0.5))
        imgui.SetNextWindowSize(imgui.ImVec2(520, 0), imgui.Cond.FirstUseEver)
        if imgui.Begin('WeaponX settings', settingsOpen) then
            local changed = false
            for _, k in ipairs({ 'enabled', 'touch_button', 'preview_on_hover', 'hover_snap_sound', 'use_weapon_icons',
                    'show_ammo_in_center', 'show_clip_ammo', 'enable_glow', 'enable_gradient', 'combo_shortcuts' }) do
                S[k] = S[k] or imgui.new.bool(false)
                S[k][0] = cfg.main[k] and true or false
                if imgui.Checkbox(k, S[k]) then cfg.main[k] = S[k][0]; changed = true end
            end
            for _, d in ipairs({ { 'radius', 120, 420 }, { 'inner_radius', 30, 200 }, { 'ring_thickness', 20, 120 }, { 'button_size', 40, 140 } }) do
                local k = d[1]
                S[k] = S[k] or imgui.new.int(0)
                S[k][0] = math.floor(tonumber(cfg.main[k]) or d[2])
                if imgui.SliderInt(k, S[k], d[2], d[3]) then cfg.main[k] = S[k][0]; changed = true end
            end
            if imgui.Button('Open wheel now') then settingsOpen[0] = false; openTouch() end
            imgui.SameLine()
            if imgui.Button('Reset button position') then cfg.main.button_x, cfg.main.button_y = -1, -1; changed = true end
            imgui.TextDisabled('/weaponx  open wheel   /weaponx cfg  this window   /wheel  on / off')
            if changed then inicfg.save(cfg, cfg_path) end
        end
        imgui.End()
    end
)

-- =========================
-- Main
-- =========================
function main()
    -- MP: MonetLoader in MP is not SA-MP; wait a few seconds at most
    local t0 = os.clock()
    while not (isSampAvailable and isSampAvailable()) and os.clock() - t0 < 5 do wait(100) end

    -- Snap sound: audiostream loader (no external wav needed)
    if cfg.main.hover_snap_sound and tostring(cfg.main.snap_mode or 'audiostream') == 'audiostream' then
        local p = tostring(cfg.main.snap_audio_path or 'moonloader\\\\resource\\\\audio\\\\GameSounds\\\\notification.mp3')
        -- accept either absolute (moonloader\...) or relative
        if doesFileExist and doesFileExist(p) then
            AudioStreamClick = loadAudioStream(p)
        else
            -- try relative to working directory
            local alt = getWorkingDirectory() .. '\\\\' .. p
            local own = getWorkingDirectory() .. '/resource/audio/GameSounds/notification.mp3'
            if doesFileExist and doesFileExist(alt) then
                AudioStreamClick = loadAudioStream(alt)
            elseif doesFileExist and doesFileExist(own) then
                AudioStreamClick = loadAudioStream(own)
            end
        end
    end


    sampRegisterChatCommand('wheel', function()
        cfg.main.enabled = not cfg.main.enabled
        inicfg.save(cfg, cfg_path)
        sampAddChatMessage(('{B6E3FF}WeaponWheel:{FFFFFF} %s'):format(cfg.main.enabled and 'enabled' or 'disabled'), 0xFFFFFF)
    end)

    sampRegisterChatCommand('wheelcfg', function()
        settingsOpen[0] = not settingsOpen[0]
    end)

    -- MP: /weaponx = open the wheel (touch), /weaponx cfg = settings, /weaponx on|off
    sampRegisterChatCommand('weaponx', function(arg)
        arg = tostring(arg or ''):lower():gsub('^%s+', ''):gsub('%s+$', '')
        if arg == 'cfg' or arg == 'settings' or arg == 'menu' then
            settingsOpen[0] = not settingsOpen[0]
        elseif arg == 'off' or arg == 'on' then
            cfg.main.enabled = (arg == 'on')
            if not cfg.main.enabled then closeWheel() end
            inicfg.save(cfg, cfg_path)
            sampAddChatMessage('{B6E3FF}WeaponX:{FFFFFF} ' .. (cfg.main.enabled and 'enabled' or 'disabled'), 0xFFFFFF)
        else
            if wheelOpen[0] then closeWheel() else openTouch() end
        end
    end)

    sampAddChatMessage('{B6E3FF}WeaponX:{FFFFFF} tap WX or type /weaponx to open the wheel (/weaponx cfg settings, /wheel on/off). Keyboard: hold X.', 0xFFFFFF)

    while true do
        wait(0)

        if not cfg.main.enabled then
            if wheelOpen[0] then
                wheelOpen[0] = false
                setCursor(false)
            end
        else
            local hold = keyDown(cfg.main.hold_key)

            -- Combo shortcuts (hold X + key)
            if hold and cfg.main.combo_shortcuts and (not isUiBlocked()) then
                for vk, slotIdx in pairs(COMBO_KEYS) do
                    if slotIdx >= 1 and slotIdx <= #SLOTS and comboPressed(vk) then
                        -- open wheel if not already open
                        if not wheelOpen[0] then
                            wheelOpen[0] = true
                            setCursor(true)
                        end
                        selectedIdx = slotIdx
                        lastValidIdx = slotIdx
                        -- optionally preview immediately
                        if cfg.main.preview_on_hover and SLOTS[slotIdx] then
                            local wid = pickBestWeapon(SLOTS[slotIdx].weapons)
                            if wid ~= 0 or SLOTS[slotIdx].name == 'Hand' then equipWeapon(wid) end
                        end
                        -- instant equip mode
                        if cfg.main.combo_instant_equip and SLOTS[slotIdx] then
                            local wid = pickBestWeapon(SLOTS[slotIdx].weapons)
                            if wid ~= 0 or SLOTS[slotIdx].name == 'Hand' then equipWeapon(wid) end
                            wheelOpen[0] = false
                            setCursor(false)
                        end
                        break
                    end
                end
            end

            if hold and not lastHold then
                if not isUiBlocked() then
                    wheelOpen[0] = true
                    setCursor(true)
                end
            end

            if not hold and lastHold and not touchMode then
                if wheelOpen[0] and cfg.main.activate_on_release then
                    local useIdx = (lastValidIdx > 0 and lastValidIdx) or selectedIdx
                    if useIdx > 0 and SLOTS[useIdx] then
                        local wid = pickBestWeapon(SLOTS[useIdx].weapons)
                        if wid ~= 0 or SLOTS[useIdx].name == 'Hand' then
                            equipWeapon(wid)
                        end
                    end
                    wheelOpen[0] = false
                    setCursor(false)
                elseif wheelOpen[0] then
                    -- click mode: just close on release
                    wheelOpen[0] = false
                    setCursor(false)
                end
            end

            -- safety: close if UI becomes blocked
            if wheelOpen[0] and isUiBlocked() then
                touchMode = false
                wheelOpen[0] = false
                setCursor(false)
            end

            lastHold = hold
        end
    end
end
