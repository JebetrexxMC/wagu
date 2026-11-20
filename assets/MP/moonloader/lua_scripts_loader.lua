script_name('MP Lua Scripts Loader')
script_author('JebetrexxMC')
script_version('1.2-mp')
script_description('Starts every .lua / .luac script in moonloader/lua_scripts/, loose or inside .zip files')

-- Entry point of moonloader/lua_scripts/. MonetLoader only starts the scripts right in moonloader/; this one starts:
--
--   * every .lua / .luac in lua_scripts/ and its sub folders (the moonloader-scripts collection is in
--     lua_scripts/moonloader-scripts/), the way PC MoonLoader packages are laid out;
--   * every .zip in lua_scripts/ (and sub folders): it is unpacked to lua_scripts/.unpacked/<zip name>/ (again only
--     when the zip changes) and its scripts are started from there. A zip may hold the scripts at the top, in one
--     folder, or in a "moonloader/" (or "monetloader/") folder like the PC packages - hitmarker.zip is one of those.
--     Windows-only files (.dll / .asi / .exe, MoonLoader's own luajit/, luarocks/, libstd/) are left out.
--
-- A package's lib/, config/, resource/ and resources/ folders, and a data folder named like one of its scripts
-- (tvplayer/ next to tvplayer.lua), are copied into moonloader/ - only files that are not there yet, so MP's own
-- libraries and your settings are never overwritten. A zip with no script in it (a data pack such as tvplayer.zip)
-- is copied into moonloader/ as a whole.
--
-- PC scripts that build paths with '\\' get them turned into '/' (lib/mp_pcpaths.lua): such a script is started
-- through a small runner in lua_scripts/.run/ that loads mp_pcpaths first and then the real file.
--
-- lua_scripts/disabled.txt: one name per line (file name, path inside lua_scripts/, or a folder ending in '/') is
-- not started. Cheat scripts (a "MP_CHEAT_MOD" line, or one of CHEATS below) follow MP's CheatMenu rule, the same as
-- mods_loader.lua: offline and on custom servers whose owner allows MonetLoader cheats only.
-- New files dropped in lua_scripts/ are picked up within 5 seconds. Chat command: /luascripts (list, rescan).

local DIR_NAME = 'lua_scripts'
local RESCAN_MS = 5000

local DATA_DIRS = { lib = true, config = true, resource = true, resources = true }
local SKIP_DIRS = { luajit = true, luarocks = true, libstd = true, ['.git'] = true, ['__macosx'] = true }
local SKIP_EXT = { dll = true, asi = true, exe = true, bat = true, lib = true, reg = true, cmd = true, sf = true }
-- scripts of the moonloader-scripts collection that are cheats (teleport, auto rob, movement exploit)
local CHEATS = { ['clickwarp.lua'] = true, ['autorob.lua'] = true, ['bunnyhopfix.lua'] = true }
local CHEAT_NAMES = { ['TROPA DO HG MENU'] = true }

local okl, lfs = pcall(require, 'lfs')
local okz, zip = pcall(require, 'mp_zip')

local ROOT, DIR
local function log(t) print('[LuaScripts] ' .. t) end
local function say(t) log(t) pcall(sampAddChatMessage, '[LuaScripts] ' .. t, 0x66CCFF) end

local function norm(p) return (p:gsub('\\', '/'):gsub('/+', '/')) end
local function mode(p) return lfs.attributes(p, 'mode') end
local function exists(p) return mode(p) ~= nil end
local function ext(n) return (n:match('%.([^%./]+)$') or ''):lower() end
local function stem(n) return (n:gsub('%.[^%./]+$', '')) end

local function mkdirs(path)
  local cur = ''
  for part in path:gmatch('[^/]+') do
    cur = cur .. '/' .. part
    if mode(cur) ~= 'directory' then lfs.mkdir(cur) end
  end
end

local function rmtree(p)
  if mode(p) == 'directory' then
    for f in lfs.dir(p) do if f ~= '.' and f ~= '..' then rmtree(p .. '/' .. f) end end
    lfs.rmdir(p)
  elseif exists(p) then os.remove(p) end
end

local function readall(p)
  local f = io.open(p, 'rb')
  if not f then return nil end
  local s = f:read('*a'); f:close()
  return s
end

local function listdir(d)
  local files, dirs = {}, {}
  local ok = pcall(function()
    for f in lfs.dir(d) do
      if f ~= '.' and f ~= '..' then
        local m = mode(d .. '/' .. f)
        if m == 'file' then files[#files + 1] = f elseif m == 'directory' then dirs[#dirs + 1] = f end
      end
    end
  end)
  table.sort(files); table.sort(dirs)
  return files, dirs, ok
end

-- ------------------------------------------------------------ what is a script
local infoCache = {}   -- path -> { mtime, script, cheat, backslash }
local function fileInfo(path)
  local mt = lfs.attributes(path, 'modification') or 0
  local c = infoCache[path]
  if c and c.mtime == mt then return c end
  local e = ext(path)
  local src = readall(path) or ''
  local info = { mtime = mt }
  if e == 'luac' or src:sub(1, 3) == '\27LJ' or src:sub(1, 4) == '\27Lua' then
    info.script = true          -- compiled: it was made to be run
    info.backslash = false
  else
    info.script = src:find('function%s+main%s*%(') or src:find('[%s;]main%s*=%s*function')
      or src:find('script_name%s*[%(\'"]') or src:find('script_properties%s*%(')
      or src:find('sampRegisterChatCommand%s*%(') or src:find('function%s+[%w_%.:]*on%u[%w_]*%s*%(')
      or src:find('[%w_]+%.on%u[%w_]*%s*=%s*function')
    info.script = info.script and true or false
    info.backslash = src:find('\\\\', 1, true) ~= nil
  end
  local head = src:sub(1, 4096)
  info.cheat = head:find('MP_CHEAT_MOD', 1, true) ~= nil or head:find("script_name('TROPA DO HG", 1, true) ~= nil
    or head:find('script_name("TROPA DO HG', 1, true) ~= nil or CHEATS[path:match('([^/]+)$'):lower()] == true
  infoCache[path] = info
  return info
end

local function hasScripts(d, depth)
  depth = depth or 0
  if depth > 8 then return false end
  local files, dirs = listdir(d)
  for _, f in ipairs(files) do
    local e = ext(f)
    if (e == 'lua' or e == 'luac') and fileInfo(d .. '/' .. f).script then return true end
  end
  for _, sd in ipairs(dirs) do
    if not DATA_DIRS[sd:lower()] and not SKIP_DIRS[sd:lower()] and hasScripts(d .. '/' .. sd, depth + 1) then return true end
  end
  return false
end

-- ------------------------------------------------------------ disabled.txt
local disabled = {}
local function readDisabled()
  disabled = {}
  local s = readall(DIR .. '/disabled.txt')
  if not s then return end
  for line in s:gmatch('[^\r\n]+') do
    line = line:gsub('#.*$', ''):gsub('^%s+', ''):gsub('%s+$', '')
    if line ~= '' then disabled[#disabled + 1] = norm(line):lower() end
  end
end
local function isDisabled(rel)
  rel = rel:lower()
  local base = rel:match('([^/]+)$')
  for _, d in ipairs(disabled) do
    if d == rel or d == base then return true end
    if d:sub(-1) == '/' and (rel:sub(1, #d) == d or rel:find('/' .. d, 1, true)) then return true end
  end
  return false
end

-- ------------------------------------------------------------ Script Manager off list (MP 5.02)
-- MP's Script Manager (top bar, moon icon) writes moonloader/config/mp_scripts_off.txt: one path per line relative
-- to moonloader/ ("lua_scripts/Hasslehud/HassleHUD Android.luac"). Those scripts are stopped and stay off after a
-- restart or a rejoin until they are switched on again.
local OFF_FILE = 'config/mp_scripts_off.txt'
local off, offStamp = {}, nil
local function readOff()
  off = {}
  local s = readall(ROOT .. '/' .. OFF_FILE)
  if not s then return end
  for line in s:gmatch('[^\r\n]+') do
    line = line:gsub('^%s+', ''):gsub('%s+$', '')
    if line ~= '' and line:sub(1, 1) ~= '#' then off[norm(line):lower()] = true end
  end
end
local function isOff(rel) return off[(DIR_NAME .. '/' .. rel):lower()] == true end
local function offChanged()
  local a = lfs.attributes(ROOT .. '/' .. OFF_FILE)
  local st = a and (a.modification .. ':' .. a.size) or ''
  if st ~= offStamp then offStamp = st return true end
  return false
end

-- ------------------------------------------------------------ copy data into moonloader/
local copied = 0
local function installTree(src, dst)
  local files, dirs = listdir(src)
  for _, f in ipairs(files) do
    if not SKIP_EXT[ext(f)] then
      local to = dst .. '/' .. f
      if not exists(to) then
        local data = readall(src .. '/' .. f)
        if data then
          mkdirs(dst)
          local w = io.open(to, 'wb')
          if w then w:write(data); w:close(); copied = copied + 1 end
        end
      end
    end
  end
  for _, d in ipairs(dirs) do
    if not SKIP_DIRS[d:lower()] then installTree(src .. '/' .. d, dst .. '/' .. d) end
  end
end

-- ------------------------------------------------------------ zip packages
local function unpackZip(zpath, rel)
  if not okz then log('mp_zip missing - ' .. rel .. ' is not unpacked') return nil end
  local name = rel:gsub('[/\\]', '__'):gsub('%.[Zz][Ii][Pp]$', '')
  local dest = DIR .. '/.unpacked/' .. name
  local a = lfs.attributes(zpath)
  local stamp = ('%d:%d:%s'):format(a.size or 0, a.modification or 0, zip.VERSION)
  if readall(dest .. '/.mp_stamp') == stamp then return dest, false end
  rmtree(dest)
  mkdirs(dest)
  local n, skipped = zip.extract(zpath, dest, function(entry)
    local low = entry:lower()
    if SKIP_EXT[ext(low)] then return false end
    for part in low:gmatch('[^/]+') do if SKIP_DIRS[part] then return false end end
    return true
  end)
  if not n then say('cannot open ' .. rel .. ': ' .. tostring(skipped)) return nil end
  for _, s in ipairs(skipped) do log(('%s: %s skipped (%s)'):format(rel, s[1], s[2])) end
  local w = io.open(dest .. '/.mp_stamp', 'wb')
  if w then w:write(stamp); w:close() end
  log(('%s unpacked: %d file(s)%s'):format(rel, n, zip.has_zlib and '' or ' (pure-Lua inflate)'))
  return dest, true
end

-- the folder inside an unpacked zip that is laid out like moonloader/
local function zipRoot(d, depth)
  depth = depth or 0
  local _, dirs = listdir(d)
  for _, sd in ipairs(dirs) do
    local l = sd:lower()
    if l == 'moonloader' or l == 'monetloader' then return d .. '/' .. sd end
  end
  if depth < 2 then
    for _, sd in ipairs(dirs) do
      if not DATA_DIRS[sd:lower()] then
        local r = zipRoot(d .. '/' .. sd, depth + 1)
        if r then return r end
      end
    end
  end
  return depth == 0 and d or nil
end

-- ------------------------------------------------------------ scan
local found        -- list of { path = file to start, src = real file, rel, cheat }
local scan

local function addScript(path, rel, pkg)
  if isDisabled(rel) then return end
  local info = fileInfo(path)
  if not info.script then return end
  if pkg then
    -- a mod folder with its own lib/: everything stays in that folder, the name check below does not apply
    found[#found + 1] = { src = path, rel = rel, cheat = info.cheat, backslash = info.backslash, pkg = pkg, off = isOff(rel) }
    return
  end
  -- MonetLoader / mods_loader already start a script with this name from moonloader/ or moonloader/mods/
  local base = path:match('([^/]+)$')
  if exists(ROOT .. '/' .. base) or exists(ROOT .. '/mods/' .. base) then
    log(rel .. ': a script with this name is in moonloader/ already - not started twice')
    return
  end
  found[#found + 1] = { src = path, rel = rel, cheat = info.cheat, backslash = info.backslash, off = isOff(rel) }
end

-- d: folder, rel: its path inside lua_scripts/, pkg: true when d is laid out like moonloader/
-- A folder (or a zip) that has its own lib/ next to its scripts is a self-contained mod (HassleHUD...): its lib/,
-- config/ and resource/ stay inside it. Its scripts run through a runner that makes getWorkingDirectory() that
-- folder and adds its lib/ to require (after moonloader/lib, so MP's Android builds of mimgui, samp.events... win).
scan = function(d, rel, depth, pkg)
  if depth > 8 then return end
  local files, dirs = listdir(d)
  if not pkg and depth > 0 and mode(d .. '/lib') == 'directory' then pkg = d end
  local stems = {}
  for _, f in ipairs(files) do
    local e, r = ext(f), (rel == '' and f or rel .. '/' .. f)
    if e == 'lua' or e == 'luac' then
      local before = #found
      addScript(d .. '/' .. f, r, pkg)
      if #found > before then stems[stem(f):lower()] = true end
    elseif e == 'zip' then
      if not isDisabled(r) then
        local dest, fresh = unpackZip(d .. '/' .. f, r)
        if dest then
          if hasScripts(dest) then
            scan(zipRoot(dest), r, depth + 1, pkg)
          else
            -- data pack: copy it into moonloader/ (top-level files like preview images stay out)
            local _, zdirs = listdir(zipRoot(dest))
            for _, zd in ipairs(zdirs) do installTree(zipRoot(dest) .. '/' .. zd, ROOT .. '/' .. zd) end
            if fresh then log(r .. ': no script inside - installed into moonloader/ as data') end
          end
        end
      end
    end
  end
  for _, sd in ipairs(dirs) do
    local l, r = sd:lower(), (rel == '' and sd or rel .. '/' .. sd)
    if sd:sub(1, 1) == '.' or SKIP_DIRS[l] then
      -- .unpacked / .run / PC runtime folders
    elseif pkg and (DATA_DIRS[l] or stems[l]) then
      -- the mod's own data: stays where it is
    elseif DATA_DIRS[l] or stems[l] then
      if not isDisabled(r .. '/') then installTree(d .. '/' .. sd, ROOT .. '/' .. sd) end
    elseif not isDisabled(r .. '/') then
      scan(d .. '/' .. sd, r, depth + 1, pkg)   -- a sub folder, or a PC package's moonloader/ folder
    end
  end
end

-- runner for scripts that use '\\' paths and for the scripts of a self-contained mod folder
local function runnerFor(item)
  local id = item.rel:gsub('%.[^%./]+$', ''):gsub('[^%w%-_%.]', '_')
  local dir = DIR .. '/.run'
  mkdirs(dir)
  local path = dir .. '/' .. id .. '.lua'
  local body = ('-- made by lua_scripts_loader.lua (MP): runs lua_scripts/%s. Do not edit.\nlocal __mp_src = %q\n'):format(item.rel, item.src)
  if item.backslash then body = body .. "pcall(require, 'mp_pcpaths')\n" end
  if item.pkg then
    body = body .. ('local __mp_pkg = %q\n'):format(item.pkg) .. [[
do
  local function n(p) return (p:gsub('\\', '/'):gsub('/+', '/')):lower() end
  local real = getWorkingDirectory
  local rootLib = n(real() .. '/lib/')
  -- moonloader/lib/ code (mimgui, MoonMonet...) keeps moonloader/, everything else (the mod and its own lib/)
  -- sees the mod folder, so its config/, resource/, fonts and .so files are found there
  getWorkingDirectory = function()
    local i = debug.getinfo(2, 'S')
    local s = i and i.source or ''
    if s:sub(1, 1) == '@' and n(s:sub(2)):sub(1, #rootLib) == rootLib then return real() end
    return __mp_pkg
  end
  local L = __mp_pkg .. '/lib/'
  package.path = package.path .. ';' .. L .. '?.lua;' .. L .. '?/init.lua;' .. L .. '?.luac;' .. L .. '?/init.luac;' ..
    __mp_pkg .. '/?.lua;' .. __mp_pkg .. '/?/init.lua'
  package.cpath = package.cpath .. ';' .. L .. '?.so'
  -- a .so of the mod that can't load here (HassleHUD's libGIF.so is 32-bit only: on a 64-bit game) no longer kills
  -- the whole mod: its functions return 0 and the rest of the mod runs
  local okf, ffi = pcall(require, 'ffi')
  if okf and type(ffi) == 'table' and ffi.load then
    local load, pkgn = ffi.load, n(__mp_pkg .. '/')
    ffi.load = function(name, ...)
      local ok, lib = pcall(load, name, ...)
      if ok then return lib end
      if type(name) == 'string' and n(name):sub(1, #pkgn) == pkgn then
        print('[LuaScripts] ' .. name .. ' does not load on this game (' .. tostring(lib) .. ') - its functions return 0')
        return setmetatable({}, { __index = function() return function() return 0 end end })
      end
      error(lib, 2)
    end
  end
end
]]
  end
  -- 1.2-mp: loadfile, then a plain read + load (BOM / #! line / a file loadfile can't open by name), and the real reason
  body = body .. [==[
local __mp_f, __mp_e = loadfile(__mp_src)
if not __mp_f then
  local h, he = io.open(__mp_src, 'rb')
  if h then
    local d = h:read('*a'); h:close()
    if d then
      d = d:gsub('^\239\187\191', ''):gsub('^#[^\n]*', '')
      local f2, e2 = load(d, '@' .. __mp_src)
      if f2 then __mp_f = f2 else __mp_e = e2 or __mp_e end
    end
  else
    __mp_e = tostring(__mp_e) .. ' (' .. tostring(he) .. ')'
  end
end
if not __mp_f then error('[LuaScripts] cannot load ' .. __mp_src .. ': ' .. tostring(__mp_e), 0) end
__mp_f(...)
]==]
  if readall(path) ~= body then
    local w = io.open(path, 'wb')
    if w then w:write(body); w:close() end
  end
  return path
end

-- lua_scripts/.run/index.txt for MP's Script Manager: "path inside lua_scripts<TAB>file MonetLoader runs<TAB>flags"
local function writeIndex(list)
  local out = { '# made by lua_scripts_loader.lua (MP) for the Script Manager. Do not edit.' }
  for _, it in ipairs(list) do
    out[#out + 1] = it.rel .. '\t' .. it.path .. '\t' .. (it.cheat and 'cheat' or '') .. (it.pkg and ',folder' or '')
  end
  local body = table.concat(out, '\n') .. '\n'
  mkdirs(DIR .. '/.run')
  local p = DIR .. '/.run/index.txt'
  if readall(p) ~= body then
    local w = io.open(p, 'wb')
    if w then w:write(body); w:close() end
  end
end

local function rescan()
  found, copied = {}, 0
  readDisabled()
  readOff()
  scan(DIR, '', 0)
  for _, it in ipairs(found) do it.path = (it.backslash or it.pkg) and runnerFor(it) or it.src end
  if copied > 0 then log(('%d data file(s) copied into moonloader/'):format(copied)) end
  writeIndex(found)
  return found
end

-- ------------------------------------------------------------ running scripts
local function samePath(a, b) return a and b and norm(a) == norm(b) end
local function isRunning(path)
  for _, s in ipairs(script.list()) do if not s.dead and samePath(s.path, path) then return s end end
end

local started = {}   -- path -> mtime of the source when it was started (a script that stops is not restarted
                     -- until its file changes, so a broken PC script doesn't loop)
local function start(it)
  if it.off then return false end
  local mt = lfs.attributes(it.src, 'modification') or 0
  if started[it.path] == mt then return false end
  started[it.path] = mt
  if isRunning(it.path) then return false end
  script.load(it.path)
  log('starting ' .. it.rel .. (it.path ~= it.src and ' (PC paths)' or ''))
  return true
end

local cheatsAllowed
do
  local okf, ffi = pcall(require, 'ffi')
  if okf and ffi then
    pcall(ffi.cdef, 'int MP_LuaCheatsAllowed(void);')
    local so = ffi.abi('64bit') and 'libMP64.so' or 'libMP32.so'
    local names = { so:sub(4, -4), so }
    local maps = io.open('/proc/self/maps', 'r')
    if maps then
      for l in maps:lines() do local p = l:match('(/%S*/' .. so:gsub('%.', '%%.') .. ')$') if p then names[#names + 1] = p break end end
      maps:close()
    end
    for _, n in ipairs(names) do
      local okl2, lib = pcall(ffi.load, n)
      if okl2 and lib and pcall(function() return lib.MP_LuaCheatsAllowed end) then
        cheatsAllowed = function() return lib.MP_LuaCheatsAllowed() ~= 0 end
        break
      end
    end
  end
  if not cheatsAllowed then cheatsAllowed = function() return false end end
end

local function cmd(arg)
  arg = (arg or ''):lower()
  if arg == 'rescan' or arg == 'reload' then started = {}; infoCache = {} end
  local list = rescan()
  local run, cheats = 0, 0
  for _, it in ipairs(list) do
    if it.cheat then cheats = cheats + 1 end
    if isRunning(it.path) then run = run + 1 end
  end
  say(('%d script(s) in lua_scripts/, %d running, %d cheat script(s). /luascripts rescan = start them again'):format(#list, run, cheats))
  if arg == 'list' then
    for _, it in ipairs(list) do say((isRunning(it.path) and '+ ' or '- ') .. it.rel) end
  end
end

function main()
  if not okl or not lfs then print('[LuaScripts] lfs missing - lua_scripts/ is not loaded') return end
  ROOT = norm(getWorkingDirectory())
  DIR = ROOT .. '/' .. DIR_NAME
  mkdirs(DIR)
  pcall(sampRegisterChatCommand, 'luascripts', cmd)

  local list = rescan()
  local n, c = 0, 0
  for _, it in ipairs(list) do
    if it.cheat then c = c + 1 elseif start(it) then n = n + 1 end
  end
  log(('%d script(s) in lua_scripts/, %d started, %d cheat script(s) (only where the server allows them)'):format(#list, n, c))

  local was, last = nil, os.time()
  while true do
    local ok = cheatsAllowed()
    if offChanged() or os.time() - last >= RESCAN_MS / 1000 then
      last = os.time()
      list = rescan()
      for _, it in ipairs(list) do
        if it.off then
          -- switched off in the Script Manager: stop it; it starts again when it is switched back on
          local s = isRunning(it.path)
          if s then s:unload(); log('stopped ' .. it.rel .. ' (switched off in the Script Manager)') end
          started[it.path] = nil
        elseif not it.cheat then start(it) end
      end
    end
    for _, it in ipairs(list) do
      if it.cheat then
        local s = isRunning(it.path)
        if ok and not s then
          start(it)
        elseif not ok and s then
          s:unload(); started[it.path] = nil
        end
      end
    end
    if was ~= nil and ok ~= was then
      local any = false
      for _, it in ipairs(list) do if it.cheat then any = true break end end
      if any then
        if ok then say('Cheat scripts in lua_scripts/ are allowed here - started.')
        else say('Cheat scripts in lua_scripts/ stopped: this server does not allow MonetLoader cheats.') end
      end
    end
    was = ok
    wait(1000)
  end
end
