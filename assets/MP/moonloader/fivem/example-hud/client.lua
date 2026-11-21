-- Example FiveM client script, unchanged FiveM style. /hudexample turns it on / off.
local show = false

RegisterCommand('hudexample', function()
    show = not show
    TriggerEvent('chat:addMessage', { args = { 'HUD example', show and 'on' or 'off' } })
    print('hud example ' .. tostring(show))
end, false)

local function text(s, x, y, scale)
    SetTextFont(4)
    SetTextScale(0.0, scale)
    SetTextColour(255, 255, 255, 230)
    SetTextOutline()
    SetTextEntry('STRING')
    AddTextComponentString(s)
    DrawText(x, y)
end

Citizen.CreateThread(function()
    while true do
        Wait(0)
        if show then
            local ped = PlayerPedId()
            local c = GetEntityCoords(ped)
            local speed = GetEntitySpeed(IsPedInAnyVehicle(ped, false) and GetVehiclePedIsIn(ped, false) or ped) * 3.6
            DrawRect(0.12, 0.80, 0.20, 0.07, 0, 0, 0, 140)
            text(string.format('%d km/h', math.floor(speed)), 0.03, 0.770, 0.45)
            text(string.format('%.1f  %.1f  %.1f   %02d:%02d', c.x, c.y, c.z, GetClockHours(), GetClockMinutes()), 0.03, 0.805, 0.30)
        end
    end
end)
