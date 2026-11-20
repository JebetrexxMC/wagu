script_name('MP Self Test')
script_author('JebetrexxMC')
script_version('1.0-mp')
script_description('MonetLoader for MP 4.0.0-mp: tests the PC compatibility layer and PC MoonImGui (MP 5.01.00)')

-- HOW TO USE: copy this file to Android/data/com.rockstargames.gtasa/files/TYFMC/MP/moonloader/ (next to
-- mods_loader.lua), start the game and load into the world (offline is fine). After ~10 s the results are in
--   moonloader/mp_selftest.txt   (and moonloader.log)
-- A test window opens too (PC imgui API): tick the box, type, pick in the combo, then /mptest closes or opens it.
-- Every test runs in pcall: a FAIL line is a bug report, a crash means the last line written before it is the culprit.

local LOG = getWorkingDirectory() .. '/mp_selftest.txt'
local fh = io.open(LOG, 'w')
local pass, fail = 0, 0
local function out(s)
  print('[selftest] ' .. s)
  if fh then fh:write(s, '\n'); fh:flush() end
end
local function test(name, fn)
  out('RUN  ' .. name)   -- written before the test: if the game crashes, this is the last line
  local ok, res, info = pcall(fn)
  if ok and res then pass = pass + 1; out('PASS ' .. name .. (info and ('  (' .. tostring(info) .. ')') or ''))
  else fail = fail + 1; out('FAIL ' .. name .. '  ' .. tostring(ok and (info or 'returned false') or res)) end
end
local function hex(v) return v and string.format('0x%X', v) or 'nil' end

out(('MP self test 1.0-mp  MONET_VERSION=%s  %s'):format(tostring(MONET_VERSION), os.date()))
out('ABI: ' .. (require('ffi').abi('64bit') and 'arm64-v8a' or 'armeabi-v7a'))

-- ---------------------------------------------------------------------------------------------- pccompat
local function pccompat_tests()
  local pc
  test('require pccompat', function() pc = require 'pccompat'; return pc ~= nil end)
  if not pc then return end
  test('address map loaded + resolved in libGTASA.so', function()
    local s = pc.stats(); return s.entries > 6000 and s.resolved > 0, ('%d entries, %d resolved'):format(s.entries, s.resolved)
  end)
  test('name(0x56E210) = FindPlayerPed', function() local n = pc.name(0x56E210); return n == 'FindPlayerPed', n end)
  test('translate FindPlayerPed', function() local a = pc.translate(0x56E210, 0); return a ~= nil and a ~= 0, hex(a) end)
  test('resolve("CTimer::m_snTimeInMilliseconds")', function() local a = pc.resolve('CTimer::m_snTimeInMilliseconds'); return a ~= 0, hex(a) end)
  test('unknown PC address -> nil, no crash', function() local a = pc.translate(0x401234, 4); return a == nil, 'logged once in moonloader.log' end)

  test('readMemory PC CTimer::m_snTimeInMilliseconds runs', function()
    local t1 = readMemory(0xB7CB84, 4, false); wait(600); local t2 = readMemory(0xB7CB84, 4, false)
    return t2 > t1, ('%d -> %d'):format(t1, t2)
  end)
  test('readMemory PC CClock hours = getTimeOfDay', function()
    local h = readMemory(0xB70153, 1, false); local gh = getTimeOfDay()
    return h == gh, ('memory %d, opcode %d'):format(h, gh)
  end)
  test('memory.lua with a PC address', function()
    local memory = require 'memory'; local v = memory.getuint32(0xB7CB84)
    return v and v > 0, tostring(v)
  end)
  test('callFunction FindPlayerPed(-1) = getCharPointer(PLAYER_PED)', function()
    local p = callFunction(0x56E210, 1, 1, -1); local q = getCharPointer(PLAYER_PED)
    return p ~= 0 and p == q, ('%s / %s'):format(hex(p), hex(q))
  end)
  test('getModuleHandle gta_sa.exe = 0x400000', function() return getModuleHandle('gta_sa.exe') == 0x400000 end)
  test('getModuleHandle libGTASA.so', function() local h = getModuleHandle('libGTASA.so'); return h ~= 0, hex(h) end)
  test('getModuleProcAddress(libGTASA.so, _Z13FindPlayerPedi)', function()
    local a = getModuleProcAddress('libGTASA.so', '_Z13FindPlayerPedi'); return a ~= 0, hex(a)
  end)
  test('loadDynamicLibrary missing .dll -> false, no crash', function()
    local ok = loadDynamicLibrary('mp_selftest_missing.dll'); return ok == false
  end)
  test('ffi.load("kernel32.dll") -> C library', function()
    local ffi = require 'ffi'; local l = ffi.load('kernel32.dll'); return l ~= nil
  end)
  test('ffi.load Windows path + .asi -> searched, clean error', function()
    local ffi = require 'ffi'; local ok, err = pcall(ffi.load, 'C:\\Games\\GTA San Andreas\\mp_missing.asi')
    return not ok, tostring(err):sub(1, 80)
  end)
  test('isKeyDown / wasKeyPressed', function() return type(isKeyDown(0x41)) == 'boolean' and type(wasKeyPressed(0x41)) == 'boolean' end)
  test('getGameGlobal(0) readable', function() local v = getGameGlobal(0); return type(v) == 'number', tostring(v) end)
