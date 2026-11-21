-- Made by sebicu , IN MOST OF THE CHASES FOR SAMP VERSION!!! --

script_name("AutoCleanStreaming")
script_author("sebicu")
script_description("Gentle streaming cleaner (lag-free trickle). ASI handles EXE side.")

-- MP 5.02.37: /memclean opens a small settings window (plain imgui). On the phone the PC addresses are translated
-- by MonetLoader for MP; the cap is also kept under the game's own streaming budget (phones have less memory).
local inicfg = require("inicfg")
local okIm, imgui = pcall(require, "mimgui")
if not okIm then imgui = nil end

local CFG_FILE = "AutoCleanStreaming.ini"
local cfg = inicfg.load({
    streaming = {
        autoClean = 1,
        limitMB = 1100,
        limitPercent = 80,
        emergencyPercent = 92,
        checkMs = 500,
        passes = 20,
        trickle = 1,
        tricklePasses = 4,
        softPercent = 70,
        periodicSec = 120,
        minIntervalSec = 4,
        escalateMin = 30,
    },
    compat = {
        respectMenu = 1,
        startupDelaySec = 30,
    },
    debug = {
        logLevel = 1,
        heartbeatSec = 60,
        showHud = 0,
    },
}, CFG_FILE)
inicfg.save(cfg, CFG_FILE)

local function num(v, d) v = tonumber(v) return v == nil and d or v end
local function C() return cfg end

local lastCheck, lastClean, lastPeriodic, lastHb, lastCleanLog = 0, 0, 0, 0, 0
local skippedCleans, skippedFreed, sessionStart, lateMode = 0, 0, 0, false
local bootTick = os.clock()

local function streamMB()
    return readMemory(0x8E4CB4, 4, true) / 1048576
end

local function streamAvailMB()
    local a = readMemory(0x8A5A80, 4, true)
    if a == 0 then return 2047 end
    if a > 100000 then return a / 1048576 end
    return a
end

local function streamPct()
    local av = streamAvailMB()
    if av <= 0 then return 0 end
    return streamMB() * 100 / av
end

local function msg(text)
    print("[AutoCleanStreaming] " .. text)
    if num(C().debug.showHud, 0) == 1 and showTextBox then
        showTextBox(text)
    end
end

local function logCleanLine(text, freed, emergency)
    local lvl = num(C().debug.logLevel, 1)
    if emergency or lvl >= 2 then print("[AutoCleanStreaming] " .. text) return end
    if lvl <= 0 then return end
    local now = os.clock()
    if now - lastCleanLog >= 60 then
        if skippedCleans > 0 then
            print(string.format("[AutoCleanStreaming] Summary: %d cleans merged (~%d MB freed).",
                skippedCleans, skippedFreed))
            skippedCleans, skippedFreed = 0, 0
        end
        print("[AutoCleanStreaming] " .. text)
        lastCleanLog = now
    else
        skippedCleans = skippedCleans + 1
        skippedFreed = skippedFreed + freed
    end
end

local function lruLoop(n)
    local gone = 0
    for i = 1, n do
        callFunction(0x40CFD0, 0, 0)
        gone = gone + 1
    end
    return gone
end

local function streamClean(reason, emergency, passes)
    local before = streamMB()
    callFunction(0x40E4E0, 0, 0)
    callFunction(0x40CF80, 0, 0)
    local n = lruLoop(passes)
    callFunction(0x707770, 0, 0)
    if emergency then
        callFunction(0x40D7C0, 1, 1, 12 * 1024 * 1024)
        callFunction(0x40E4E0, 0, 0)
    end
    local after = streamMB()
    local text = string.format("Clean STR: %d->%d MB (cap %d, n=%d) [%s%s]",
        before, after, num(C().streaming.limitMB, 1100), n, reason,
        emergency and "-CAP" or "")
    msg(text)
    logCleanLine(text, before - after, emergency)
    lastClean = os.clock()
    lastPeriodic = os.clock()
end

local function gameReady()
    if isPlayerPlaying and not isPlayerPlaying(0) then return false end
    return true
end

local function menuActive()
    if isPauseMenuActive and isPauseMenuActive() then return true end
    return false
end

-- =================== MP: /memclean window ===================
local win = imgui and imgui.new.bool(false)
local lastManual = ""
local W = {}
local function manualClean()
    local b = streamMB()
    streamClean("manual", false, num(C().streaming.passes, 20) + 20)
    lastManual = string.format("Freed %.1f MB (%.1f -> %.1f MB)", b - streamMB(), b, streamMB())
