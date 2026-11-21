-- Advanced Tactical Compass for MP (Android MonetLoader) 1.0-mp
-- A new Lua version of "Advanced Tactical Compass" by zConnor.i22 (MW3-style compass,
-- https://libertycity.net/files/gta-san-andreas/239690-advanced-tactical-compass.html). The original is compiled PC
-- LuaJIT bytecode that uses PC-only keys (ALT+J / ALT+H), so this file re-creates its features from scratch:
-- modes Mini / Mini border / Full / Full no border / hidden, top or bottom, waypoint direction and distance,
-- edge fade, 1080p-based scaling and a saved config. Drawn with mimgui; settings in a plain imgui window.
--   /compass         settings window
--   /compass mode    next mode            /compass top|bottom    move it
--   /compass hide    hide / show

script_name('AdvancedTacticalCompass')
script_author('zConnor.i22 (idea, original PC mod); MP port')
script_version('1.0-mp')

local imgui = require 'mimgui'
local inicfg = require 'inicfg'

local CFG = 'AdvancedTacticalCompass.ini'
local cfg = inicfg.load({
    main = {
        mode = 1,          -- 1 Mini, 2 Mini border, 3 Full, 4 Full no border, 5 hidden
        bottom = false,    -- false = top of the screen
        scale = 1.0,
        offsetY = 18,      -- px at 1080p from the edge
        fade = true,
        waypoint = true,
        degrees = true,    -- numbers between the letters
        alpha = 230,
    },
}, CFG)
inicfg.save(cfg, CFG)

local MODES = { 'Mini', 'Mini border', 'Full', 'Full no border', 'Hidden' }
local function save() inicfg.save(cfg, CFG) end

local LETTERS = { [0] = 'N', [45] = 'NE', [90] = 'E', [135] = 'SE', [180] = 'S', [225] = 'SW', [270] = 'W', [315] = 'NW' }

local function col(r, g, b, a)
    a = math.max(0, math.min(255, math.floor(a)))
    return a * 16777216 + b * 65536 + g * 256 + r   -- ImU32 is ABGR
end

local function norm(d)
    d = d % 360
    if d < 0 then d = d + 360 end
    return d
end

local function diff(a, b)   -- signed a - b in -180..180
    local d = (a - b + 540) % 360 - 180
    return d
end

-- 0 = north (+Y), 90 = east (+X)
local function bearing(dx, dy)
    return norm(math.deg(math.atan2(dx, dy)))
end

local function cameraHeading()
    local cx, cy = getActiveCameraCoordinates()
    local px, py = getActiveCameraPointAt()
    if not cx or not px then return 0 end
    local dx, dy = px - cx, py - cy
    if math.abs(dx) + math.abs(dy) < 1e-4 then
        return norm(-getCharHeading(PLAYER_PED))
    end
    return bearing(dx, dy)
end

local function waypoint()
    local ok, x, y, z = getTargetBlipCoordinates()
    if ok and x then return x, y, z end
    return nil
end

local function drawCompass()
    local mode = tonumber(cfg.main.mode) or 1
    if mode >= 5 then return end
    if not doesCharExist(PLAYER_PED) then return end
    if isPauseMenuActive and isPauseMenuActive() then return end
    local sw, sh = getScreenResolution()
    local K = (sh / 1080) * (tonumber(cfg.main.scale) or 1)
    local full = (mode == 3 or mode == 4)
    local border = (mode == 2 or mode == 3)
    local W = (full and 900 or 520) * K
    local H = 34 * K
    local span = full and 180 or 110        -- degrees shown across the strip
    local cx = sw / 2
    local y0 = cfg.main.bottom and (sh - (tonumber(cfg.main.offsetY) or 18) * K - H) or ((tonumber(cfg.main.offsetY) or 18) * K)
    local A = tonumber(cfg.main.alpha) or 230
    local dl = imgui.GetBackgroundDrawList()
    local heading = cameraHeading()
    local fadeOn = cfg.main.fade

    local function fade(x)
        if not fadeOn then return 1 end
        local t = math.abs(x - cx) / (W / 2)
        return math.max(0, 1 - t * t * t)
    end

    if border then
        dl:AddRectFilled(imgui.ImVec2(cx - W / 2, y0), imgui.ImVec2(cx + W / 2, y0 + H), col(0, 0, 0, A * 0.35), 4 * K)
        dl:AddRect(imgui.ImVec2(cx - W / 2, y0), imgui.ImVec2(cx + W / 2, y0 + H), col(255, 255, 255, A * 0.45), 4 * K, nil, 1.2 * K)
    end

    local fsBig = 20 * K
    local fsSmall = 13 * K
    local font = imgui.GetFont()
    local start = math.floor((heading - span / 2) / 5) * 5
    for d = start, heading + span / 2 + 5, 5 do
        local off = diff(norm(d), heading)
        local x = cx + off / span * W
        if math.abs(x - cx) <= W / 2 then
            local f = fade(x)
            local dd = norm(d)
            local letter = LETTERS[dd]
            local a = A * f
            if letter then
                local c = (dd == 0) and col(255, 90, 70, a) or col(255, 255, 255, a)
                local ts = font:CalcTextSizeA(fsBig, 1000, 0, letter)
                dl:AddTextFontPtr(font, fsBig, imgui.ImVec2(x - ts.x / 2, y0 + H * 0.5 - ts.y / 2), c, letter)
                dl:AddLine(imgui.ImVec2(x, y0 + 1), imgui.ImVec2(x, y0 + H * 0.18), c, 2 * K)
            elseif dd % 15 == 0 then
                if cfg.main.degrees then
                    local s = tostring(math.floor(dd))
                    local ts = font:CalcTextSizeA(fsSmall, 1000, 0, s)
                    dl:AddTextFontPtr(font, fsSmall, imgui.ImVec2(x - ts.x / 2, y0 + H * 0.5 - ts.y / 2), col(220, 220, 220, a * 0.85), s)
                end
                dl:AddLine(imgui.ImVec2(x, y0 + 1), imgui.ImVec2(x, y0 + H * 0.22), col(255, 255, 255, a * 0.8), 1.5 * K)
            else
                dl:AddLine(imgui.ImVec2(x, y0 + H * 0.78), imgui.ImVec2(x, y0 + H - 1), col(255, 255, 255, a * 0.55), 1 * K)
            end
        end
    end

    -- centre pointer + current heading
    local pc = col(255, 210, 60, A)
    local ty = cfg.main.bottom and y0 - 2 * K or y0 + H + 2 * K
    local dir = cfg.main.bottom and 1 or -1
    dl:AddTriangleFilled(imgui.ImVec2(cx - 7 * K, ty - dir * 0), imgui.ImVec2(cx + 7 * K, ty), imgui.ImVec2(cx, ty + dir * 9 * K), pc)
    local hs = string.format('%03d', math.floor(heading + 0.5) % 360)
    local hts = font:CalcTextSizeA(fsSmall, 1000, 0, hs)
    local hy = cfg.main.bottom and (y0 - 14 * K - hts.y) or (y0 + H + 12 * K)
    dl:AddTextFontPtr(font, fsSmall, imgui.ImVec2(cx - hts.x / 2, hy), col(255, 255, 255, A), hs)

    -- waypoint marker
    if cfg.main.waypoint then
        local wx, wy = waypoint()
        if wx then
            local px, py = getCharCoordinates(PLAYER_PED)
            local b = bearing(wx - px, wy - py)
            local off = diff(b, heading)
            local x = cx + math.max(-1, math.min(1, off / span * 2)) * (W / 2)
            local clamped = math.abs(off) > span / 2
            local wc = col(255, 70, 200, A * (clamped and 0.7 or 1))
            local my = cfg.main.bottom and y0 or y0 + H
            dl:AddCircleFilled(imgui.ImVec2(x, my), 6 * K, wc, 12)
            local dist = math.sqrt((wx - px) ^ 2 + (wy - py) ^ 2)
            local ds = dist >= 1000 and string.format('%.1fkm', dist / 1000) or string.format('%dm', math.floor(dist))
            local dts = font:CalcTextSizeA(fsSmall, 1000, 0, ds)
            local dy = cfg.main.bottom and (y0 - 8 * K - dts.y) or (y0 + H + 8 * K)
            if math.abs(x - cx) < 30 * K then dy = cfg.main.bottom and (dy - hts.y - 4 * K) or (dy + hts.y + 4 * K) end
            dl:AddTextFontPtr(font, fsSmall, imgui.ImVec2(x - dts.x / 2, dy), wc, ds)
        end
    end
end

imgui.OnFrame(function() return true end, function(self)
    self.HideCursor = true
    pcall(drawCompass)
end)

-- settings window (plain imgui)
local win = imgui.new.bool(false)
local V = {}
imgui.OnFrame(function() return win[0] end, function(self)
    self.HideCursor = false
    local sw, sh = getScreenResolution()
    imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.FirstUseEver, imgui.ImVec2(0.5, 0.5))
    imgui.SetNextWindowSize(imgui.ImVec2(460, 0), imgui.Cond.FirstUseEver)
    if imgui.Begin('Tactical Compass', win) then
        local ch = false
        for i, n in ipairs(MODES) do
            if imgui.RadioButton(n, (tonumber(cfg.main.mode) or 1) == i) then cfg.main.mode = i; ch = true end
            if i < #MODES then imgui.SameLine() end
        end
        for _, k in ipairs({ { 'bottom', 'At the bottom' }, { 'fade', 'Edge fade' }, { 'waypoint', 'Waypoint marker' }, { 'degrees', 'Degree numbers' } }) do
            V[k[1]] = V[k[1]] or imgui.new.bool(false)
            V[k[1]][0] = cfg.main[k[1]] and true or false
            if imgui.Checkbox(k[2], V[k[1]]) then cfg.main[k[1]] = V[k[1]][0]; ch = true end
        end
        V.scale = V.scale or imgui.new.float(1)
        V.scale[0] = tonumber(cfg.main.scale) or 1
        if imgui.SliderFloat('Scale', V.scale, 0.5, 2.0, '%.2f') then cfg.main.scale = V.scale[0]; ch = true end
        V.off = V.off or imgui.new.int(18)
        V.off[0] = tonumber(cfg.main.offsetY) or 18
        if imgui.SliderInt('Distance from edge', V.off, 0, 300) then cfg.main.offsetY = V.off[0]; ch = true end
        V.alpha = V.alpha or imgui.new.int(230)
        V.alpha[0] = tonumber(cfg.main.alpha) or 230
        if imgui.SliderInt('Opacity', V.alpha, 40, 255) then cfg.main.alpha = V.alpha[0]; ch = true end
        imgui.TextDisabled('Original idea and PC mod: zConnor.i22')
        if ch then save() end
    end
    imgui.End()
end)

function main()
    while not isPlayerPlaying(PLAYER_HANDLE) do wait(200) end
    sampRegisterChatCommand('compass', function(arg)
        arg = tostring(arg or ''):lower():gsub('%s', '')
        if arg == 'mode' then
            cfg.main.mode = (tonumber(cfg.main.mode) or 1) % 4 + 1; save()
            printStringNow('Compass: ' .. MODES[cfg.main.mode], 1500)
        elseif arg == 'top' or arg == 'bottom' then
            cfg.main.bottom = (arg == 'bottom'); save()
        elseif arg == 'hide' then
            if (tonumber(cfg.main.mode) or 1) == 5 then cfg.main.mode = 1 else cfg.main.mode = 5 end
            save()
        else
            win[0] = not win[0]
        end
    end)
    wait(-1)
end
