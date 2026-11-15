script_name('MP Mods Loader')
script_author('ThirtyFoxMCx00E1')
script_version('1.1-mp')
script_description('Starts every script in moonloader/mods/ (MP 4.13.56)')

-- MonetLoader only starts the .lua files right in moonloader/. This starts moonloader/mods/*.lua too (only the
-- .lua files directly in mods/ - a mod's own folder, like mods/tvplayer/, is its data and is not started).
-- An old copy of the same mod in moonloader/ (from MP 4.13.55 or older) is stopped and renamed to .lua.old, so
-- the mod doesn't run twice.
--
-- Cheat mods (a "MP_CHEAT_MOD" line near the top of the file, or a known cheat menu like the HG menu) follow the
-- same rule as CheatMenu: they run offline (single player) and on a custom server whose owner turned
-- "Allow CheatMenu + MonetLoader cheats" on in Server Management. Never on the default / LAN server or the featured
-- servers, never with "Block menu cheats". When the owner turns it off, or you join a server that doesn't allow
-- them, they are stopped at once; they start again where they are allowed. MP answers MP_LuaCheatsAllowed().

local CHEAT_NAMES = { ['TROPA DO HG MENU'] = true }   -- script_name() of cheat menus without the marker

local function exists(p) local f = io.open(p, 'r') if f then f:close() return true end return false end

local cheatCache = {}
local function fileIsCheat(path)
  if cheatCache[path] ~= nil then return cheatCache[path] end
  local f = io.open(path, 'rb')
  local yes = false
  if f then
    local head = f:read(4096) or ''
    f:close()
    yes = head:find('MP_CHEAT_MOD', 1, true) ~= nil or head:find("script_name('TROPA DO HG MENU')", 1, true) ~= nil
  end
  cheatCache[path] = yes
  return yes
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
      local okl, lib = pcall(ffi.load, n)
      if okl and lib then
        local oks = pcall(function() return lib.MP_LuaCheatsAllowed end)
        if oks then cheatsAllowed = function() return lib.MP_LuaCheatsAllowed() ~= 0 end break end
      end
    end
  end
  if not cheatsAllowed then cheatsAllowed = function() return false end end   -- MP older than 4.13.56: never
end

local function say(text)
  print('[Mods] ' .. text)
  pcall(sampAddChatMessage, '[Mods] ' .. text, 0xFFDD33)
end

local function samePath(a, b)
  if not a or not b then return false end
  a = a:gsub('\\', '/'):gsub('/+', '/'); b = b:gsub('\\', '/'):gsub('/+', '/')
  return a == b
end
local function isRunning(path)
  for _, s in ipairs(script.list()) do
    if not s.dead and samePath(s.path, path) then return s end
  end
  return nil
end
local lastLoad = {}
local function loadOnce(path)   -- script.load is queued: never ask again within 10 s
  local t = os.time()
  if lastLoad[path] and t - lastLoad[path] < 10 then return end
  lastLoad[path] = t
  script.load(path)
end

function main()
  local root = getWorkingDirectory()
  local dir = root .. '/mods'
  local okl, lfs = pcall(require, 'lfs')
  if not okl or not lfs then print('[Mods] lfs missing - mods/ is not loaded') return end
  local files = {}
  local okd = pcall(function()
    for f in lfs.dir(dir) do
      if f:lower():match('%.lua$') and lfs.attributes(dir .. '/' .. f, 'mode') == 'file' then files[#files + 1] = f end
    end
  end)
  if not okd then print('[Mods] no folder ' .. dir) return end
  table.sort(files)

  -- old copies in moonloader/ -> .lua.old
  for _, f in ipairs(files) do
    local old = root .. '/' .. f
    if exists(old) then
      local s = isRunning(old)
      if s then s:unload() end
      os.rename(old, old .. '.old')
      print('[Mods] old ' .. f .. ' in moonloader/ - stopped, renamed to ' .. f .. '.old')
    end
  end

  local normal, cheats = 0, {}
  for _, f in ipairs(files) do
    local path = dir .. '/' .. f
    if fileIsCheat(path) then cheats[#cheats + 1] = path
    elseif not isRunning(path) then
      loadOnce(path)
      normal = normal + 1
      print('[Mods] starting mods/' .. f)
    end
  end
  print(string.format('[Mods] %d mod(s) in mods/, %d started, %d cheat mod(s) (only where the server allows them)', #files, normal, #cheats))

  local was = nil
  while true do
    local ok = cheatsAllowed()
    for _, path in ipairs(cheats) do
      local s = isRunning(path)
      if ok and not s then
        loadOnce(path)
      elseif not ok and s then
        s:unload()
      end
    end
    -- cheat menus started some other way (a copy right in moonloader/ ...) follow the same rule
    if not ok then
      for _, s in ipairs(script.list()) do
        if not s.dead and not samePath(s.path, thisScript().path) and (CHEAT_NAMES[s.name] or fileIsCheat(s.path)) then
          s:unload()
        end
      end
    end
    if was ~= nil and ok ~= was and #cheats > 0 then
      if ok then say('Cheat mods are allowed here - started.')
      else say('Cheat mods stopped: this server does not allow MonetLoader cheats (featured / LAN server, or the owner turned them off).') end
    end
    was = ok
    wait(1000)
  end
end