end
if imgui then
    imgui.OnFrame(function() return win[0] end, function(self)
        self.HideCursor = false
        local sw, sh = getScreenResolution()
        imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.FirstUseEver, imgui.ImVec2(0.5, 0.5))
        imgui.SetNextWindowSize(imgui.ImVec2(480, 0), imgui.Cond.FirstUseEver)
        if imgui.Begin("AutoCleanStreaming", win) then
            local used, avail = streamMB(), streamAvailMB()
            imgui.Text(string.format("Streaming memory: %.1f / %.0f MB (%.0f%%)", used, avail, streamPct()))
            imgui.ProgressBar(math.max(0, math.min(1, streamPct() / 100)), imgui.ImVec2(-1, 0))
            if lateMode then imgui.TextDisabled("Long session mode is on") end
            local changed = false
            W.auto = W.auto or imgui.new.bool(false)
            W.auto[0] = num(C().streaming.autoClean, 1) == 1
            if imgui.Checkbox("Auto clean", W.auto) then C().streaming.autoClean = W.auto[0] and 1 or 0; changed = true end
            W.tr = W.tr or imgui.new.bool(false)
            W.tr[0] = num(C().streaming.trickle, 1) == 1
            if imgui.Checkbox("Gentle trickle cleaning", W.tr) then C().streaming.trickle = W.tr[0] and 1 or 0; changed = true end
            for _, d in ipairs({ { "streaming", "limitMB", "Limit (MB)", 100, 2000 }, { "streaming", "limitPercent", "Clean at pool %", 40, 99 },
                    { "streaming", "emergencyPercent", "Emergency at pool %", 50, 99 }, { "streaming", "periodicSec", "Maintenance every (s)", 0, 600 },
                    { "compat", "startupDelaySec", "Start delay (s)", 0, 120 } }) do
                local k = d[2]
                W[k] = W[k] or imgui.new.int(0)
                W[k][0] = math.floor(num(C()[d[1]][k], d[4]))
                if imgui.SliderInt(d[3], W[k], d[4], d[5]) then C()[d[1]][k] = W[k][0]; changed = true end
            end
            if imgui.Button("Clean now") then manualClean() end
            if lastManual ~= "" then imgui.SameLine(); imgui.Text(lastManual) end
            imgui.TextDisabled("/memclean  window   /memclean now  clean   /memclean on|off")
            if changed then inicfg.save(cfg, CFG_FILE) end
        end
        imgui.End()
    end)
end

function main()
    print("[AutoCleanStreaming] lua loaded (streaming side; ASI covers EXE).")
    if sampRegisterChatCommand then
        sampRegisterChatCommand("memclean", function(arg)
            arg = tostring(arg or ""):lower():gsub("%s", "")
            if arg == "now" or arg == "clean" then
                manualClean()
                if sampAddChatMessage then sampAddChatMessage("{9AD0FF}AutoClean:{FFFFFF} " .. lastManual, -1) end
            elseif arg == "on" or arg == "off" then
                C().streaming.autoClean = (arg == "on") and 1 or 0
                inicfg.save(cfg, CFG_FILE)
                if sampAddChatMessage then sampAddChatMessage("{9AD0FF}AutoClean:{FFFFFF} auto clean " .. arg, -1) end
            elseif win then
                win[0] = not win[0]
            end
        end)
    end
    if isGameLoaded ~= nil then
        while not isGameLoaded() do wait(500) end
    end
    lastCheck, lastClean, lastPeriodic, lastHb, lastCleanLog =
        os.clock(), os.clock() - 60, os.clock(), os.clock(), os.clock()

    while true do
        wait(num(C().streaming.checkMs, 500))
        local now = os.clock()

        if num(C().debug.logLevel, 1) >= 1 and num(C().debug.heartbeatSec, 60) > 0
            and now - lastHb >= num(C().debug.heartbeatSec, 60) then
            lastHb = now
            print(string.format("[AutoCleanStreaming] HB: STR %.1f/%dMB (%d%% pool) ready=%d menu=%d",
                streamMB(), num(C().streaming.limitMB, 1100), streamPct(),
                gameReady() and 1 or 0, menuActive() and 1 or 0))
        end

        if num(C().streaming.autoClean, 1) == 1 and gameReady() then
            local doClean = true
            if num(C().compat.startupDelaySec, 30) > 0
                and (os.clock() - bootTick) < num(C().compat.startupDelaySec, 30) then
                doClean = false
            end
            if doClean and num(C().compat.respectMenu, 1) == 1 and menuActive() then
                doClean = false
            end
            if doClean then
        if sessionStart == 0 then sessionStart = now end
        local late = num(C().streaming.escalateMin, 30) > 0
            and (now - sessionStart) >= num(C().streaming.escalateMin, 30) * 60
        if late and not lateMode then
            lateMode = true
            print("[AutoCleanStreaming] LONG SESSION mode on: passes x2, trickle x2, intervals /2.")
        end
        local passes = num(C().streaming.passes, 20)
        local trickleP = num(C().streaming.tricklePasses, 4)
        local minInt = num(C().streaming.minIntervalSec, 4)
        local periodic = num(C().streaming.periodicSec, 120)
        if lateMode then
            passes = math.min(passes * 2, 200)
            trickleP = math.min(trickleP * 2, 20)
            minInt = math.max(math.floor(minInt / 2), 1)
            periodic = math.max(math.floor(periodic / 2), 30)
        end

        local sMB = streamMB()
        local cap = num(C().streaming.limitMB, 1100)
        local avail = streamAvailMB()
        if avail > 0 and avail < 2047 then cap = math.min(cap, avail * 0.9) end

        if num(C().streaming.trickle, 1) == 1 then
            local soft = cap * num(C().streaming.softPercent, 70) / 100
            if sMB > soft then
                lruLoop(trickleP)
                sMB = streamMB()
            end
        end

        local sPct = streamPct()
        if sMB >= cap then
            if now - lastClean >= minInt then
                local emg = (sMB >= cap + 150)
                    or (sPct >= num(C().streaming.emergencyPercent, 92))
                streamClean(emg and "cap" or "thresh", emg, emg and (passes + 40) or passes)
            end
        elseif sPct >= num(C().streaming.emergencyPercent, 92) then
            streamClean("pool-urgent", true, passes + 40)
        elseif sPct >= num(C().streaming.limitPercent, 80) then
            if now - lastClean >= minInt then
                streamClean("pool-thresh", false, passes)
            end
        end

        if periodic > 0 and now - lastPeriodic >= periodic then
            local b0 = streamMB()
            lruLoop(15)
            local a0 = streamMB()
            if b0 ~= a0 then
                local t = string.format("Maintenance: %d->%d MB [periodic-light]", b0, a0)
                msg(t)
                logCleanLine(t, b0 - a0, false)
            end
            lastPeriodic = now
        end
            end -- doClean
        end -- autoClean and gameReady
    end -- while true
end -- main
