-- MP Auto Translate (MP 5.02.41)
-- 5.02.41: the translated line is private - it is added to YOUR chat box only (sampAddChatMessage), nothing is sent
-- to the server, nobody else sees it. Players' English chat ("hello" -> Indonesian "halo"), server lines and (new)
-- your own messages get one: "[TR] name: halo". /at test <text> shows a translation right away to check the setup.
-- /autotranslate (or /auto translate) opens a plain mimgui window - not an MP window - with the 20 languages of
-- files/TYFMC/MP/lang/. Press your language: English chat lines get a translated line under them, and (optional) MP's
-- own menus switch to the same language page (lang/<code>.lng). Translations come from Google Translate over the
-- internet (Wi-Fi or mobile data); nothing is sent when the window is set to "Off (English)".
script_name('MP Auto Translate')
script_author('JebetrexxMC')
script_version('1.1-mp')
script_description('/autotranslate - translate English chat into your language (20 languages)')

local imgui = require 'mimgui'
local new = imgui.new
local okE, effil = pcall(require, 'effil')
local okJ, json = pcall(require, 'cjson')
if not okJ then okJ, json = pcall(require, 'json') end
local okF, ffi = pcall(require, 'ffi')

-- MP code, English name, Google Translate code (same order as Settings > Language)
local LANGS = {
  { 'es', 'Spanish', 'es' }, { 'fil', 'Filipino', 'tl' }, { 'id', 'Indonesian', 'id' },
  { 'pt-BR', 'Portuguese (Brazil)', 'pt' }, { 'ar', 'Arabic', 'ar' }, { 'ko', 'Korean', 'ko' },
  { 'ru', 'Russian', 'ru' }, { 'zh-CN', 'Chinese (Simplified)', 'zh-CN' }, { 'vi', 'Vietnamese', 'vi' },
  { 'ja', 'Japanese', 'ja' }, { 'fr', 'French', 'fr' }, { 'de', 'German', 'de' },
  { 'it', 'Italian', 'it' }, { 'tr', 'Turkish', 'tr' }, { 'pl', 'Polish', 'pl' },
  { 'th', 'Thai', 'th' }, { 'ms', 'Malay', 'ms' }, { 'uk', 'Ukrainian', 'uk' },
  { 'nl', 'Dutch', 'nl' }, { 'zh-TW', 'Chinese (Traditional)', 'zh-TW' },
}

local CFG = getWorkingDirectory() .. '/config/autotranslate.txt'
local cfg = { lang = '', chat = true, mplang = true, players = true, mine = true }
local show = new.bool(false)
local bChat, bMpLang, bPlayers, bMine = new.bool(true), new.bool(true), new.bool(true), new.bool(true)
local cache, cacheN = {}, 0
local busy, queue = 0, {}
local MAX_BUSY = 3

local function loadCfg()
  local f = io.open(CFG, 'r'); if not f then return end
  for l in f:lines() do
    local k, v = l:match('^(%w+)=(.*)$')
    if k == 'lang' then cfg.lang = v
    elseif k then cfg[k] = (v == '1') end
  end
  f:close()
end
local function saveCfg()
  local f = io.open(CFG, 'w'); if not f then return end
  f:write('# MP Auto Translate (/autotranslate)\n')
  f:write(('lang=%s\nchat=%s\nmplang=%s\nplayers=%s\nmine=%s\n'):format(cfg.lang, cfg.chat and 1 or 0, cfg.mplang and 1 or 0, cfg.players and 1 or 0, cfg.mine and 1 or 0))
  f:close()
end

local function langOf(code) for _, L in ipairs(LANGS) do if L[1] == code then return L end end end
local function say(t) sampAddChatMessage('{38BDF8}[Auto Translate]{FFFFFF} ' .. t, -1) end

-- MP's own language page (lang/<code>.lng), applied by MP on the game thread
local mp
local function mpLib()
  if mp ~= nil then return mp or nil end
  mp = false
  if not okF then return nil end
  pcall(ffi.cdef, 'int MP_LangRequest(const char* code); const char* MP_LangCurrent(void);')
  local so = ffi.abi('64bit') and 'libMP64.so' or 'libMP32.so'
  local names = { so:sub(4, -4), so }
  local maps = io.open('/proc/self/maps', 'r')
  if maps then
    for l in maps:lines() do local p = l:match('(/%S*/' .. so:gsub('%.', '%%.') .. ')$') if p then table.insert(names, 1, p) break end end
    maps:close()
  end
  for _, n in ipairs(names) do
    local ok, lib = pcall(ffi.load, n)
    if ok and lib and pcall(function() return lib.MP_LangRequest end) then mp = lib break end
  end
  return mp or nil
