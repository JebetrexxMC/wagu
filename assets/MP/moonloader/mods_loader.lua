script_name('MP Mods Loader')
script_author('ThirtyFoxMCx00E1')
script_version('1.0-mp')
script_description('Starts every script in moonloader/mods/ (MP 4.13.56)')

-- MonetLoader only starts the .lua files right in moonloader/. This starts moonloader/mods/*.lua too (only the
-- .lua files directly in mods/ - a mod's own folder, like mods/tvplayer/, is its data and is not started).
-- An old copy of the same mod in moonloader/ (from MP 4.13.55 or older) is stopped and renamed to .lua.old, so
-- the mod doesn't run twice.

local function exists(p) local f = io.open(p, 'r') if f then f:close() return true end return false end

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

  local running = {}
  for _, s in ipairs(script.list()) do running[#running + 1] = s end
  local started = 0
  for _, f in ipairs(files) do
    local path = dir .. '/' .. f
    local already = false
    for _, s in ipairs(running) do
      if s.path == path then already = true end
      if not s.dead and s.path == root .. '/' .. f then   -- old copy in moonloader/
        print('[Mods] old ' .. f .. ' in moonloader/ - stopped, renamed to ' .. f .. '.old')
        s:unload()
        os.rename(root .. '/' .. f, root .. '/' .. f .. '.old')
      end
    end
    if not already then
      if exists(root .. '/' .. f) then os.rename(root .. '/' .. f, root .. '/' .. f .. '.old') end
      script.load(path)
      started = started + 1
      print('[Mods] starting mods/' .. f)
    end
  end
  print(string.format('[Mods] %d mod(s) in mods/, %d started', #files, started))
end
