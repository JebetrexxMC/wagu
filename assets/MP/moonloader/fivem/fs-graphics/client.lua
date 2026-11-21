-- fs-graphics 1.0-mp - FiveM resource for MP's FiveM loader (moonloader/fivem_loader.lua)
-- FS-Graphics by IDK_FORCE: https://github.com/JebetrexxMC/fs-graphics
-- The pack is GTA V data (timecycle XML, water, clouds, ReShade). On GTA SA this resource blends the colours of its
-- timecycles into SA's own timecycle (lib/fs_graphics.lua + lib/fs_graphics_data.lua). Approximations, listed in
-- the window. /fsgraphics opens it (plain imgui), /fsgraphics on|off, /fsgraphics preset [full|nosky].

local imgui = require 'mimgui'
local inicfg = require 'inicfg'
local fsg = require 'fs_graphics'

local CFG = 'fs_graphics.ini'
local cfg = inicfg.load({
    main = { enabled = true, strength = 0.7, sky = true, clouds = true, water = true, nights = true, colors = true,
        reflections = true, rain = true, streetlights = true, fps = false },
}, CFG)
inicfg.save(cfg, CFG)

local running = true
local win = imgui.new.bool(false)
local V = {}

local FEATURES = {
    { 'water', 'Better Water', 'SA water colour towards the FS-Graphics water tune' },
    { 'rain', 'Better Rain', 'Better Sky / Clouds also on SA rain and thunder weathers' },
    { 'colors', 'Better Colors', 'less grey colour filter (SA post FX alpha)' },
    { 'reflections', 'Better Reflections', 'brighter sun / light sprites (SA has no screen space reflections)' },
    { 'clouds', 'Better Clouds', 'cloud colours from the FS-Graphics timecycle' },
    { 'nights', 'Better Nights', 'darker, slightly blue night ambient light' },
    { 'sky', 'Better Sky', 'sky top / horizon / sun colours from the FS-Graphics timecycle' },
    { 'streetlights', 'Better StreetLights', 'brighter light coronas (CCoronas::LightsMult)' },
    { 'fps', 'FPS Increase', 'shorter draw distance in SA timecycle (off by default)' },
}

-- presets: every feature listed; the rest of the config (enabled, strength) stays
local PRESETS = {
    { 'full', 'Full', 'every feature on (FPS Increase off)',
        { sky = true, clouds = true, water = true, nights = true, colors = true, reflections = true, rain = true, streetlights = true, fps = false } },
    { 'nosky', 'No sky / clouds', 'game sky and clouds, everything else on',
        { sky = false, clouds = false, water = true, nights = true, colors = true, reflections = true, rain = true, streetlights = true, fps = false } },
}

local function findPreset(id)
    id = tostring(id or ''):lower()
    for _, p in ipairs(PRESETS) do if p[1] == id then return p end end
    return nil
end

local function currentPreset()
    for _, p in ipairs(PRESETS) do
        local same = true
        for k, v in pairs(p[4]) do if (cfg.main[k] and true or false) ~= v then same = false end end
        if same then return p end
    end
    return nil
end

local apply
local function setPreset(p)
    for k, v in pairs(p[4]) do cfg.main[k] = v end
    cfg.main.enabled = true
    inicfg.save(cfg, CFG)
    apply()
end

function apply()
    if cfg.main.enabled then
        local ok, e = pcall(fsg.apply, cfg.main, cfg.main.strength)
        if not ok then print('apply failed: ' .. tostring(e)) end
    else
        pcall(fsg.restore)
    end
end

imgui.OnFrame(function() return running and win[0] end, function(self)
    self.HideCursor = false
    local sw, sh = getScreenResolution()
    imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.FirstUseEver, imgui.ImVec2(0.5, 0.5))
    imgui.SetNextWindowSize(imgui.ImVec2(560, 0), imgui.Cond.FirstUseEver)
    if imgui.Begin('FS-Graphics', win) then
        local ch = false
        V.en = V.en or imgui.new.bool(true)
        V.en[0] = cfg.main.enabled and true or false
        if imgui.Checkbox('Enabled', V.en) then cfg.main.enabled = V.en[0]; ch = true end
        V.s = V.s or imgui.new.float(0.7)
        V.s[0] = tonumber(cfg.main.strength) or 0.7
        if imgui.SliderFloat('Strength', V.s, 0.0, 1.0, '%.2f') then cfg.main.strength = V.s[0]; ch = true end
        imgui.Separator()
        local cur = currentPreset()
        imgui.Text('Preset:')
        for _, p in ipairs(PRESETS) do
            imgui.SameLine()
            if imgui.Button(p[2] .. (cur == p and ' *' or '')) then setPreset(p) end
        end
        imgui.SameLine()
        imgui.TextDisabled(cur and cur[3] or 'custom')
        imgui.Separator()
        for _, f in ipairs(FEATURES) do
            V[f[1]] = V[f[1]] or imgui.new.bool(false)
            V[f[1]][0] = cfg.main[f[1]] and true or false
            if imgui.Checkbox(f[2], V[f[1]]) then cfg.main[f[1]] = V[f[1]][0]; ch = true end
            imgui.SameLine(230)
            imgui.TextDisabled(f[3])
        end
        imgui.Checkbox('Better Fire', imgui.new.bool(false))
        imgui.SameLine(230)
        imgui.TextDisabled('not on SA (GTA V particle file explosion.ymt)')
        imgui.Separator()
        local st = fsg.status()
        imgui.TextDisabled(string.format('SA timecycle tables found: %d / %d   draw distance table: %s   %s',
            st.groups, st.total, st.farClip and 'yes' or 'no', st.applied and 'applied' or 'not applied'))
        if imgui.Button('Restore game colours') then cfg.main.enabled = false; ch = true end
        imgui.SameLine()
        imgui.TextDisabled('FS-Graphics by IDK_FORCE')
        if ch then inicfg.save(cfg, CFG); apply() end
    end
    imgui.End()
end)

RegisterCommand('fsgraphics', function(_, args)
    local a = (args[1] or ''):lower()
    if a == 'on' or a == 'off' then
        cfg.main.enabled = (a == 'on'); inicfg.save(cfg, CFG); apply()
        printStringNow('FS-Graphics ' .. a, 1500)
    elseif a == 'preset' then
        local p = findPreset(args[2])
        if p then
            setPreset(p)
            TriggerEvent('chat:addMessage', { args = { 'FS-Graphics', 'preset ' .. p[2] .. ' - ' .. p[3] } })
        else
            local t = {}
            for _, q in ipairs(PRESETS) do t[#t + 1] = q[1] .. ' (' .. q[3] .. ')' end
            TriggerEvent('chat:addMessage', { args = { 'FS-Graphics', '/fsgraphics preset ' .. table.concat(t, ' | ') } })
        end
    else
        win[0] = not win[0]
    end
end)

AddEventHandler('onResourceStop', function(name)
    if name == GetCurrentResourceName() then
        running = false
        pcall(fsg.restore)
    end
end)

Citizen.CreateThread(function()
    Wait(1500)
    apply()
end)