end
local function setMpLang(code)
  local lib = mpLib()
  if lib then lib.MP_LangRequest(code) end
end

local function urlencode(s)
  return (s:gsub('[^%w%-_%.~]', function(c) return ('%%%02X'):format(c:byte()) end))
end
local function plain(t) return (t:gsub('{%x%x%x%x%x%x}', ''):gsub('%s+', ' '):gsub('^%s+', ''):gsub('%s+$', '')) end

-- one HTTP GET on its own thread (effil), so the game never waits for the internet
local function httpGet(url, done)
  if not okE then done(nil, 'effil is missing (moonloader/lib)') return end
  local th = effil.thread(function(u)
    local requests = require 'requests'
    local ok, r = pcall(requests.get, u, { headers = { ['User-Agent'] = 'Mozilla/5.0' }, timeout = 10 })
    if ok and r then return true, r.status_code, r.text end
    return false, 0, tostring(r)
  end)(url)
  lua_thread.create(function()
    while true do
      local st, err = th:status()
      if err then done(nil, tostring(err)) return end
      if st == 'completed' then
        local ok, code, body = th:get()
        if ok and code == 200 then done(body) else done(nil, ok and ('HTTP ' .. tostring(code)) or tostring(body)) end
        return
      elseif st == 'canceled' or st == 'failed' then done(nil, st) return end
      wait(50)
    end
  end)
end

local function translate(text, tl, cb)
  local key = tl .. '\1' .. text
  if cache[key] ~= nil then cb(cache[key] or nil) return end
  local url = 'https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl=' .. tl .. '&dt=t&q=' .. urlencode(text)
  httpGet(url, function(body, err)
    local out
    if body and okJ then
      local ok, d = pcall(json.decode, body)
      if ok and type(d) == 'table' and type(d[1]) == 'table' then
        local parts = {}
        for _, seg in ipairs(d[1]) do if type(seg) == 'table' and type(seg[1]) == 'string' then parts[#parts + 1] = seg[1] end end
        local src = type(d[3]) == 'string' and d[3] or ''
        out = table.concat(parts)
        -- already in that language, or nothing changed: no extra line
        if src:lower() == tl:lower() or src:sub(1, 2) == tl:sub(1, 2) or plain(out):lower() == text:lower() then out = false end
      end
    end
    if out == nil and err then print('[Auto Translate] ' .. err) end
    if cacheN > 400 then cache, cacheN = {}, 0 end
    cache[key] = out or false; cacheN = cacheN + 1
    cb(out or nil)
  end)
end

local function pump()
  while busy < MAX_BUSY and #queue > 0 do
    local job = table.remove(queue, 1)
    busy = busy + 1
    translate(job.text, job.tl, function(out)
      busy = busy - 1
      if out and out ~= '' then
        -- local only: this line is never sent, only you see it
        sampAddChatMessage('{9CA3AF}[TR] {FFFF00}' .. job.who .. '{E5E7EB}' .. out, -1)
      elseif job.test then
        say(out == nil and 'no translation (no internet, or it is already in that language).' or 'already in that language.')
      end
      pump()
    end)
  end
end

local function wantLine(t)
  if not cfg.chat or cfg.lang == '' then return false end
  if t:find('[Auto Translate]', 1, true) or t:find('[TR]', 1, true) then return false end
  if #t < 2 or #t > 400 then return false end
  return t:find('%a%a') ~= nil
end
local function push(text, who, test)
  local L = langOf(cfg.lang); if not L then return end
  local t = plain(text)
  if not test and not wantLine(t) then return end
  if #t < 1 then return end
  if #queue > 20 then table.remove(queue, 1) end
  queue[#queue + 1] = { text = t, tl = L[3], who = who or '', test = test }
  pump()
end
local function myName()
  local ok, r, id = pcall(sampGetPlayerIdByCharHandle, PLAYER_PED)
  if ok and r and id then local okn, n = pcall(sampGetPlayerNickname, id) if okn and n then return n end end
  return 'You'
end

local function pick(code)
  cfg.lang = code; saveCfg()
  local L = langOf(code)
  if L then
    if cfg.mplang then setMpLang(code) end
    say(('chat is translated into %s now. /autotranslate to change it.'):format(L[2]))
  else
    if cfg.mplang then setMpLang('en') end
    say('off - the chat stays in English.')
  end
end

-- the window (plain Dear ImGui)
imgui.OnInitialize(function() imgui.GetIO().IniFilename = nil end)
imgui.OnFrame(function() return show[0] end, function()
  local sw, sh = getScreenResolution()
  local s = math.max(1.0, sh / 720)
  imgui.SetNextWindowPos(imgui.ImVec2(sw / 2, sh / 2), imgui.Cond.Appearing, imgui.ImVec2(0.5, 0.5))
  imgui.SetNextWindowSize(imgui.ImVec2(math.min(sw * 0.9, 760 * s), math.min(sh * 0.9, 600 * s)), imgui.Cond.Appearing)
  if imgui.Begin('Auto Translate##mpat', show, imgui.WindowFlags.NoCollapse) then
    imgui.SetWindowFontScale(s)
    local cur = langOf(cfg.lang)
    imgui.Text('Translate from English into: ' .. (cur and cur[2] or 'Off (English)'))
    if imgui.Checkbox('Translate the chat', bChat) then cfg.chat = bChat[0]; saveCfg() end
    imgui.SameLine()
    if imgui.Checkbox("Players' chat too", bPlayers) then cfg.players = bPlayers[0]; saveCfg() end
    if imgui.Checkbox('My own messages too (only I see the line)', bMine) then cfg.mine = bMine[0]; saveCfg() end
    if imgui.Checkbox("Switch MP's menus to the same language", bMpLang) then cfg.mplang = bMpLang[0]; saveCfg() end
    imgui.Separator()
    local avail = imgui.GetContentRegionAvail().x
    local sp = imgui.GetStyle().ItemSpacing.x
    local bw, bh = (avail - sp) / 2, 44 * s
    if imgui.Button((cfg.lang == '' and '> ' or '') .. 'Off (English)', imgui.ImVec2(avail, bh)) then pick('') end
    for i, L in ipairs(LANGS) do
      local on = L[1] == cfg.lang
      if on then imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.15, 0.55, 0.30, 1.0)) end
      if imgui.Button((on and '> ' or '') .. L[2] .. ' (' .. L[1] .. ')##l' .. i, imgui.ImVec2(bw, bh)) then pick(L[1]) end
      if on then imgui.PopStyleColor() end
      if i % 2 == 1 then imgui.SameLine() end
    end
    imgui.Separator()
    imgui.TextDisabled('The [TR] lines are only in your chat box - nothing is sent, nobody else sees them.')
    imgui.TextDisabled('Check it: /at test hello. Uses Google Translate over the internet.')
    if imgui.Button('Close', imgui.ImVec2(avail, bh)) then show[0] = false end
  end
  imgui.End()
end)

