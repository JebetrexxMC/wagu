script_name('MP Lua Scripts Loader')
script_author('ThirtyFoxMCx00E1')
script_version('1.0-mp')
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

local function addScript(path, rel)
  if isDisabled(rel) then return end
  local info = fileInfo(path)
  if not info.script then return end
  -- MonetLoader / mods_loader already start a script with this name from moonloader/ or moonloader/mods/
  local base = path:match('([^/]+)$')
  if exists(ROOT .. '/' .. base) or exists(ROOT .. '/mods/' .. base) then
    log(rel .. ': a script with this name is in moonloader/ already - not started twice')
    return
  end
  found[#found + 1] = { src = path, rel = rel, cheat = info.cheat, backslash = info.backslash }
end

-- d: folder, rel: its path inside lua_scripts/, pkg: true when d is laid out like moonloader/
scan = function(d, rel, depth)
  if depth > 8 then return end
  local files, dirs = listdir(d)
  local stems = {}
  for _, f in ipairs(files) do
    local e, r = ext(f), (rel == '' and f or rel .. '/' .. f)
    if e == 'lua' or e == 'luac' then
      local before = #found
      addScript(d .. '/' .. f, r)
      if #found > before then stems[stem(f):lower()] = true end
    elseif e == 'zip' then
      if not isDisabled(r) then
        local dest, fresh = unpackZip(d .. '/' .. f, r)
        if dest then
          if hasScripts(dest) then
            scan(zipRoot(dest), r, depth + 1)
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
    elseif DATA_DIRS[l] or stems[l] then
      if not isDisabled(r .. '/') then installTree(d .. '/' .. sd, ROOT .. '/' .. sd) end
    elseif not isDisabled(r .. '/') then
      scan(d .. '/' .. sd, r, depth + 1)   -- a sub folder, or a PC package's moonloader/ folder
    end
  end
end

-- runner for scripts that use '\\' paths
local function runnerFor(item)
  local id = item.rel:gsub('%.[^%./]+$', ''):gsub('[^%w%-_%.]', '_')
  local dir = DIR .. '/.run'
  mkdirs(dir)
  local path = dir .. '/' .. id .. '.lua'
  local body = ('-- made by lua_scripts_loader.lua (MP): runs lua_scripts/%s with PC paths (\\\\ -> /). Do not edit.\n' ..
    'local __mp_src = %q\npcall(require, \'mp_pcpaths\')\nlocal __mp_f, __mp_e = loadfile(__mp_src)\n' ..
    'if not __mp_f then error(__mp_e, 0) end\n__mp_f(...)\n'):format(item.rel, item.src)
  if readall(path) ~= body then
    local w = io.open(path, 'wb')
    if w then w:write(body); w:close() end
  end
  return path
end

local function rescan()
  found, copied = {}, 0
  readDisabled()
  scan(DIR, '', 0)
  for _, it in ipairs(found) do it.path = it.backslash and runnerFor(it) or it.src end
  if copied > 0 then log(('%d data file(s) copied into moonloader/'):format(copied)) end
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
    if os.time() - last >= RESCAN_MS / 1000 then
      last = os.time()
      list = rescan()
      for _, it in ipairs(list) do if not it.cheat then start(it) end end
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
