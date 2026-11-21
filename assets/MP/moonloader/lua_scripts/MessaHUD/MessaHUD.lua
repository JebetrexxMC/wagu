-- MessaHUD 1.2c-mp - Android / MonetLoader port of "mesahud" by hq~ (LibertyCity.Ru, v1.2c)
--
-- The original messahud.luac is a PC MoonLoader script: it draws with renderDrawBox / renderDrawTexture (d3d9),
-- moves the radar with radarrect.asi and is set up with the right mouse button. None of that exists on the phone, so
-- this is a rewrite with the same look: dark panels, white icons, an orange frame, the same pictures and sounds
-- (lua_scripts/MessaHUD/mesahud/). It is drawn with mimgui; the settings window is in MessaHUD's own style.
--
--   informer        FPS + ping box (movable)
--   time / date     clock icon, real time, date
--   nickname        card icon, your name in your colour, or your own text ("encrypted nick")
--   bars            health, armour, sprint, breath (+ car health in a car), each with its number on / off
--   weapon / money  weapon icon, ammo in clip / rest, money with dots ($100.007.004)
--   wanted          six stars
--   mini game       tap the target (perfect / godlike / holy shit sounds)
-- The game's own player info block (bars, weapon, money, clock, stars) is hidden while MessaHUD is on, through
-- libMP's MP_HudHideInfo (5.02.39); the weapon icon can still be tapped to switch weapons. The radar stays the game's.
--
-- Chat: /messahud           settings window
--       /messahud on|off    show / hide the HUD (kept)
--       /messahud move      move the HUD and the informer (drag them, tap Done)
--       /messahud game      mini game
-- Settings: moonloader/config/messahud.ini

script_name('MessaHUD')
script_author('hq~ (mesahud), MP port')
script_version('1.2c-mp')

local imgui = require 'mimgui'
local inicfg = require 'inicfg'
local ffi = require 'ffi'
local okWS, ws = pcall(require, 'mp_weaponslots')
if not okWS then ws = nil end

local CFG = 'messahud.ini'
local cfg = inicfg.load({
    main = { enabled = true, hidegame = true, scale = 1.0, font = 1, fontsize = 18, sounds = true,
        x = -1, y = -1, ix = -1, iy = -1 },
    show = { informer = true, time = true, nick = true, bars = true, weapon = true, money = true, wanted = true },
    num = { health = true, armour = true, sprint = false, breath = false, car = true },
    nick = { custom = false, text = 'hq322' },
    game = { best = 0 },
}, CFG)
local function save() inicfg.save(cfg, CFG) end
save()

-- ============================ files ============================
local function scriptDir()
    local src = debug.getinfo(1, 'S').source or ''
    local p = src:match('^@(.+)[/\\][^/\\]+$')
    if p and doesDirectoryExist and doesDirectoryExist(p .. '/mesahud') then return p end
    return getWorkingDirectory() .. '/lua_scripts/MessaHUD'
end
local DIR = scriptDir():gsub('\\', '/')
local RES = DIR .. '/mesahud'

local ICONS = { 'health', 'armour', 'sprint', 'breath', 'carhealth', 'clock', 'calendar', 'card', 'money', 'star',
    'informs', 'cogs', 'mgame', 'symbol', 'gps', 'border', 'size' }
local WEAPON_FILE = {
    [0] = 'fist', 'brassknuckle', 'golfclub', 'nitestick', 'knife', 'baseball', 'shovel', 'poolcue', 'katana',
    'chainsaw', 'dildo', 'dildo', 'dildo', 'dildo', 'flowers', 'cane', 'grenade', 'teargas', 'molotov',
    [22] = 'deagle', [23] = 'deagle', [24] = 'deagle', [25] = 'shotgun', [26] = 'sawnoff', [27] = 'shotgun',
    [28] = 'uzi', [29] = 'mp5', [30] = 'ak47', [31] = 'm4', [32] = 'tec9', [33] = 'sniper', [34] = 'sniper',
    [35] = 'rpg', [36] = 'rpg', [37] = 'flamethrower', [38] = 'minigun', [39] = 'detonate', [40] = 'detonate',
    [41] = 'spray', [42] = 'extinguisher', [43] = 'camera', [44] = 'vision', [45] = 'vision', [46] = 'parachute',
}
local tex, wtex = {}, {}
local FONTS = { { 'Default', nil }, { 'Sansation', 'sansation.ttf' }, { 'GTEestiProText', 'gteestiprotext.otf' } }
local fontPtr = {}

