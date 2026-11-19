-- MP: host test of assets/MP/moonloader/lua_scripts_loader.lua, lib/mp_zip.lua and lib/mp_pcpaths.lua.
-- Run from the repo root:  luajit test/lua_scripts_loader_test.lua   (needs lfs: apt install lua-filesystem)
-- MonetLoader is mocked (script.list/load, wait, getWorkingDirectory ...). A copy of assets/MP/moonloader/lua_scripts
-- plus a data-pack zip, a loose script, a zip with the script at the top and a cheat is scanned; then every runner
-- is run in its own environment to check that PC '\\' paths reach the files.
package.cpath = package.cpath .. ';/usr/lib/x86_64-linux-gnu/lua/5.1/?.so;/usr/local/lib/lua/5.1/?.so'
local lfs = require 'lfs'
local REPO = lfs.currentdir()
local ML = REPO .. '/assets/MP/moonloader'
package.path = ML .. '/lib/?.lua;' .. package.path

local pass, fail = 0, 0
local function check(name, ok, extra)
  if ok then pass = pass + 1; print('PASS ' .. name) else fail = fail + 1; print('FAIL ' .. name .. (extra and (' - ' .. tostring(extra)) or '')) end
end

local TMP = os.tmpname(); os.remove(TMP); lfs.mkdir(TMP)
local function sh(c) assert(os.execute(c) == 0 or true) end
sh(('cp -r %q %q'):format(ML .. '/lua_scripts', TMP .. '/lua_scripts'))
lfs.mkdir(TMP .. '/lib'); lfs.mkdir(TMP .. '/mods')
sh(('cp %q %q %q'):format(ML .. '/lib/mp_zip.lua', ML .. '/lib/mp_pcpaths.lua', TMP .. '/lib/'))
-- extra cases: a loose script, a zip with the script at its top + a data folder, a data pack, a cheat, a lib-only file
local function write(p, s) local f = assert(io.open(p, 'wb')) f:write(s) f:close() end
write(TMP .. '/lua_scripts/hello.lua', "script_name('hello')\nfunction main() end\n")
write(TMP .. '/lua_scripts/helper_module.lua', "local M = {} return M\n")   -- not a script
write(TMP .. '/lua_scripts/warp.lua', "-- MP_CHEAT_MOD\nscript_name('warp')\nfunction main() end\n")
lfs.mkdir(TMP .. '/z1'); lfs.mkdir(TMP .. '/z1/radio')
write(TMP .. '/z1/radio.lua', "script_name('radio')\nfunction main() local f = io.open(getWorkingDirectory()..'\\\\radio\\\\list.txt') assert(f, 'list') f:close() end\n")
write(TMP .. '/z1/radio/list.txt', 'x')
write(TMP .. '/z1/evil.txt', 'x')
sh(('cd %q && zip -qr %q radio.lua radio'):format(TMP .. '/z1', TMP .. '/lua_scripts/radio.zip'))
sh(('cp %q %q'):format(REPO .. '/Monet loader/tvplayer.zip', TMP .. '/lua_scripts/tvplayer.zip'))
-- a zip-slip entry must never be written outside .unpacked
sh(('cd %q && python3 -c "import zipfile;z=zipfile.ZipFile(\'%s\',\'w\');z.writestr(\'../../escaped.txt\',\'x\');z.writestr(\'ok.lua\',\'function main() end\');z.close()"'):format(TMP, TMP .. '/lua_scripts/slip.zip'))

