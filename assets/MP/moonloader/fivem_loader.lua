-- FiveM Lua resource loader for MP (Android MonetLoader) 1.0-mp
--
-- Runs FiveM client Lua resources on GTA SA. Each resource is a folder in moonloader/fivem/<name>/ with an
-- fxmanifest.lua (or __resource.lua); its client_scripts / shared_scripts are started in their own environment with
-- a FiveM-style API on top of MonetLoader: Citizen.CreateThread / Wait / SetTimeout, RegisterCommand (a chat
-- command), events, vector3, PlayerPedId, GetEntityCoords, the DrawText / DrawRect / DrawLine / DrawMarker family,
-- World3dToScreen2d, time and weather, GetHashKey, LoadResourceFile, json ... (see FIVEM_NATIVES below). A native
-- that has no SA version returns nil and is written once to moonloader.log. Function names follow the FiveM Lua SDK
-- (https://github.com/JebetrexxMC/fivem-lua-sdk, copied to lib/fivem-sdk/ as a reference - it is not loaded).
-- Server scripts, NUI (HTML) and streamed GTA V assets can't run on SA and are skipped.
--
--   /fivem                 resource list (plain imgui) with Start / Stop / Restart
--   /fivem start|stop|restart <name>     /fivem list
--   resources that register commands keep them (fs-graphics: /fsgraphics)
-- Off switch: moonloader/fivem/<name>/disabled (an empty file) or the checkbox in /fivem.

script_name('FiveM Loader')
script_author('MP')
script_version('1.0-mp')

local imgui = require 'mimgui'
local ffi = require 'ffi'
local okJ, json = pcall(require, 'cjson')
if not okJ then okJ, json = pcall(require, 'json2lua') end
if not okJ then json = nil end

local ROOT = getWorkingDirectory() .. '/fivem'
local log = function(...) print('[FiveM] ' .. table.concat({ ... }, ' ')) end

-- ============================ files ============================
local function exists(p) local f = io.open(p, 'rb'); if f then f:close(); return true end; return false end
local function readAll(p) local f = io.open(p, 'rb'); if not f then return nil end; local s = f:read('*a'); f:close(); return s end

local function listDirs(path)
    local out = {}
    local ok, lfs = pcall(require, 'lfs')
    if ok and lfs then
        pcall(function()
            for n in lfs.dir(path) do
                if n ~= '.' and n ~= '..' and lfs.attributes(path .. '/' .. n, 'mode') == 'directory' then out[#out + 1] = n end
            end
        end)
    end
    if #out == 0 and findFirstFile then
        pcall(function()
            local h, n = findFirstFile(path .. '/*')
            while n do
                if n ~= '.' and n ~= '..' and doesDirectoryExist(path .. '/' .. n) then out[#out + 1] = n end
                n = findNextFile(h)
            end
            if h then findClose(h) end
        end)
    end
    table.sort(out)
    return out
end

local function listFiles(path, pattern)
    local out = {}
    local ok, lfs = pcall(require, 'lfs')
    if ok and lfs then
        pcall(function()
            for n in lfs.dir(path) do
                if n:match(pattern) and lfs.attributes(path .. '/' .. n, 'mode') == 'file' then out[#out + 1] = n end
            end
        end)
    elseif findFirstFile then
        pcall(function()
            local h, n = findFirstFile(path .. '/*')
            while n do
                if n:match(pattern) then out[#out + 1] = n end
                n = findNextFile(h)
            end
            if h then findClose(h) end
        end)
    end
    table.sort(out)
    return out
end

-- 'client/*.lua' -> files
local function expand(dir, spec)
    spec = tostring(spec):gsub('\\', '/'):gsub('^%./', '')
    if not spec:find('*', 1, true) then return { spec } end
    local sub, mask = spec:match('^(.-)/?([^/]*)$')
    local pat = '^' .. mask:gsub('[%.%-%+%?%(%)%[%]%^%$]', '%%%0'):gsub('%*', '.*') .. '$'
    local res = {}
    for _, f in ipairs(listFiles(dir .. (sub ~= '' and '/' .. sub or ''), pat)) do
        res[#res + 1] = (sub ~= '' and sub .. '/' or '') .. f
    end
    return res
end

-- ============================ manifest ============================
local function readManifest(dir)
    local path = dir .. '/fxmanifest.lua'
    if not exists(path) then path = dir .. '/__resource.lua' end
    local src = readAll(path)
    if not src then return nil, 'no fxmanifest.lua' end
    local m = { client = {}, shared = {}, meta = {} }
    local function add(list, v)
        if type(v) == 'table' then for _, x in ipairs(v) do list[#list + 1] = x end else list[#list + 1] = v end
    end
    local env
    env = setmetatable({}, { __index = function(_, key)
        local function collect(v)
            if key == 'client_script' or key == 'client_scripts' then add(m.client, v)
            elseif key == 'shared_script' or key == 'shared_scripts' then add(m.shared, v)
            elseif type(v) == 'string' or type(v) == 'number' then m.meta[key] = m.meta[key] or tostring(v)
            end
            return collect   -- dependency 'a' 'b'
        end
        return collect
    end })
    local fn, err = loadstring(src, '@' .. path)
    if not fn then return nil, err end
    setfenv(fn, env)
    local ok, e = pcall(fn)
    if not ok then return nil, e end
    return m
end

-- ============================ drawing queue ============================
-- FiveM draw natives last one frame. Each script thread's draws between two Wait() calls are one batch; the frame
-- draws the last finished batch of every thread (so a thread that stops drawing disappears, nothing is doubled).
local Q = setmetatable({}, { __mode = 'k' })
local batchOf = setmetatable({}, { __mode = 'k' })
local MAINCO = {}
local function push(item)
    local co = coroutine.running() or MAINCO
    local b = batchOf[co] or 0
    local q = Q[co]
    if not q or q.batch ~= b then q = { batch = b, items = {} }; Q[co] = q end
    q.items[#q.items + 1] = item
end
local function fwait(ms)
    local co = coroutine.running()
    if co then batchOf[co] = (batchOf[co] or 0) + 1 end
    wait(math.max(0, tonumber(ms) or 0))
end

local TEXT = {}
local function resetText()
    TEXT = { font = 0, scale = 0.35, r = 255, g = 255, b = 255, a = 255, centre = false, outline = false, shadow = false,
        right = false, wrapR = 1.0, str = '' }
end
resetText()

local function col32(r, g, b, a)
    r, g, b, a = math.floor(r or 255) % 256, math.floor(g or 255) % 256, math.floor(b or 255) % 256, math.floor(a or 255)
    if a < 0 then a = 0 elseif a > 255 then a = 255 end
    return a * 16777216 + b * 65536 + g * 256 + r
end

local function stripCodes(s)
    s = tostring(s or '')
    s = s:gsub('~[%a_]~', ''):gsub('~h~', ''):gsub('~n~', '\n'):gsub('%^%d', '')
    return s
end

imgui.OnFrame(function() return true end, function(self)
    self.HideCursor = true
    local list = {}
    for co, q in pairs(Q) do
        local cur = (batchOf[co] or 0) - 1
        if q.batch == cur then
            for _, it in ipairs(q.items) do list[#list + 1] = it end
        elseif q.batch < cur then
            Q[co] = nil
        end
    end
    if #list == 0 then return end
    local sw, sh = getScreenResolution()
    local dl = imgui.GetBackgroundDrawList()
    local font = imgui.GetFont()
    for _, it in ipairs(list) do
        if it.k == 'rect' then
            dl:AddRectFilled(imgui.ImVec2((it.x - it.w / 2) * sw, (it.y - it.h / 2) * sh),
                imgui.ImVec2((it.x + it.w / 2) * sw, (it.y + it.h / 2) * sh), it.c)
        elseif it.k == 'line' then
            dl:AddLine(imgui.ImVec2(it.x1, it.y1), imgui.ImVec2(it.x2, it.y2), it.c, it.t or 2)
        elseif it.k == 'circle' then
            dl:AddCircle(imgui.ImVec2(it.x, it.y), it.r, it.c, 24, it.t or 2)
            if it.fill then dl:AddCircleFilled(imgui.ImVec2(it.x, it.y), it.r, it.fill, 24) end
        elseif it.k == 'text' then
            local size = math.max(8, it.scale * 43 * (sh / 1080))
            local x, y = it.x * sw, it.y * sh
            local ts = font:CalcTextSizeA(size, 10000, 0, it.s)
            if it.centre then x = x - ts.x / 2 elseif it.right then x = it.wrapR * sw - ts.x end
            if it.outline or it.shadow then
                local oc = col32(0, 0, 0, it.a)
                dl:AddTextFontPtr(font, size, imgui.ImVec2(x + 1.5, y + 1.5), oc, it.s)
                if it.outline then dl:AddTextFontPtr(font, size, imgui.ImVec2(x - 1, y - 1), oc, it.s) end
            end
            dl:AddTextFontPtr(font, size, imgui.ImVec2(x, y), it.c, it.s)
        end
    end
end)

-- ============================ helpers ============================
local vecmt = {}
local function vec(x, y, z, w)
    return setmetatable({ x = x or 0.0, y = y or 0.0, z = z, w = w }, vecmt)
end
vecmt.__index = function(t, k)
    if k == 1 then return rawget(t, 'x') elseif k == 2 then return rawget(t, 'y') elseif k == 3 then return rawget(t, 'z')
    elseif k == 4 then return rawget(t, 'w') elseif k == 'xy' then return vec(t.x, t.y) elseif k == 'xyz' then return vec(t.x, t.y, t.z) end
end
local function vv(a) if type(a) == 'number' then return a, a, a, a end return a.x, a.y, a.z or 0, a.w or 0 end
vecmt.__add = function(a, b) local ax, ay, az, aw = vv(a); local bx, by, bz, bw = vv(b); return vec(ax + bx, ay + by, (a.z or b.z) and az + bz, (a.w or b.w) and aw + bw) end
vecmt.__sub = function(a, b) local ax, ay, az, aw = vv(a); local bx, by, bz, bw = vv(b); return vec(ax - bx, ay - by, (type(a) == 'table' and a.z or b.z) and az - bz, nil) end
vecmt.__mul = function(a, b) local ax, ay, az = vv(a); local bx, by, bz = vv(b); local z = (type(a) == 'table' and a.z) or (type(b) == 'table' and b.z); return vec(ax * bx, ay * by, z and az * bz) end
vecmt.__div = function(a, b) local ax, ay, az = vv(a); local bx, by, bz = vv(b); return vec(ax / bx, ay / by, a.z and az / bz) end
vecmt.__unm = function(a) return vec(-a.x, -a.y, a.z and -a.z) end
vecmt.__len = function(a) return math.sqrt(a.x * a.x + a.y * a.y + (a.z or 0) ^ 2) end
vecmt.__eq = function(a, b) return a.x == b.x and a.y == b.y and a.z == b.z end
vecmt.__tostring = function(a) return string.format('vector3(%.3f, %.3f, %.3f)', a.x, a.y, a.z or 0) end

local function joaat(s)
    s = tostring(s):lower()
    local h = 0
    for i = 1, #s do
        h = bit.band(h + s:byte(i), 0xFFFFFFFF)
        h = bit.band(h + bit.lshift(h, 10), 0xFFFFFFFF)
        h = bit.bxor(h, bit.rshift(h, 6))
    end
    h = bit.band(h + bit.lshift(h, 3), 0xFFFFFFFF)
    h = bit.bxor(h, bit.rshift(h, 11))
    h = bit.band(h + bit.lshift(h, 15), 0xFFFFFFFF)
    if h < 0 then h = h + 4294967296 end
    if h >= 2147483648 then h = h - 4294967296 end
    return h
end

local function toScreen(x, y, z)
    local ok, sx, sy = pcall(convert3DCoordsToScreen, x, y, z)
    if not ok or not sx then return false end
    local cx, cy, cz = getActiveCameraCoordinates()
    local px, py, pz = getActiveCameraPointAt()
    if cx and px then
        local d = (x - cx) * (px - cx) + (y - cy) * (py - cy) + (z - cz) * (pz - cz)
        if d <= 0 then return false end
    end
    return true, sx, sy
end

local SA_WEATHER = { EXTRASUNNY = 0, CLEAR = 1, CLOUDS = 4, SMOG = 2, FOGGY = 9, OVERCAST = 7, RAIN = 8, THUNDER = 16,
    CLEARING = 5, NEUTRAL = 1, SNOW = 7, BLIZZARD = 9, SNOWLIGHT = 7, XMAS = 7, HALLOWEEN = 9 }

local function camRot()
    local cx, cy, cz = getActiveCameraCoordinates()
    local px, py, pz = getActiveCameraPointAt()
    if not cx or not px then return 0, 0, 0 end
    local dx, dy, dz = px - cx, py - cy, pz - cz
    local yaw = math.deg(math.atan2(-dx, dy))
    local pitch = math.deg(math.atan2(dz, math.sqrt(dx * dx + dy * dy)))
    return pitch, 0.0, yaw
end

-- ============================ resources ============================
local resources = {}   -- name -> { name, dir, state, threads, commands, handlers, env, err, meta }
local order = {}
local events = {}      -- name -> { {res, fn} }

local function trigger(name, ...)
    local list = events[name]
    if not list then return end
    for _, h in ipairs(list) do
        local ok, e = pcall(h.fn, ...)
        if not ok then log(h.res .. ': event ' .. name .. ': ' .. tostring(e)) end
    end
end

local warned = {}

-- FiveM's chat resource events -> SA chat
local function chatMsg(m)
    if type(m) ~= 'table' then sampAddChatMessage(stripCodes(m), -1); return end
    local a = m.args or {}
    local t = {}
    for i = 1, #a do t[i] = stripCodes(a[i]) end
    local line = (#t > 1) and ('{9AD0FF}' .. t[1] .. ':{FFFFFF} ' .. table.concat(t, ' ', 2)) or (t[1] or stripCodes(m.template or ''))
    sampAddChatMessage(line, -1)
end
events['chat:addMessage'] = { { res = 'chat', fn = chatMsg } }
events['chatMessage'] = { { res = 'chat', fn = function(author, _, text) chatMsg({ args = { author, text } }) end } }

local function makeEnv(res)
    local env = {}
    local function thread(fn)
        local t = lua_thread.create(function()
            local ok, e = pcall(fn)
            if not ok and not tostring(e):find('terminated') then log(res.name .. ': ' .. tostring(e)); res.err = tostring(e) end
        end)
        res.threads[#res.threads + 1] = t
        return t
    end
    local Citizen = {}
    Citizen.CreateThread = thread
    Citizen.CreateThreadNow = thread
    Citizen.Wait = fwait
    Citizen.SetTimeout = function(ms, fn) thread(function() fwait(ms); fn() end) end
    Citizen.Trace = function(s) log(res.name .. ': ' .. tostring(s)) end
    Citizen.InvokeNative = function() return nil end
    env.Citizen = Citizen
    env.CreateThread = thread
    env.Wait = Citizen.Wait
    env.SetTimeout = Citizen.SetTimeout

    env.vector2 = function(x, y) return vec(x, y) end
    env.vector3 = function(x, y, z) return vec(x, y, z or 0.0) end
    env.vector4 = function(x, y, z, w) return vec(x, y, z, w) end
    env.vec2, env.vec3, env.vec4 = env.vector2, env.vector3, env.vector4
    env.json = json and { encode = json.encode, decode = json.decode } or nil
    env.GetHashKey = joaat
    env.GetCurrentResourceName = function() return res.name end
    env.GetResourceState = function(n) local r = resources[n]; return r and r.state or 'missing' end
    env.LoadResourceFile = function(n, file) local r = resources[n]; return r and readAll(r.dir .. '/' .. file) or nil end
    env.SaveResourceFile = function(n, file, data)
        local r = resources[n]; if not r then return false end
        local f = io.open(r.dir .. '/' .. file, 'wb'); if not f then return false end
        f:write(data); f:close(); return true
    end
    env.exports = setmetatable({}, { __index = function() return setmetatable({}, { __index = function() return function() end end }) end })

    -- events
    env.AddEventHandler = function(name, fn)
        events[name] = events[name] or {}
        local h = { res = res.name, fn = fn }
        table.insert(events[name], h)
        res.handlers[#res.handlers + 1] = { name = name, h = h }
        return h
    end
    env.RemoveEventHandler = function(h)
        for _, list in pairs(events) do for i = #list, 1, -1 do if list[i] == h then table.remove(list, i) end end end
    end
    env.RegisterNetEvent = function(name, fn) if fn then return env.AddEventHandler(name, fn) end end
    env.RegisterClientEvent = env.RegisterNetEvent
    env.TriggerEvent = trigger
    env.TriggerServerEvent = function(name) if not warned['srv:' .. name] then warned['srv:' .. name] = true; log(res.name .. ': TriggerServerEvent(' .. tostring(name) .. ') - no FiveM server here') end end
    env.TriggerLatentServerEvent = env.TriggerServerEvent

    -- commands
    env.RegisterCommand = function(name, fn)
        name = tostring(name):lower()
        if sampIsChatCommandDefined and sampIsChatCommandDefined(name) then pcall(sampUnregisterChatCommand, name) end
        sampRegisterChatCommand(name, function(param)
            local args = {}
            for a in tostring(param or ''):gmatch('%S+') do args[#args + 1] = a end
            thread(function() fn(0, args, '/' .. name .. (param ~= '' and (' ' .. param) or '')) end)
        end)
        res.commands[#res.commands + 1] = name
    end
    env.RegisterKeyMapping = function() end
    env.ExecuteCommand = function(cmd) sampProcessChatInput('/' .. tostring(cmd)) end

    -- player / entities
    env.PlayerPedId = function() return PLAYER_PED end
    env.GetPlayerPed = function() return PLAYER_PED end
    env.PlayerId = function() return PLAYER_HANDLE end
    env.GetPlayerServerId = function() return 0 end
    env.GetPlayerName = function() return (sampGetPlayerNickname and select(2, pcall(sampGetPlayerNickname, 0))) or 'Player' end
    env.DoesEntityExist = function(e) return e ~= nil and e ~= 0 and (doesCharExist(e) or doesVehicleExist(e)) end
    env.IsEntityAPed = function(e) return doesCharExist(e) end
    env.IsEntityAVehicle = function(e) return doesVehicleExist(e) end
    env.GetEntityCoords = function(e)
        if doesCharExist(e) then return vec(getCharCoordinates(e)) end
        if doesVehicleExist(e) then return vec(getCarCoordinates(e)) end
        return vec(0, 0, 0)
    end
    env.SetEntityCoords = function(e, x, y, z) if doesCharExist(e) then setCharCoordinates(e, x, y, z) elseif doesVehicleExist(e) then setCarCoordinates(e, x, y, z) end end
    env.SetEntityCoordsNoOffset = env.SetEntityCoords
    env.GetEntityHeading = function(e) if doesCharExist(e) then return getCharHeading(e) elseif doesVehicleExist(e) then return getCarHeading(e) end return 0.0 end
    env.SetEntityHeading = function(e, h) if doesCharExist(e) then setCharHeading(e, h) elseif doesVehicleExist(e) then setCarHeading(e, h) end end
    env.GetEntityHealth = function(e) if doesCharExist(e) then return getCharHealth(e) + 100 elseif doesVehicleExist(e) then return getCarHealth(e) end return 0 end
    env.SetEntityHealth = function(e, h) if doesCharExist(e) then setCharHealth(e, math.max(0, h - 100)) elseif doesVehicleExist(e) then setCarHealth(e, h) end end
    env.GetEntityMaxHealth = function() return 200 end
    env.GetPedArmour = function(p) return getCharArmour(p) end
    env.SetPedArmour = function(p, a) addArmourToChar(p, a - getCharArmour(p)) end
    env.GetEntitySpeed = function(e) if doesCharExist(e) then return getCharSpeed(e) elseif doesVehicleExist(e) then return getCarSpeed(e) end return 0.0 end
    env.GetEntityModel = function(e) if doesCharExist(e) then return getCharModel(e) elseif doesVehicleExist(e) then return getCarModel(e) end return 0 end
    env.IsEntityDead = function(e) return doesCharExist(e) and isCharDead(e) end
    env.IsPedDeadOrDying = env.IsEntityDead
    env.IsPedInAnyVehicle = function(p) return isCharInAnyCar(p) end
    env.GetVehiclePedIsIn = function(p) if isCharInAnyCar(p) then return storeCarCharIsInNoSave(p) end return 0 end
    env.GetVehiclePedIsUsing = env.GetVehiclePedIsIn
    env.IsPedOnFoot = function(p) return isCharOnFoot(p) end
    env.IsPedSprinting = function(p) return isCharOnFoot(p) and getCharSpeed(p) > 6 end
    env.IsPedRunning = function(p) return isCharOnFoot(p) and getCharSpeed(p) > 4 end
    env.IsPedWalking = function(p) local s = getCharSpeed(p); return isCharOnFoot(p) and s > 0.5 and s <= 4 end
    env.IsPedShooting = function(p) return isCharShooting and isCharShooting(p) or false end
    env.IsPlayerFreeAiming = function() local ok, t = pcall(isPlayerTargettingAnything, PLAYER_HANDLE); return ok and t or false end
    env.GetSelectedPedWeapon = function(p) return getCurrentCharWeapon(p) end
    env.GetVehicleEngineHealth = function(v) return getCarHealth(v) end
    env.GetEntitySpeedVector = function() return vec(0, 0, 0) end
    env.GetDistanceBetweenCoords = function(x1, y1, z1, x2, y2, z2, use3d)
        if use3d == false then return math.sqrt((x1 - x2) ^ 2 + (y1 - y2) ^ 2) end
        return math.sqrt((x1 - x2) ^ 2 + (y1 - y2) ^ 2 + (z1 - z2) ^ 2)
    end
    env.Vdist = function(x1, y1, z1, x2, y2, z2) return math.sqrt((x1 - x2) ^ 2 + (y1 - y2) ^ 2 + (z1 - z2) ^ 2) end
    env.Vdist2 = function(x1, y1, z1, x2, y2, z2) return (x1 - x2) ^ 2 + (y1 - y2) ^ 2 + (z1 - z2) ^ 2 end
    env.GetGroundZFor_3dCoord = function(x, y, z) return true, getGroundZFor3dCoord(x, y, z) end
    env.GetFirstBlipInfoId = function() return 8 end
    env.DoesBlipExist = function() local ok = getTargetBlipCoordinates(); return ok and true or false end
    env.GetBlipInfoIdCoord = function() local ok, x, y, z = getTargetBlipCoordinates(); return vec(x or 0, y or 0, z or 0) end

    -- camera
    env.GetGameplayCamCoord = function() return vec(getActiveCameraCoordinates()) end
    env.GetFinalRenderedCamCoord = env.GetGameplayCamCoord
    env.GetGameplayCamRot = function() return vec(camRot()) end
    env.GetFinalRenderedCamRot = env.GetGameplayCamRot
    env.GetGameplayCamFov = function() return getCameraFov and getCameraFov() or 70.0 end
    env.GetFinalRenderedCamFov = env.GetGameplayCamFov
    env.GetGameplayCamRelativeHeading = function() local _, _, y = camRot(); return y - getCharHeading(PLAYER_PED) end
    env.IsGameplayCamRendering = function() return true end

    -- world / time
    env.GetGameTimer = function() return getGameTimer and getGameTimer() or math.floor(os.clock() * 1000) end
    env.GetFrameTime = function() return 1 / 30 end
    env.GetFrameCount = function() return math.floor(os.clock() * 60) end
    env.GetClockHours = function() return (getTimeOfDay()) end
    env.GetClockMinutes = function() return select(2, getTimeOfDay()) end
    env.GetClockSeconds = function() return 0 end
    env.NetworkOverrideClockTime = function(h, m) setTimeOfDay(h or 12, m or 0) end
    env.SetClockTime = env.NetworkOverrideClockTime
    env.SetWeatherTypeNow = function(w) local id = SA_WEATHER[tostring(w):upper()]; if id then forceWeatherNow(id) end end
    env.SetWeatherTypeNowPersist = env.SetWeatherTypeNow
    env.SetWeatherTypePersist = env.SetWeatherTypeNow
    env.SetOverrideWeather = env.SetWeatherTypeNow
    env.SetWeatherTypeOvertimePersist = env.SetWeatherTypeNow
    env.ClearOverrideWeather = function() if releaseWeather then releaseWeather() end end
    env.ClearWeatherTypePersist = env.ClearOverrideWeather
    env.IsPauseMenuActive = function() return isPauseMenuActive and isPauseMenuActive() or false end
    env.GetScreenResolution = function() return getScreenResolution() end
    env.GetActiveScreenResolution = env.GetScreenResolution
    env.GetAspectRatio = function() local w, h = getScreenResolution(); return w / h end
    env.GetSafeZoneSize = function() return 1.0 end

    -- controls (touch game: keys are not there, so nothing is "pressed")
    local no = function() return false end
    env.IsControlPressed, env.IsControlJustPressed, env.IsControlJustReleased, env.IsControlReleased = no, no, no, function() return true end
    env.IsDisabledControlPressed, env.IsDisabledControlJustPressed, env.IsDisabledControlJustReleased = no, no, no
    env.DisableControlAction, env.EnableControlAction, env.DisableAllControlActions, env.EnableAllControlActions = function() end, function() end, function() end, function() end
    env.GetControlNormal = function() return 0.0 end
    env.GetDisabledControlNormal = env.GetControlNormal

    -- notifications
    local feed = ''
    env.SetNotificationTextEntry = function() feed = '' end
    env.BeginTextCommandThefeedPost = env.SetNotificationTextEntry
    env.AddTextComponentString = function(s) feed = feed .. tostring(s); TEXT.str = TEXT.str .. tostring(s) end
    env.AddTextComponentSubstringPlayerName = env.AddTextComponentString
    env.AddTextComponentSubstringTextLabel = env.AddTextComponentString
    env.AddTextComponentInteger = function(n) env.AddTextComponentString(tostring(n)) end
    env.AddTextComponentFloat = function(n, d) env.AddTextComponentString(string.format('%.' .. (d or 2) .. 'f', n)) end
    env.DrawNotification = function() sampAddChatMessage('{9AD0FF}[' .. res.name .. ']{FFFFFF} ' .. stripCodes(feed), -1); feed = '' end
    env.EndTextCommandThefeedPostTicker = env.DrawNotification
    env.BeginTextCommandPrint = function() feed = '' end
    env.EndTextCommandPrint = function(ms) printStringNow(stripCodes(feed), ms or 2500) end
    env.BeginTextCommandDisplayHelp = env.BeginTextCommandPrint
    env.EndTextCommandDisplayHelp = function() printStringNow(stripCodes(feed), 1500) end

    -- 2D text / shapes (FiveM screen coords: 0..1)
    env.SetTextFont = function(f) TEXT.font = f end
    env.SetTextScale = function(a, b) TEXT.scale = (b and b > 0) and b or a end
    env.SetTextColour = function(r, g, b, a) TEXT.r, TEXT.g, TEXT.b, TEXT.a = r, g, b, a end
    env.SetTextColor = env.SetTextColour
    env.SetTextCentre = function(on) TEXT.centre = on and true or false end
    env.SetTextJustification = function(j) TEXT.centre = (j == 0); TEXT.right = (j == 2) end
    env.SetTextWrap = function(_, r) TEXT.wrapR = r or 1.0 end
    env.SetTextRightJustify = function(on) TEXT.right = on and true or false end
    env.SetTextOutline = function() TEXT.outline = true end
    env.SetTextDropShadow = function() TEXT.shadow = true end
    env.SetTextDropshadow = env.SetTextDropShadow
    env.SetTextEdge = function() TEXT.outline = true end
    env.SetTextProportional = function() end
    env.SetTextEntry = function() TEXT.str = '' end
    env.BeginTextCommandDisplayText = env.SetTextEntry
    local function drawText(x, y)
        push({ k = 'text', x = x or 0, y = y or 0, s = stripCodes(TEXT.str), scale = TEXT.scale, c = col32(TEXT.r, TEXT.g, TEXT.b, TEXT.a),
            a = TEXT.a, centre = TEXT.centre, right = TEXT.right, wrapR = TEXT.wrapR, outline = TEXT.outline, shadow = TEXT.shadow })
        resetText()
    end
    env.DrawText = drawText
    env.EndTextCommandDisplayText = drawText
    env.DrawRect = function(x, y, w, h, r, g, b, a) push({ k = 'rect', x = x, y = y, w = w, h = h, c = col32(r, g, b, a) }) end
    env.DrawLine = function(x1, y1, z1, x2, y2, z2, r, g, b, a)
        local ok1, sx1, sy1 = toScreen(x1, y1, z1)
        local ok2, sx2, sy2 = toScreen(x2, y2, z2)
        if ok1 and ok2 then push({ k = 'line', x1 = sx1, y1 = sy1, x2 = sx2, y2 = sy2, c = col32(r, g, b, a) }) end
    end
    env.DrawMarker = function(_, x, y, z, _, _, _, _, _, _, sx, sy, sz, r, g, b, a)
        local ok, px, py = toScreen(x, y, z)
        if not ok then return end
        local ok2, qx, qy = toScreen(x + (sx or 1) / 2, y, z)
        local rad = (ok2 and math.max(4, math.abs(qx - px))) or 12
        push({ k = 'circle', x = px, y = py, r = rad, c = col32(r, g, b, math.min(255, (a or 150) + 60)), fill = col32(r, g, b, (a or 150) * 0.5) })
    end
    env.World3dToScreen2d = function(x, y, z)
        local ok, sx, sy = toScreen(x, y, z)
        if not ok then return false, 0.0, 0.0 end
        local w, h = getScreenResolution()
        return true, sx / w, sy / h
    end
    env.GetScreenCoordFromWorldCoord = env.World3dToScreen2d
    env.HasStreamedTextureDictLoaded = function() return true end
    env.RequestStreamedTextureDict = function() end
    env.DrawSprite = function(_, _, x, y, w, h, _, r, g, b, a) push({ k = 'rect', x = x, y = y, w = w, h = h, c = col32(r, g, b, (a or 255) * 0.5) }) end

    -- graphics natives without an SA version: accepted, ignored (logged once)
    for _, n in ipairs({ 'SetTimecycleModifier', 'SetTimecycleModifierStrength', 'ClearTimecycleModifier', 'SetExtraTimecycleModifier',
            'ClearExtraTimecycleModifier', 'SetTransitionTimecycleModifier', 'SetArtificialLightsState', 'SetVisualSettingFloat',
            'SetFlashLightKeepOnWhileMoving', 'SetForcePedFootstepsTracks', 'SetForceVehicleTrails', 'SetRainLevel', 'SetWindSpeed',
            'SetWind', 'SetWindDirection', 'SetCloudHatOpacity', 'LoadCloudHat', 'ClearCloudHat', 'SetDeepOceanScaler',
            'SetSnowLevel', 'SetPlayerWantedLevel', 'SetPlayerWantedLevelNow', 'SetRadarZoom', 'DisplayRadar', 'HideHudComponentThisFrame' }) do
        env[n] = function() if not warned[n] then warned[n] = true; log(res.name .. ': ' .. n .. ' has no SA version (ignored)') end end
    end
    env.DisplayRadar = function(on) if displayRadar then displayRadar(on and true or false) end end
    env.SetPlayerWantedLevel = function(_, lvl) alterWantedLevel(PLAYER_HANDLE, lvl or 0) end

    env.print = function(...) local t = {}; for i = 1, select('#', ...) do t[i] = tostring(select(i, ...)) end; log(res.name .. ': ' .. table.concat(t, ' ')) end
    env._G = env
    setmetatable(env, { __index = function(_, k)
        local v = _G[k]
        if v ~= nil then return v end
        if type(k) == 'string' and k:match('^[A-Z][%w_]*$') and k ~= 'Config' then
            return function()
                if not warned[k] then warned[k] = true; log(res.name .. ': native ' .. k .. ' is not on SA (returns nil)') end
                return nil
            end
        end
        return nil
    end })
    return env
end

local function stopResource(name)
    local r = resources[name]
    if not r or r.state ~= 'started' then return end
    trigger('onResourceStop', name)
    trigger('onClientResourceStop', name)
    for _, t in ipairs(r.threads) do pcall(function() t:terminate() end) end
    for _, c in ipairs(r.commands) do pcall(sampUnregisterChatCommand, c) end
    for _, hh in ipairs(r.handlers) do
        local list = events[hh.name]
        if list then for i = #list, 1, -1 do if list[i] == hh.h then table.remove(list, i) end end end
    end
    r.threads, r.commands, r.handlers, r.env = {}, {}, {}, nil
    r.state = 'stopped'
    log('stopped ' .. name)
end

local function startResource(name)
    local r = resources[name]
    if not r or r.state == 'started' then return end
    r.threads, r.commands, r.handlers, r.err = {}, {}, {}, nil
    local m, err = readManifest(r.dir)
    if not m then r.err = err; r.state = 'error'; log(name .. ': ' .. tostring(err)); return end
    r.meta = m.meta
    local env = makeEnv(r)
    r.env = env
    r.state = 'started'
    local files = {}
    for _, list in ipairs({ m.shared, m.client }) do
        for _, spec in ipairs(list) do
            if not tostring(spec):match('^@') then
                for _, f in ipairs(expand(r.dir, spec)) do files[#files + 1] = f end
            end
        end
    end
    r.files = files
    local main = lua_thread.create(function()
        for _, f in ipairs(files) do
            if f:match('%.lua$') then
                local src = readAll(r.dir .. '/' .. f)
                if not src then log(name .. ': missing ' .. f)
                else
                    local fn, e = loadstring(src, '@' .. name .. '/' .. f)
                    if not fn then r.err = e; log(name .. ': ' .. tostring(e))
                    else
                        setfenv(fn, env)
                        local ok, e2 = pcall(fn)
                        if not ok then r.err = tostring(e2); log(name .. ': ' .. tostring(e2)) end
                    end
                end
            end
        end
        trigger('onClientResourceStart', name)
        trigger('onResourceStart', name)
    end)
    r.threads[#r.threads + 1] = main
    log('started ' .. name .. ' (' .. #files .. ' files)')
end

local function scanResources()
    for _, n in ipairs(listDirs(ROOT)) do
        local dir = ROOT .. '/' .. n
        if not resources[n] and (exists(dir .. '/fxmanifest.lua') or exists(dir .. '/__resource.lua')) then
            resources[n] = { name = n, dir = dir, state = 'stopped', threads = {}, commands = {}, handlers = {} }
            order[#order + 1] = n
        end
    end
    table.sort(order)
end

-- ============================ /fivem window (plain imgui) ============================
local win = imgui.new.bool(false)
imgui.OnFrame(function() return win[0] end, function(self)
    self.HideCursor = false
    local sw, sh = getScreenResolution()
    imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.FirstUseEver, imgui.ImVec2(0.5, 0.5))
    imgui.SetNextWindowSize(imgui.ImVec2(620, 420), imgui.Cond.FirstUseEver)
    if imgui.Begin('FiveM resources', win) then
        imgui.TextDisabled('moonloader/fivem/<name>/fxmanifest.lua')
        if imgui.Button('Rescan') then scanResources() end
        imgui.Separator()
        for _, n in ipairs(order) do
            local r = resources[n]
            imgui.PushIDStr(n)
            imgui.Text(n)
            imgui.SameLine(220)
            imgui.TextDisabled(r.state)
            imgui.SameLine(320)
            if r.state == 'started' then
                if imgui.Button('Stop') then stopResource(n); local f = io.open(r.dir .. '/disabled', 'wb'); if f then f:close() end end
                imgui.SameLine()
                if imgui.Button('Restart') then stopResource(n); startResource(n) end
            else
                if imgui.Button('Start') then os.remove(r.dir .. '/disabled'); startResource(n) end
            end
            if r.meta and (r.meta.description or r.meta.author) then
                imgui.TextDisabled('  ' .. tostring(r.meta.description or '') .. (r.meta.author and ('  by ' .. r.meta.author) or ''))
            end
            if #r.commands > 0 then imgui.TextDisabled('  commands: /' .. table.concat(r.commands, '  /')) end
            if r.err then imgui.TextColored(imgui.ImVec4(1, 0.45, 0.4, 1), '  ' .. r.err:sub(1, 160)) end
            imgui.PopID()
        end
        if #order == 0 then imgui.Text('No resources.') end
    end
    imgui.End()
end)

function main()
    while not isPlayerPlaying(PLAYER_HANDLE) do wait(250) end
    scanResources()
    for _, n in ipairs(order) do
        if not exists(resources[n].dir .. '/disabled') then startResource(n) end
    end
    sampRegisterChatCommand('fivem', function(arg)
        local cmd, name = tostring(arg or ''):match('^(%S*)%s*(.-)%s*$')
        cmd = cmd:lower()
        if (cmd == 'start' or cmd == 'stop' or cmd == 'restart') and name ~= '' then
            scanResources()
            if not resources[name] then sampAddChatMessage('{9AD0FF}[FiveM]{FFFFFF} no resource ' .. name, -1); return end
            if cmd ~= 'start' then stopResource(name) end
            if cmd ~= 'stop' then startResource(name) end
            sampAddChatMessage('{9AD0FF}[FiveM]{FFFFFF} ' .. name .. ': ' .. resources[name].state, -1)
        elseif cmd == 'list' then
            local t = {}
            for _, n in ipairs(order) do t[#t + 1] = n .. ' (' .. resources[n].state .. ')' end
            sampAddChatMessage('{9AD0FF}[FiveM]{FFFFFF} ' .. (#t > 0 and table.concat(t, ', ') or 'no resources'), -1)
        else
            win[0] = not win[0]
        end
    end)
    wait(-1)
end

function onScriptTerminate(s)
    if s == thisScript() then
        for _, n in ipairs(order) do pcall(stopResource, n) end
    end
end