-- ============================ sounds ============================
local snd = {}
local function play(name)
    if not cfg.main.sounds or not loadAudioStream then return end
    pcall(function()
        if not snd[name] then snd[name] = loadAudioStream(RES .. '/sounds/' .. name .. '.wav') end
        if snd[name] then setAudioStreamState(snd[name], 1) end
    end)
end

-- ============================ libMP: hide the game's player info ============================
local mpHide
do
    local okc = pcall(ffi.cdef, 'int MP_HudHideInfo(int on);')
    local so = ffi.abi('64bit') and 'libMP64.so' or 'libMP32.so'
    local names = { so:sub(4, -4), so }
    local maps = io.open('/proc/self/maps', 'r')
    if maps then
        for l in maps:lines() do local p = l:match('(/%S*/' .. so:gsub('%.', '%%.') .. ')$') if p then names[#names + 1] = p break end end
        maps:close()
    end
    for _, n in ipairs(names) do
        local okl, lib = pcall(ffi.load, n)
        if okl and lib and pcall(function() return lib.MP_HudHideInfo end) then
            mpHide = function(on) return lib.MP_HudHideInfo(on and 1 or 0) ~= 0 end
            break
        end
    end
end
local hideState = nil
local function syncGameHud()
    local want = cfg.main.enabled and cfg.main.hidegame
    if want == hideState then return end
    hideState = want
    if mpHide then
        local ok = mpHide(want)
        if want and not ok then print('[MessaHUD] the game HUD can not be hidden (Advanced Settings owns it)') end
    end
end

-- ============================ game data ============================
local PD = nil   -- CWorld::Players[0]: CPlayerInfo { CPlayerPed* m_pPed; CPlayerData m_PlayerData; ... }
local P64 = ffi.abi('64bit')
local OFF_PED, OFF_RUN, OFF_BREATH = 0, P64 and (8 + 36) or (4 + 24), P64 and (8 + 88) or (4 + 68)
local maxRun, maxBreath = 1, 1
local function playerData()
    if PD == nil then
        PD = false
        if getModuleProcAddress then
            local ok, a = pcall(getModuleProcAddress, 'libGTASA.so', '_ZN6CWorld7PlayersE')
            if ok and a and a ~= 0 then PD = a end
        end
    end
    if not PD then return nil end
    local ok, ped = pcall(function() return tonumber(ffi.cast('uintptr_t*', PD + OFF_PED)[0]) end)
    if not ok or not getCharPointer or ped ~= getCharPointer(PLAYER_PED) then return nil end
    return PD
end
local function readF(a) local ok, v = pcall(function() return ffi.cast('float*', a)[0] end) if ok and v == v then return v end end

local function myId()
    if not sampGetPlayerIdByCharHandle then return nil end
    local ok, r, id = pcall(sampGetPlayerIdByCharHandle, PLAYER_PED)
    if ok and r then return id end
end

local function nickname()
    if cfg.nick.custom then return tostring(cfg.nick.text), 0xFFFFA500 end
    local id = myId()
    local name, col = 'Player', 0xFFFFFFFF
    if id and sampGetPlayerNickname then
        local ok, n = pcall(sampGetPlayerNickname, id); if ok and n and n ~= '' then name = n end
        local okc, c = pcall(sampGetPlayerColor, id); if okc and c and c ~= 0 then col = c end
    end
    return name, col
end

local function moneyText(m)
    local neg = m < 0; m = math.abs(math.floor(m))
    local s = tostring(m):reverse():gsub('(%d%d%d)', '%1.'):reverse():gsub('^%.', '')
    return (neg and '-$' or '$') .. s
end

-- ============================ drawing helpers ============================
local function col(r, g, b, a)
    return ((a or 255) % 256) * 0x1000000 + (b % 256) * 0x10000 + (g % 256) * 0x100 + (r % 256)
end
local function argb(c, a)   -- 0xAARRGGBB -> imgui
    local r, g, b = math.floor(c / 0x10000) % 256, math.floor(c / 0x100) % 256, c % 256
    return col(r, g, b, a or 255)
end
local C = {
    panel = col(14, 14, 14, 205), panel2 = col(24, 24, 24, 215), orange = col(255, 160, 0), white = col(255, 255, 255),
    dim = col(150, 150, 150), shadow = col(0, 0, 0, 200), empty = col(255, 255, 255, 40),
    health = col(196, 24, 24), armour = col(28, 104, 170), sprint = col(214, 138, 14), breath = col(102, 214, 186), car = col(110, 190, 60),
}
local V2 = imgui.ImVec2
local function font() return fontPtr[tonumber(cfg.main.font) or 1] end
local function text(dl, x, y, size, c, s, right)
    local f = font()
    if right then
        local w = f and f:CalcTextSizeA(size, 10000, 0, s).x or imgui.GetFont():CalcTextSizeA(size, 10000, 0, s).x
        x = x - w
    end
    if f then dl:AddTextFontPtr(f, size, V2(x + 1, y + 1), C.shadow, s); dl:AddTextFontPtr(f, size, V2(x, y), c, s)
    else dl:AddText(V2(x + 1, y + 1), C.shadow, s); dl:AddText(V2(x, y), c, s) end
end
local function icon(dl, name, x, y, size, c)
    local t = tex[name]
    if t then dl:AddImage(t, V2(x, y), V2(x + size, y + size), V2(0, 0), V2(1, 1), c or C.white) end
end
local function box(dl, x, y, w, h, c) dl:AddRectFilled(V2(x, y), V2(x + w, y + h), c or C.panel) end

-- ============================ state ============================
local win = imgui.new.bool(false)
local edit = false
local gameWin = imgui.new.bool(false)
local fps = 0
local hudW, hudH = 300, 300

local function S() local _, sh = getScreenResolution(); return sh / 1080 * (tonumber(cfg.main.scale) or 1) end
local function defaultPos()
    local sw, sh = getScreenResolution()
    local s = S()
    if tonumber(cfg.main.x) < 0 then cfg.main.x = math.floor(sw - 320 * s - sw * 0.02); cfg.main.y = math.floor(sh * 0.16) end
    if tonumber(cfg.main.ix) < 0 then cfg.main.ix = cfg.main.x; cfg.main.iy = math.max(4, cfg.main.y - 70 * s) end
end

-- ============================ the HUD ============================
local function drawInformer(dl, x, y, s)
    local fs = cfg.main.fontsize * s
    local id = myId()
    local ping = '-'
    if id and sampGetPlayerPing then local ok, p = pcall(sampGetPlayerPing, id); if ok and p then ping = tostring(p) end end
    local w, h = 92 * s, fs * 2 + 12 * s
    box(dl, x, y, w, h)
    dl:AddRect(V2(x, y), V2(x + w, y + h), C.orange, 0, 0, 2 * s)
    text(dl, x + 6 * s, y + 5 * s, fs, C.white, 'FPS:'); text(dl, x + 46 * s, y + 5 * s, fs, C.orange, tostring(math.floor(fps + 0.5)))
    text(dl, x + 6 * s, y + 6 * s + fs, fs, C.white, 'PING:'); text(dl, x + 54 * s, y + 6 * s + fs, fs, col(80, 220, 80), ping)
    return w, h
end

local function bar(dl, x, y, w, s, iconName, frac, c, num)
    local fs = cfg.main.fontsize * s
    local ih = 18 * s
    icon(dl, iconName, x + 4 * s, y + 2 * s, ih)
    local bx, bw = x + 30 * s, w - 30 * s - (num and 44 * s or 8 * s)
    local by, bh = y + 6 * s, 10 * s
    dl:AddRectFilled(V2(bx, by), V2(bx + bw, by + bh), C.empty)
    frac = math.max(0, math.min(1, frac or 0))
    if frac > 0 then dl:AddRectFilled(V2(bx, by), V2(bx + bw * frac, by + bh), c) end
    if num then text(dl, x + w - 6 * s, y + (22 * s - fs) * 0.5, fs, C.white, num, true) end
    return 22 * s
end

local function drawHud(dl, x0, y0, s)
    local ped = PLAYER_PED
    local W = 300 * s
    local fs = cfg.main.fontsize * s
    local gap = 4 * s
    local y = y0
    local row = 26 * s
    if cfg.show.time then
        box(dl, x0, y, W, row)
        icon(dl, 'clock', x0 + 6 * s, y + 4 * s, 18 * s)
        text(dl, x0 + 30 * s, y + (row - fs) * 0.5, fs, C.white, os.date('%H:%M'))
        text(dl, x0 + W - 8 * s, y + (row - fs) * 0.5, fs, C.white, os.date('%d.%m.%Y'), true)
        y = y + row + gap
    end
    if cfg.show.nick then
        box(dl, x0, y, W, row)
        icon(dl, 'card', x0 + 6 * s, y + 4 * s, 18 * s)
        local n, c = nickname()
        text(dl, x0 + W - 8 * s, y + (row - fs) * 0.5, fs, argb(c), n, true)
        y = y + row + gap
    end
    if cfg.show.bars then
        local inCar = isCharInAnyCar(ped)
        local rows = 4 + (inCar and 1 or 0)
        box(dl, x0, y, W, rows * 22 * s + 8 * s)
        local by = y + 4 * s
        local hp = getCharHealth(ped)
        by = by + bar(dl, x0, by, W, s, 'health', hp / 100, C.health, cfg.num.health and tostring(hp))
        local ar = getCharArmour(ped)
        by = by + bar(dl, x0, by, W, s, 'armour', ar / 100, C.armour, cfg.num.armour and tostring(ar))
        local pd = playerData()
        local run = pd and readF(pd + OFF_RUN)
        if run then maxRun = math.max(maxRun, run) end
        by = by + bar(dl, x0, by, W, s, 'sprint', run and run / maxRun or 1, C.sprint, cfg.num.sprint and (run and tostring(math.floor(run / maxRun * 100)) or '-'))
        local br = pd and readF(pd + OFF_BREATH)
        if br then maxBreath = math.max(maxBreath, br) end
        by = by + bar(dl, x0, by, W, s, 'breath', br and br / maxBreath or 1, C.breath, cfg.num.breath and (br and tostring(math.floor(br / maxBreath * 100)) or '-'))
        if inCar then
            local okV, car = pcall(storeCarCharIsInNoSave, ped)
            local ch = okV and car and getCarHealth(car) or 1000
            by = by + bar(dl, x0, by, W, s, 'carhealth', (ch - 250) / 750, C.car, cfg.num.car and tostring(math.floor(ch / 10)))
        end
        y = by + 4 * s + gap
    end
    if cfg.show.weapon or cfg.show.money then
        local h = 38 * s
        local half = (W - gap) / 2
        if cfg.show.weapon then
            local wx = x0
            local ww = cfg.show.money and half or W
            box(dl, wx, y, ww, h)
            local w = getCurrentCharWeapon(ped)
            local t = wtex[w] or wtex[0]
            if t then dl:AddImage(t, V2(wx + 6 * s, y + 4 * s), V2(wx + 36 * s, y + 34 * s), V2(0, 0), V2(1, 1), C.white) end
            if w >= 16 and w ~= 40 and w ~= 44 and w ~= 45 and w ~= 46 then
                local total = getAmmoInCharWeapon(ped, w) or 0
                local clip = ws and ws.clip(ped, w)
                local f2 = fs * 1.2
                if clip then
                    text(dl, wx + 44 * s, y + (h - f2) * 0.5, f2, C.dim, tostring(clip))
                    local cw = (font() or imgui.GetFont()):CalcTextSizeA(f2, 10000, 0, tostring(clip)).x
                    text(dl, wx + 44 * s + cw, y + (h - f2) * 0.5, f2, C.white, ' / ' .. tostring(math.max(0, total - clip)))
                else
                    text(dl, wx + 44 * s, y + (h - f2) * 0.5, f2, C.white, tostring(total))
                end
            end
        end
        if cfg.show.money then
            local mx = cfg.show.weapon and (x0 + half + gap) or x0
            local mw = cfg.show.weapon and half or W
            box(dl, mx, y, mw, h)
            local f2 = fs * 1.25
            text(dl, mx + mw - 8 * s, y + (h - f2) * 0.5, f2, C.white, moneyText(getPlayerMoney(PLAYER_HANDLE) or 0), true)
        end
        y = y + h + gap
    end
    if cfg.show.wanted then
        local h = 30 * s
        box(dl, x0, y, W, h)
        local okW, lvl = pcall(storeWantedLevel, PLAYER_HANDLE)
        lvl = okW and tonumber(lvl) or 0
        local st = 22 * s
        for i = 1, 6 do
            local sx = x0 + W - 8 * s - (7 - i) * (st + 2 * s)
            icon(dl, 'star', sx, y + 4 * s, st, i <= lvl and C.white or C.empty)
        end
        y = y + h + gap
    end
    return W, y - y0
end

local function hudVisible()
    if not cfg.main.enabled then return false end
    if isPauseMenuActive and isPauseMenuActive() then return false end
    if not doesCharExist(PLAYER_PED) then return false end
    if sampIsScoreboardOpen and select(2, pcall(sampIsScoreboardOpen)) == true then return false end
    return true
end

imgui.OnFrame(function() return hudVisible() or edit end, function(self)
    self.HideCursor = not edit
    fps = imgui.GetIO().Framerate or fps
    defaultPos()
    local s = S()
    local dl = imgui.GetBackgroundDrawList()
    if cfg.show.informer then drawInformer(dl, cfg.main.ix, cfg.main.iy, s) end
    hudW, hudH = drawHud(dl, cfg.main.x, cfg.main.y, s)
    if edit then
        -- drag handles: borderless windows over the HUD and the informer
        local fl = imgui.WindowFlags.NoTitleBar + imgui.WindowFlags.NoResize + imgui.WindowFlags.NoScrollbar + imgui.WindowFlags.NoSavedSettings + imgui.WindowFlags.NoCollapse
        local function handle(id, x, y, w, h)
            imgui.SetNextWindowPos(V2(x, y), imgui.Cond.Appearing)
            imgui.SetNextWindowSize(V2(w, h), imgui.Cond.Always)
            imgui.SetNextWindowBgAlpha(0.15)
            local nx, ny = x, y
            if imgui.Begin(id, nil, fl) then local p = imgui.GetWindowPos(); nx, ny = p.x, p.y end
            imgui.End()
            return math.floor(nx), math.floor(ny)
        end
        cfg.main.x, cfg.main.y = handle('##mh_hud', cfg.main.x, cfg.main.y, hudW, hudH)
        if cfg.show.informer then cfg.main.ix, cfg.main.iy = handle('##mh_inf', cfg.main.ix, cfg.main.iy, 92 * s, cfg.main.fontsize * s * 2 + 12 * s) end
        local sw = getScreenResolution()
        imgui.SetNextWindowPos(V2(sw / 2, 40 * s), imgui.Cond.Always, V2(0.5, 0))
        if imgui.Begin('##mh_done', nil, fl + imgui.WindowFlags.AlwaysAutoResize) then
            imgui.Text('Drag the HUD and the FPS box.')
            imgui.SameLine()
            if imgui.Button('Done', V2(120 * s, 0)) then edit = false; save(); play('click') end
        end
        imgui.End()
    end
end)

-- ============================ settings window (MessaHUD style) ============================
local V = {}
local function chk(label, tbl, key)
    V[label] = V[label] or imgui.new.bool(false)
    V[label][0] = tbl[key] and true or false
    if imgui.Checkbox(label, V[label]) then tbl[key] = V[label][0]; save(); play('click'); return true end
    return false
end
local function header(t)
    imgui.Spacing()
    imgui.TextColored(imgui.ImVec4(1.0, 0.63, 0.0, 1.0), t)
    imgui.Separator()
end
local nickBuf = imgui.new.char[64]()

imgui.OnFrame(function() return win[0] end, function(self)
    self.HideCursor = false
    local sw, sh = getScreenResolution()
    local s = sh / 1080
    imgui.SetNextWindowPos(V2(sw / 2, sh / 2), imgui.Cond.FirstUseEver, V2(0.5, 0.5))
    imgui.SetNextWindowSize(V2(640 * s, 720 * s), imgui.Cond.FirstUseEver)
    if imgui.Begin('MESSAHUD  v1.2c', win, imgui.WindowFlags.NoCollapse) then
        chk('Show MessaHUD', cfg.main, 'enabled')
        imgui.SameLine(320 * s)
        chk('Hide the game HUD', cfg.main, 'hidegame')
        if cfg.main.hidegame and not mpHide then imgui.TextDisabled('(needs MP 5.02.39+ libMP)') end

        header('HUD')
        chk('FPS and ping', cfg.show, 'informer'); imgui.SameLine(320 * s); chk('Time and date', cfg.show, 'time')
        chk('Nickname', cfg.show, 'nick'); imgui.SameLine(320 * s); chk('Bars', cfg.show, 'bars')
        chk('Weapon and ammo', cfg.show, 'weapon'); imgui.SameLine(320 * s); chk('Money', cfg.show, 'money')
        chk('Wanted stars', cfg.show, 'wanted')

        header('NUMBERS ON THE BARS')
        chk('Health', cfg.num, 'health'); imgui.SameLine(220 * s); chk('Armour', cfg.num, 'armour'); imgui.SameLine(420 * s); chk('Sprint', cfg.num, 'sprint')
        chk('Breath', cfg.num, 'breath'); imgui.SameLine(220 * s); chk('Car health', cfg.num, 'car')

        header('ENCRYPTED NICK')
        chk('Show my own text instead of the nickname', cfg.nick, 'custom')
        if cfg.nick.custom then
            if ffi.string(nickBuf) == '' then ffi.copy(nickBuf, tostring(cfg.nick.text):sub(1, 63)) end
            imgui.PushItemWidth(300 * s)
            if imgui.InputText('##nick', nickBuf, 64) then cfg.nick.text = ffi.string(nickBuf); save() end
            imgui.PopItemWidth()
        end

        header('FONT AND SIZE')
        for i, f in ipairs(FONTS) do
            if i > 1 then imgui.SameLine() end
            local on = tonumber(cfg.main.font) == i
            if on then imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(1.0, 0.63, 0.0, 0.85)) end
            if imgui.Button(f[1] .. '##f' .. i, V2(190 * s, 0)) then cfg.main.font = i; save(); play('click') end
            if on then imgui.PopStyleColor() end
        end
        V.fs = V.fs or imgui.new.int(18); V.fs[0] = tonumber(cfg.main.fontsize) or 18
        if imgui.SliderInt('Text size', V.fs, 12, 30) then cfg.main.fontsize = V.fs[0]; save() end
        V.sc = V.sc or imgui.new.float(1); V.sc[0] = tonumber(cfg.main.scale) or 1
        if imgui.SliderFloat('HUD size', V.sc, 0.5, 2.0, '%.2f') then cfg.main.scale = V.sc[0]; save() end

        header('POSITION')
        if imgui.Button('Move the HUD', V2(200 * s, 0)) then edit = true; win[0] = false; play('click') end
        imgui.SameLine()
        if imgui.Button('Reset position', V2(200 * s, 0)) then cfg.main.x, cfg.main.y, cfg.main.ix, cfg.main.iy = -1, -1, -1, -1; save(); play('click') end

        header('OTHER')
        chk('Sounds', cfg.main, 'sounds')
        imgui.SameLine(320 * s)
        if imgui.Button('Mini game', V2(200 * s, 0)) then gameWin[0] = true; win[0] = false; play('click') end
        imgui.Spacing()
        imgui.TextDisabled('mesahud by hq~ (LibertyCity.Ru) - Android port for MP. Radar settings are PC only (radarrect.asi).')
    end
    imgui.End()
end)

