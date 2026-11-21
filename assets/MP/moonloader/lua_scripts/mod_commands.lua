-- MP mod commands 1.0 (MP 5.02.37): /mods lists the chat command of every script mod that MP ships, with a button
-- that runs it (plain imgui). A command that no script registered (the mod is switched off) is shown greyed out.

script_name('MP mod commands')
script_author('MP')
script_version('1.0')

local imgui = require 'mimgui'

local LIST = {
    { 'weaponx', 'WeaponX weapon wheel (tap a weapon)' },
    { 'weaponx cfg', 'WeaponX settings' },
    { 'wheel', 'WeaponX on / off' },
    { 'ammohud', 'Dynamic Ammo HUD & animated crosshair menu' },
    { 'compass', 'Advanced Tactical Compass settings' },
    { 'compass mode', 'Compass: next mode' },
    { 'memclean', 'AutoCleanStreaming window' },
    { 'memclean now', 'Free streaming memory now' },
    { 'fpp', 'FPP Realistic: first person on / off' },
    { 'fppcfg', 'FPP Realistic settings' },
    { 'fivem', 'FiveM resource loader' },
    { 'fsgraphics', 'FS-Graphics (FiveM resource)' },
    { 'hudexample', 'FiveM example HUD resource' },
    { 'hasslehud', 'HassleHUD panel' },
    { 'snapturn', 'Snap turn' },
    { 'autotranslate', 'Auto translate' },
    { 'luascripts', 'lua_scripts loader' },
}

local win = imgui.new.bool(false)

imgui.OnFrame(function() return win[0] end, function(self)
    self.HideCursor = false
    local sw, sh = getScreenResolution()
    imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.FirstUseEver, imgui.ImVec2(0.5, 0.5))
    imgui.SetNextWindowSize(imgui.ImVec2(560, 480), imgui.Cond.FirstUseEver)
    if imgui.Begin('Mod commands', win) then
        for _, c in ipairs(LIST) do
            local base = c[1]:match('^(%S+)')
            local on = not sampIsChatCommandDefined or sampIsChatCommandDefined(base)
            if on then
                if imgui.Button('/' .. c[1], imgui.ImVec2(200, 0)) then
                    win[0] = false
                    lua_thread.create(function() wait(0); sampProcessChatInput('/' .. c[1]) end)
                end
            else
                imgui.TextDisabled('/' .. c[1] .. '  (off)')
            end
            imgui.SameLine(220)
            imgui.Text(c[2])
        end
    end
    imgui.End()
end)

function main()
    while not isPlayerPlaying(PLAYER_HANDLE) do wait(250) end
    sampRegisterChatCommand('mods', function() win[0] = not win[0] end)
    wait(-1)
end