end

-- ---------------------------------------------------------------------------------------------- native libs
local function lib_tests()
  test('lpeg', function() local lpeg = require 'lpeg'; return lpeg.match(lpeg.C(lpeg.R('az')^1), 'hello1') == 'hello' end)
  test('re', function() local re = require 're'; return re.match('abc123', '{[a-z]+}') == 'abc' end)
  test('rex_pcre2', function() local rex = require 'rex_pcre2'; return rex.match('MP 5.01.00', '(\\d+)\\.(\\d+)') == '5' end)
  test('rex_posix', function() local rex = require 'rex_posix'; return rex.find('abc', 'b') == 2 end)
  test('luasql.sqlite3', function()
    local drv = require 'luasql.sqlite3'; local env = drv.sqlite3(); local con = env:connect(':memory:')
    con:execute('create table t(x)'); con:execute('insert into t values(42)')
    local cur = con:execute('select x from t'); local x = cur:fetch(); cur:close(); con:close(); env:close()
    return tonumber(x) == 42
  end)
  test('lanes', function()
    local lanes = require('lanes').configure(); local h = lanes.gen('*', function(a) return a * 2 end)(21)
    return h[1] == 42
  end)
  test('xml', function() local xml = require 'xml'; local t = xml.load('<a><b>hi</b></a>'); return t ~= nil end)
  test('pure Lua libs', function()
    for _, m in ipairs({ 'pl.stringx', 'inspect', 'lume', 'lustache', 'copas', 'xml2lua', 'htmlparser', 'samp.events', 'cjson', 'lfs', 'effil', 'socket', 'ssl', 'base64', 'encoding', 'inicfg', 'vkeys' }) do
      local ok, e = pcall(require, m); if not ok then return false, m .. ': ' .. tostring(e):sub(1, 80) end
    end
    return true
  end)
end

-- ---------------------------------------------------------------------------------------------- PC imgui
local imgui, frames = nil, 0
local check, num, buf, combo, slider, radio, color, open
local function imgui_tests()
  test('require imgui (PC MoonImGui on mimgui)', function() imgui = require 'imgui'; return imgui ~= nil, imgui._VERSION end)
  if not imgui then return end
  test('ImBool / ImInt / ImFloat / ImBuffer .v', function()
    check = imgui.ImBool(true); num = imgui.ImInt(3); slider = imgui.ImFloat(0.5); buf = imgui.ImBuffer('MP', 64)
    combo = imgui.ImInt(0); radio = imgui.ImInt(1); color = imgui.ImFloat4(1, 0.5, 0, 1); open = imgui.ImBool(true)
    buf.v = buf.v .. ' test'; num.v = num.v + 1
    return check.v == true and num.v == 4 and buf.v == 'MP test' and color.v[2] == 0.5
  end)
  function imgui.OnDrawFrame()
    frames = frames + 1
    imgui.SetNextWindowSize(imgui.ImVec2(520, 420), imgui.Cond.FirstUseEver)
    imgui.Begin('MP self test (PC imgui)', open)
    imgui.Text('frames: %d   /mptest closes this', frames)
    imgui.Checkbox('Checkbox', check)
    imgui.InputText('InputText', buf)
    imgui.InputInt('InputInt', num)
    imgui.SliderFloat('SliderFloat', slider, 0, 1)
    imgui.Combo('Combo', combo, { 'San Andreas', 'Vice City', 'LAN' })
    imgui.RadioButton('A', radio, 0); imgui.SameLine(); imgui.RadioButton('B', radio, 1)
    imgui.ColorEdit4('ColorEdit4', color)
    if imgui.Button('Close') then open.v = false end
    imgui.End()
    if not open.v then imgui.Process = false end
  end
  imgui.Process = true
end

function main()
  while not isPlayerPlaying(PLAYER_HANDLE) do wait(200) end
  wait(2000)
  pccompat_tests()
  lib_tests()
  imgui_tests()
  wait(3000)
  test('imgui.OnDrawFrame ran', function() return frames > 0, frames .. ' frames' end)
  out(('DONE  %d passed, %d failed  -> %s'):format(pass, fail, LOG))
  if fh then fh:close(); fh = nil end
  pcall(sampAddChatMessage, ('MP self test: %d passed, %d failed (moonloader/mp_selftest.txt)'):format(pass, fail), -1)
  pcall(sampRegisterChatCommand, 'mptest', function() if imgui then open.v = true; imgui.Process = not imgui.Process end end)
  wait(-1)
end