-- ============================ mini game ============================
local G = { x = 0.5, y = 0.5, born = 0, life = 1.6, score = 0, streak = 0, miss = 0 }
local function newTarget()
    G.x, G.y = 0.1 + math.random() * 0.8, 0.15 + math.random() * 0.7
    G.born = os.clock()
    G.life = math.max(0.6, 1.6 - G.score * 0.02)
end
imgui.OnFrame(function() return gameWin[0] end, function(self)
    self.HideCursor = false
    local sw, sh = getScreenResolution()
    local s = sh / 1080
    imgui.SetNextWindowPos(V2(sw / 2, sh / 2), imgui.Cond.FirstUseEver, V2(0.5, 0.5))
    imgui.SetNextWindowSize(V2(620 * s, 520 * s), imgui.Cond.FirstUseEver)
    if imgui.Begin('MESSAHUD  mini game', gameWin, imgui.WindowFlags.NoCollapse) then
        imgui.Text(string.format('Clicks: %d   Streak: %d   Missed: %d   Best: %d', G.score, G.streak, G.miss, tonumber(cfg.game.best) or 0))
        imgui.SameLine()
        if imgui.SmallButton('Restart') then G.score, G.streak, G.miss = 0, 0, 0; newTarget() end
        local p = imgui.GetCursorScreenPos()
        local av = imgui.GetContentRegionAvail()
        local dl = imgui.GetWindowDrawList()
        dl:AddRectFilled(p, V2(p.x + av.x, p.y + av.y), C.panel2)
        dl:AddRect(p, V2(p.x + av.x, p.y + av.y), C.orange, 0, 0, 2)
        if G.born == 0 then newTarget() end
        local age = os.clock() - G.born
        if age > G.life then G.miss = G.miss + 1; G.streak = 0; newTarget(); age = 0 end
        local r = (34 - 14 * (age / G.life)) * s
        local cx, cy = p.x + av.x * G.x, p.y + av.y * G.y
        imgui.SetCursorScreenPos(V2(cx - r, cy - r))
        if imgui.InvisibleButton('##target', V2(r * 2, r * 2)) then
            G.score = G.score + 1; G.streak = G.streak + 1
            if G.score > (tonumber(cfg.game.best) or 0) then cfg.game.best = G.score; save() end
            if G.streak == 10 then play('perfect') elseif G.streak == 25 then play('mod_godlike') elseif G.streak == 50 then play('holyshit') else play('click') end
            newTarget()
        end
        dl:AddCircleFilled(V2(cx, cy), r, C.orange, 32)
        dl:AddCircle(V2(cx, cy), r * 0.55, C.white, 32, 2)
        if tex.mgame then dl:AddImage(tex.mgame, V2(cx - r * 0.4, cy - r * 0.4), V2(cx + r * 0.4, cy + r * 0.4), V2(0, 0), V2(1, 1), C.white) end
    end
    imgui.End()
end)