local function open() show[0] = not show[0] end

function main()
  while not isSampLoaded() do wait(100) end
  loadCfg()
  bChat[0], bMpLang[0], bPlayers[0], bMine[0] = cfg.chat, cfg.mplang, cfg.players, cfg.mine
  local ok, ev = pcall(require, 'samp.events')
  if ok and ev then
    local prevS, prevC, prevO = ev.onServerMessage, ev.onChatMessage, ev.onSendChat
    ev.onSendChat = function(msg)
      if prevO then local r = prevO(msg) if r ~= nil then return r end end
      -- your own line goes to the server unchanged; the translation is only added to your chat box
      if cfg.mine and type(msg) == 'string' and msg:sub(1, 1) ~= '/' then push(msg, myName() .. ': ') end
    end
    ev.onServerMessage = function(color, text)
      if prevS then local r = prevS(color, text) if r ~= nil then return r end end
      push(text)
    end
    ev.onChatMessage = function(id, text)
      if prevC then local r = prevC(id, text) if r ~= nil then return r end end
      if cfg.players then
        -- my own line coming back from the server: already shown by onSendChat
        local okm, r, me = pcall(sampGetPlayerIdByCharHandle, PLAYER_PED)
        if cfg.mine and okm and r and me == id then return end
        local okn, name = pcall(sampGetPlayerNickname, id)
        push(text, okn and name and (name .. ': ') or '')
      end
    end
  else
    print('[Auto Translate] samp.events not found - chat is not translated (the window still switches MP\'s language)')
  end
  while not isSampAvailable() do wait(100) end
  sampRegisterChatCommand('autotranslate', open)
  sampRegisterChatCommand('auto', function(arg)
    if (arg or ''):lower():match('^%s*translate') then open() else say('type /autotranslate (or /auto translate)') end
  end)
  sampRegisterChatCommand('at', function(arg)
    local t = (arg or ''):match('^%s*test%s+(.+)$')
    if t then
      if cfg.lang == '' then say('pick a language first: /autotranslate') else push(t, 'test: ', true) end
    else open() end
  end)
  wait(-1)
end