-- ---- MonetLoader mock
local running, loads = {}, {}
local function mk(path) local s = { path = path, name = path, dead = false } function s:unload() self.dead = true running[self.path] = nil end return s end
script = {
  list = function() local t = {} for _, s in pairs(running) do t[#t + 1] = s end return t end,
  load = function(p) loads[#loads + 1] = p; running[p] = mk(p) end,
}
function getWorkingDirectory() return TMP end
function script_name() end function script_author() end function script_version() end function script_description() end
local chat = {}
function sampAddChatMessage(t) chat[#chat + 1] = t end
local cmds = {}
function sampRegisterChatCommand(n, f) cmds[n] = f end
local _print = print
print = function() end
package.preload.ffi = function() return { cdef = function() end, abi = function() return true end, load = function() error('no MP') end } end
local real_ffi_ok = true

dofile(ML .. '/lua_scripts_loader.lua')
local co = coroutine.create(main)
function wait() coroutine.yield() end
local ok, err = coroutine.resume(co)
print = _print
check('loader main runs', ok, err)

local function loaded(sub) for _, p in ipairs(loads) do if p:find(sub, 1, true) then return p end end end
check('loose script started', loaded('/lua_scripts/hello.lua'))
check('non-script module not started', not loaded('helper_module'))
check('cheat not started offline (no MP)', not loaded('warp.lua'))
check('moonloader-scripts collection started (AutoLock)', loaded('moonloader-scripts/arizona/AutoLock.lua'))
check('PC package moonloader/ folder (mphelper)', loaded('mphelper/moonloader/mphelper.lua') or loaded('mphelper_moonloader_mphelper'))
check('templates/ disabled', not loaded('/templates/'))
check('bb.lua disabled', not loaded('bb.lua'))
check('clickwarp is a cheat', not loaded('clickwarp'))
check('hitmarker.zip unpacked', lfs.attributes(TMP .. '/lua_scripts/.unpacked/hitmarker/moonloader/hitmarker.lua'))
local hm = loaded('hitmarker')
check('hitmarker started from the zip (through the PC-path runner)', hm and hm:find('/.run/', 1, true), hm)
check('hitmarker resources copied to moonloader/resources/hitmarker', lfs.attributes(TMP .. '/resources/hitmarker/hitmarker.png'))
check('mphelper config copied', lfs.attributes(TMP .. '/config/mphelper.ini'))
check('zip with script at top started', loaded('radio'))
check('zip data folder named like the script copied', lfs.attributes(TMP .. '/radio/list.txt'))
check('data pack (tvplayer.zip) copied', lfs.attributes(TMP .. '/tvplayer/config/tvplayer.ini') and lfs.attributes(TMP .. '/tvplayer/videos/tvplayer.mp4'))
check('zip-slip entry not written', not lfs.attributes(TMP .. '/escaped.txt') and not lfs.attributes(TMP .. '/../escaped.txt'))
check('safe entry of the same zip started', loaded('slip/ok.lua') or loaded('.unpacked/slip/ok.lua'))

-- no double start on the next rescans
local n = #loads
for _ = 1, 12 do coroutine.resume(co) end
check('no script started twice', #loads == n, #loads .. ' vs ' .. n)
-- a new file dropped in is picked up
write(TMP .. '/lua_scripts/late.lua', "script_name('late') function main() end\n")
local t0 = os.time()
while os.time() - t0 < 7 and not loaded('late.lua') do coroutine.resume(co) end
check('new file picked up by the rescan', loaded('late.lua'))
-- an existing settings file is never overwritten
write(TMP .. '/config/mphelper.ini', 'mine')
cmds.luascripts('rescan')
check('existing config kept', io.open(TMP .. '/config/mphelper.ini'):read('*a') == 'mine')

-- ---- run the runners: mp_pcpaths turns '\\' into '/'
local function runScript(path)
  local env = setmetatable({}, { __index = _G })
  env._G = env
  env.getWorkingDirectory = function() return TMP end
  env.doesFileExist = function(p) return lfs.attributes(p) ~= nil end
  local function copy(t) local r = {} for k, v in pairs(t) do r[k] = v end return r end
  env.io = copy(io); env.os = copy(os)
  env.require = function(m)
    if m == 'mp_pcpaths' then
      local f = assert(loadfile(TMP .. '/lib/mp_pcpaths.lua')); setfenv(f, env); return f()
    end
    return require(m)
  end
  env.loadfile = function(p) local f, e = loadfile(p) if f then setfenv(f, env) end return f, e end
  env.script_name = function() end
  local f = assert(loadfile(path)); setfenv(f, env)
  f()
  return env
end
local radio = loaded('radio')
check('radio runs through a runner', radio:find('/.run/', 1, true), radio)
local env = runScript(radio)
local okm, em = pcall(env.main)
check("PC path getWorkingDirectory()..'\\\\radio\\\\list.txt' opens", okm, em)
check('mp_pcpaths.fix', require('mp_pcpaths').fix('C/a\\b\\\\c.png') == 'C/a/b/c.png' and require('mp_pcpaths').fix('http://x\\y') == 'http://x\\y')

-- pure-Lua inflate == zlib
local zip = require 'mp_zip'
local z = zip.open(REPO .. '/Monet loader/monetloader.zip')
local same, total = 0, 0
for _, e in ipairs(z.entries) do
  if e.method == 8 and e.size < 200000 then
    total = total + 1
    local f = z.f
    f:seek('set', e.offset); local h = f:read(30)
    f:seek('set', e.offset + 30 + h:byte(27) + h:byte(28) * 256 + h:byte(29) + h:byte(30) * 256)
    local raw = f:read(e.csize)
    local okp, d = pcall(zip._lua_inflate, raw, e.size)
    if okp and d == z:read(e) then same = same + 1 end
  end
end
check(('pure-Lua inflate matches zlib (%d entries)'):format(total), same == total and total > 0, same .. '/' .. total)

sh(('rm -rf %q'):format(TMP))
print(('%d passed, %d failed'):format(pass, fail))
os.exit(fail == 0 and 0 or 1)