-- ============================ init: textures, fonts, style ============================
imgui.OnInitialize(function()
    for _, n in ipairs(ICONS) do
        local p = RES .. '/images/' .. n .. '.png'
        if doesFileExist(p) then local ok, t = pcall(imgui.CreateTextureFromFile, p); if ok then tex[n] = t end end
    end
    local cache = {}
    for id, n in pairs(WEAPON_FILE) do
        if not cache[n] then
            local p = RES .. '/images/weapons/' .. n .. '.png'
            if doesFileExist(p) then local ok, t = pcall(imgui.CreateTextureFromFile, p); if ok then cache[n] = t end end
        end
        wtex[id] = cache[n]
    end
    pcall(function()
        local io = imgui.GetIO()
        local fonts = io.Fonts
        local ranges = fonts:GetGlyphRangesCyrillic()
        for i, f in ipairs(FONTS) do
            if f[2] then
                local p = RES .. '/fonts/' .. f[2]
                if doesFileExist(p) then fontPtr[i] = fonts:AddFontFromFileTTF(p, 40.0, nil, ranges) end
            end
        end
    end)
    -- MessaHUD style: black panels, orange accent, square corners
    pcall(function()
        local st = imgui.GetStyle()
        local c = st.Colors
        local V4 = imgui.ImVec4
        local O = function(a) return V4(1.0, 0.63, 0.0, a) end
        st.WindowRounding, st.FrameRounding, st.GrabRounding, st.ScrollbarRounding, st.ChildRounding = 0, 0, 0, 0, 0
        st.WindowBorderSize, st.FrameBorderSize = 2, 0
        st.WindowPadding = V2(14, 12); st.FramePadding = V2(10, 6); st.ItemSpacing = V2(10, 8)
        st.WindowTitleAlign = V2(0.5, 0.5)
        local s = function(k, v) if imgui.Col[k] then c[imgui.Col[k]] = v end end
        s('WindowBg', V4(0.055, 0.055, 0.055, 0.96)); s('ChildBg', V4(0.08, 0.08, 0.08, 0.9)); s('PopupBg', V4(0.06, 0.06, 0.06, 0.97))
        s('Border', O(1.0)); s('TitleBg', V4(0.04, 0.04, 0.04, 1)); s('TitleBgActive', V4(0.04, 0.04, 0.04, 1)); s('TitleBgCollapsed', V4(0.04, 0.04, 0.04, 1))
        s('Text', V4(1, 1, 1, 1)); s('TextDisabled', V4(0.55, 0.55, 0.55, 1))
        s('FrameBg', V4(0.14, 0.14, 0.14, 1)); s('FrameBgHovered', V4(0.2, 0.2, 0.2, 1)); s('FrameBgActive', O(0.35))
        s('CheckMark', O(1.0)); s('SliderGrab', O(0.9)); s('SliderGrabActive', O(1.0))
        s('Button', V4(0.16, 0.16, 0.16, 1)); s('ButtonHovered', O(0.55)); s('ButtonActive', O(0.85))
        s('Header', O(0.4)); s('HeaderHovered', O(0.55)); s('HeaderActive', O(0.8))
        s('Separator', O(0.5)); s('ScrollbarGrab', O(0.5)); s('ScrollbarGrabHovered', O(0.7)); s('ScrollbarGrabActive', O(0.9))
        s('ResizeGrip', O(0.3)); s('ResizeGripHovered', O(0.6)); s('ResizeGripActive', O(0.9))
    end)
end)

-- ============================ commands ============================
local function say(t) sampAddChatMessage('{FFA000}[MessaHUD]{FFFFFF} ' .. t, -1) end

function main()
    while not isPlayerPlaying(PLAYER_HANDLE) do wait(250) end
    math.randomseed(os.time())
    sampRegisterChatCommand('messahud', function(arg)
        arg = tostring(arg or ''):lower():match('^%s*(%S*)')
        if arg == 'on' or arg == 'off' then
            cfg.main.enabled = (arg == 'on'); save(); say('HUD ' .. arg)
        elseif arg == 'move' then
            cfg.main.enabled = true; edit = true; win[0] = false
        elseif arg == 'game' then
            gameWin[0] = not gameWin[0]
        else
            win[0] = not win[0]
        end
    end)
    while true do
        wait(250)
        syncGameHud()
    end
end

function onScriptTerminate(s)
    if s == thisScript() and mpHide and hideState then pcall(mpHide, false) end
end
