-- fs_graphics 1.1 (MP 5.02.39): FS-Graphics for GTA SA on Android.
--
-- FS-Graphics (https://github.com/JebetrexxMC/fs-graphics, by IDK_FORCE) is a FiveM / GTA V graphics pack: timecycle
-- XML files, water, clouds and a ReShade preset. GTA SA can't load GTA V files, so this module takes the colours from
-- its timecycles (lib/fs_graphics_data.lua, generated from w_*.xml) and blends them into SA's own timecycle tables
-- (CTimeCycle::m_nSkyTopRed ... in libGTASA.so, 8 hours x 23 weathers). It is an approximation of the look, not the
-- pack itself. Every table is backed up first and written back by restore().
--
--   local fsg = require 'fs_graphics'
--   fsg.apply({ sky = true, clouds = true, ... }, strength)   fsg.restore()   fsg.status()   fsg.hideClouds(true)

local ffi = require 'ffi'
local okD, DATA = pcall(require, 'fs_graphics_data')
if not okD then DATA = nil end

local M = {}
local NH, NW = 8, 23
local SIZE = NH * NW

-- SA weather id -> FS-Graphics (GTA V) weather. 19 sandstorm, 20 underwater and 21/22 extra colours stay as they are.
local WMAP = {
    [0] = 'extrasunny', [2] = 'extrasunny', [6] = 'extrasunny', [11] = 'extrasunny', [13] = 'extrasunny', [17] = 'extrasunny',
    [1] = 'clear', [3] = 'clear', [5] = 'clear', [10] = 'clear', [14] = 'clear', [18] = 'clear',
    [4] = 'clouds', [7] = 'clouds', [12] = 'clouds', [15] = 'clouds',
    [8] = 'rain', [16] = 'thunder',
    [9] = 'foggy',
}
M.WMAP = WMAP

local function sym(name)
    if not getModuleProcAddress then return nil end
    local ok, a = pcall(getModuleProcAddress, 'libGTASA.so', '_ZN10CTimeCycle' .. #name .. name .. 'E')
    if ok and a and a ~= 0 then return a end
    return nil
end

-- groups of tables that must sit next to each other (that check tells us they are uint8[8][23])
local GROUPS = {
    skyTop       = { 'm_nSkyTopRed', 'm_nSkyTopGreen', 'm_nSkyTopBlue' },
    skyBot       = { 'm_nSkyBottomRed', 'm_nSkyBottomGreen', 'm_nSkyBottomBlue' },
    sunCore      = { 'm_nSunCoreRed', 'm_nSunCoreGreen', 'm_nSunCoreBlue' },
    sunCorona    = { 'm_nSunCoronaRed', 'm_nSunCoronaGreen', 'm_nSunCoronaBlue' },
    lowClouds    = { 'm_nLowCloudsRed', 'm_nLowCloudsGreen', 'm_nLowCloudsBlue' },
    fluffyBottom = { 'm_nFluffyCloudsBottomRed', 'm_nFluffyCloudsBottomGreen', 'm_nFluffyCloudsBottomBlue' },
    ambient      = { 'm_nAmbientRed', 'm_nAmbientGreen', 'm_nAmbientBlue' },
    ambientObj   = { 'm_nAmbientRed_Obj', 'm_nAmbientGreen_Obj', 'm_nAmbientBlue_Obj' },
    water        = { 'm_fWaterRed', 'm_fWaterGreen', 'm_fWaterBlue', 'm_fWaterAlpha' },
    postFx1      = { 'm_fPostFx1Red', 'm_fPostFx1Green', 'm_fPostFx1Blue', 'm_fPostFx1Alpha' },
    postFx2      = { 'm_fPostFx2Red', 'm_fPostFx2Green', 'm_fPostFx2Blue', 'm_fPostFx2Alpha' },
    sprite       = { 'm_fSpriteSize', 'm_fSpriteBrightness' },
    cloudAlpha   = { 'm_fCloudAlpha', 'm_nHighLightMinIntensity' },
}

local T = nil        -- found tables: T[group] = { addr1, addr2, ... }
local farClip = nil  -- int16[8][23]
local backup = {}    -- backup[addr] = { bytes }
local applied = false
local info = {}

local function scan()
    if T then return end
    T = {}
    for g, names in pairs(GROUPS) do
        local addrs, ok = {}, true
        for i, n in ipairs(names) do
            addrs[i] = sym(n)
            if not addrs[i] then ok = false; break end
        end
        if ok then
            for i = 2, #addrs do
                local d = math.abs(addrs[i] - addrs[i - 1])
                if d == 0 or d % SIZE ~= 0 or d > SIZE * 64 then ok = false end
            end
        end
        if ok then T[g] = addrs end
        info[#info + 1] = g .. (ok and ' ok' or ' missing')
    end
    local fc, fs = sym('m_fFarClip'), sym('m_fFogStart')
    if fc and fs and math.abs(fs - fc) == SIZE * 2 then farClip = fc end
    info[#info + 1] = 'farClip' .. (farClip and ' ok' or ' missing')
    table.sort(info)
end

local function u8(addr) return ffi.cast('uint8_t*', addr) end

local function save(addr, n)
    if backup[addr] then return end
    local p, b = u8(addr), {}
    for i = 0, n - 1 do b[i] = p[i] end
    backup[addr] = b
end

local function orig(addr, i) return backup[addr][i] end

local function lerp(a, b, t) return a + (b - a) * t end
local function clamp8(v) v = math.floor(v + 0.5); if v < 0 then return 0 elseif v > 255 then return 255 end return v end

-- blend a colour group towards FS-Graphics data
local function blendData(g, field, s, weathers)
    local addrs = T[g]
    if not addrs or not DATA then return 0 end
    local n = 0
    for c = 1, 3 do save(addrs[c], SIZE) end
    for w, vw in pairs(WMAP) do
        if not weathers or weathers[w] then
            local src = DATA[vw] and DATA[vw][field]
            if src then
                for h = 0, NH - 1 do
                    local col = src[h + 1]
                    local idx = h * NW + w
                    for c = 1, 3 do
                        u8(addrs[c])[idx] = clamp8(lerp(orig(addrs[c], idx), col[c], s))
                    end
                    n = n + 1
                end
            end
        end
    end
    return n
end

local function scaleGroup(g, comps, fn)
    local addrs = T[g]
    if not addrs then return end
    for _, c in ipairs(comps) do
        save(addrs[c], SIZE)
        local p = u8(addrs[c])
        for h = 0, NH - 1 do
            for w = 0, NW - 1 do
                local idx = h * NW + w
                p[idx] = clamp8(fn(orig(addrs[c], idx), h, w, c))
            end
        end
    end
end

local function restoreAll()
    for addr, b in pairs(backup) do
        local n = 0
        for _ in pairs(b) do n = n + 1 end
        local p = u8(addr)
        for i = 0, n - 1 do p[i] = b[i] end
    end
end

local LightsMultDefault = nil
local function setLightsMult(v)
    if not (readMemory and writeMemory) then return end
    pcall(function()
        if not LightsMultDefault then
            local cur = representIntAsFloat and representIntAsFloat(readMemory(0x8D4B5C, 4, false)) or nil
            if cur and cur == cur and cur > 0.05 and cur < 10 then LightsMultDefault = cur else LightsMultDefault = 1.0 end
        end
        if v and representFloatAsInt then writeMemory(0x8D4B5C, 4, representFloatAsInt(v), false) end
    end)
end

-- opts: sky clouds water nights colors reflections rain streetlights fps (booleans); s: 0..1
function M.apply(opts, s)
    scan()
    s = math.max(0, math.min(1, tonumber(s) or 0.7))
    restoreAll()   -- always start from the game's own values
    local rainW = { [8] = true, [16] = true }
    local dryW = {}
    for w in pairs(WMAP) do if not rainW[w] then dryW[w] = true end end
    local weathers = opts.rain and nil or dryW

    if opts.sky then
        blendData('skyTop', 'skyTop', s, weathers)
        blendData('skyBot', 'skyBot', s, weathers)
        blendData('sunCore', 'sunCore', s, weathers)
        blendData('sunCorona', 'sunCorona', s * 0.8, weathers)
    end
    if opts.clouds then
        blendData('lowClouds', 'lowClouds', s, weathers)
        blendData('fluffyBottom', 'fluffyBottom', s, weathers)
        scaleGroup('cloudAlpha', { 1 }, function(v) return v * (1 + 0.15 * s) end)
    end
    -- sky = false / clouds = false leave the game's own sky / clouds alone on every weather (rain only widens them)
    if opts.water and T.water then
        local wc = DATA and DATA.waterColor or 0x1A00191C
        local tr = math.min(255, math.floor(wc / 65536) % 256 * 4 + 20)
        local tg = math.min(255, math.floor(wc / 256) % 256 * 4 + 20)
        local tb = math.min(255, wc % 256 * 4 + 20)
        local tgt = { tr, tg, tb }
        scaleGroup('water', { 1, 2, 3 }, function(v, h, w, c)
            if w >= 19 then return v end
            return lerp(v, tgt[c], 0.6 * s)
        end)
        scaleGroup('water', { 4 }, function(v) return v * (1 + 0.1 * s) end)
    end
    if opts.nights then
        local night = { [0] = 1, [1] = 0.6, [6] = 0.5, [7] = 1 }   -- 0h 5h 20h 22h
        for _, g in ipairs({ 'ambient', 'ambientObj' }) do
            scaleGroup(g, { 1, 2, 3 }, function(v, h, w, c)
                local k = night[h]
                if not k or w >= 19 then return v end
                local f = 1 - 0.25 * s * k
                if c == 3 then f = f + 0.12 * s * k end
                return v * f
            end)
        end
    end
    if opts.colors then
        for _, g in ipairs({ 'postFx1', 'postFx2' }) do
            scaleGroup(g, { 4 }, function(v, h, w) if w >= 19 then return v end return v * (1 - 0.45 * s) end)
        end
    end
    if opts.reflections then
        scaleGroup('sprite', { 2 }, function(v) return v * (1 + 0.35 * s) end)
    end
    if opts.fps and farClip then
        save(farClip, SIZE * 2)
        local p = ffi.cast('int16_t*', farClip)
        local b = backup[farClip]
        for i = 0, SIZE - 1 do
            local o = ffi.cast('int16_t*', ffi.new('uint8_t[2]', b[i * 2], b[i * 2 + 1]))[0]
            p[i] = math.max(250, math.floor(o * (1 - 0.35 * s)))
        end
    end
    setLightsMult(opts.streetlights and (LightsMultDefault or 1) * (1 + 0.6 * s) or LightsMultDefault)
    applied = true
end

-- Hide the game's clouds: hooks on CClouds::Render (cloud layer, fluffy clouds - the moon and stars are drawn there
-- too), CClouds::RenderBottomFromHeight and CClouds::VolumetricCloudsRender that skip the original while on.
-- The sky (CClouds::RenderSkyPolys) is not touched. Installed once on first use; off calls the originals again.
local CLOUD_SYMS = { '_ZN7CClouds6RenderEv', '_ZN7CClouds22RenderBottomFromHeightEv', '_ZN7CClouds22VolumetricCloudsRenderEv' }
local cloudHooks, cloudsHidden = nil, false
function M.hideClouds(on)
    cloudsHidden = on and true or false
    if not cloudsHidden or cloudHooks then return cloudHooks and #cloudHooks or 0 end
    cloudHooks = {}
    local okH, hook = pcall(require, 'monethook')
    if not okH or not getModuleProcAddress then print('[fs_graphics] no monethook: game clouds stay'); return 0 end
    for _, name in ipairs(CLOUD_SYMS) do
        local okA, addr = pcall(getModuleProcAddress, 'libGTASA.so', name)
        if okA and addr and addr ~= 0 then
            local orig
            local ok, h = pcall(hook.new, 'void(*)()', function()
                if cloudsHidden then return end
                return orig()
            end, addr)
            if ok and h then orig = h; cloudHooks[#cloudHooks + 1] = h else print('[fs_graphics] hook ' .. name .. ' failed: ' .. tostring(h)) end
        end
    end
    return #cloudHooks
end

function M.restore()
    if not T then return end
    restoreAll()
    setLightsMult(LightsMultDefault)
    applied = false
    cloudsHidden = false
end

function M.status()
    scan()
    local found = 0
    for _ in pairs(T) do found = found + 1 end
    return { applied = applied, groups = found, total = 0 + (function() local n = 0 for _ in pairs(GROUPS) do n = n + 1 end return n end)(),
        farClip = farClip ~= nil, data = DATA ~= nil, info = info,
        cloudHooks = cloudHooks and #cloudHooks or nil, cloudsHidden = cloudsHidden }
end

return M
