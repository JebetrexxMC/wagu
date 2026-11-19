-- mp_pcpaths 1.0: PC MoonLoader file paths on Android.
--
-- PC scripts build paths with backslashes: getWorkingDirectory() .. '\\resource\\hitmarker\\hit.png'. On Android
-- that is one file name with '\' in it, so the file is "missing". `require 'mp_pcpaths'` (lua_scripts_loader.lua does
-- it for every script it starts that uses '\\' paths) turns '\' into '/' in the file functions of this script:
-- io.open / io.lines / io.input / io.output, os.remove / os.rename, loadfile / dofile, doesFileExist /
-- doesDirectoryExist / createDirectory, renderLoadTextureFromFile, loadAudioStream / load3dAudioStream and lfs.
-- Anything else (chat text, URLs) is left alone. Loaded once per script state.

if rawget(_G, '__mp_pcpaths') then return _G.__mp_pcpaths end

local M = { VERSION = '1.0' }

local function fix(p)
  if type(p) ~= 'string' or not p:find('\\', 1, true) then return p end
  if p:find('^%a+://') then return p end   -- URLs
  p = p:gsub('\\', '/'):gsub('([^:])//+', '%1/')
  return p
end
M.fix = fix

local function wrap(tbl, key)
  local f = tbl and rawget(tbl, key)
  if type(f) ~= 'function' then return end
  tbl[key] = function(a, b, ...) return f(fix(a), b, ...) end
end
local function wrap2(tbl, key)   -- both arguments are paths
  local f = tbl and rawget(tbl, key)
  if type(f) ~= 'function' then return end
  tbl[key] = function(a, b, ...) return f(fix(a), fix(b), ...) end
end

wrap(io, 'open'); wrap(io, 'lines'); wrap(io, 'input'); wrap(io, 'output')
wrap(os, 'remove'); wrap2(os, 'rename')
wrap(_G, 'loadfile'); wrap(_G, 'dofile')
for _, k in ipairs({ 'doesFileExist', 'doesDirectoryExist', 'createDirectory', 'renderLoadTextureFromFile',
  'loadAudioStream', 'load3dAudioStream', 'loadDynamicLibrary' }) do wrap(_G, k) end

local okl, lfs = pcall(require, 'lfs')
if okl and type(lfs) == 'table' then
  for _, k in ipairs({ 'attributes', 'symlinkattributes', 'dir', 'mkdir', 'rmdir', 'chdir', 'touch', 'lock_dir' }) do wrap(lfs, k) end
  wrap2(lfs, 'link')
end

-- modules with file functions: patched when the script requires them
local MODULE_FUNCS = {
  mimgui = { 'CreateTextureFromFile' },
  imgui = { 'CreateTextureFromFile' },
  memory = {},
}
local patched = setmetatable({}, { __mode = 'k' })
local raw_require = require
_G.require = function(name, ...)
  local m = raw_require(name, ...)
  local list = MODULE_FUNCS[name]
  if list and type(m) == 'table' and not patched[m] then
    patched[m] = true
    for _, k in ipairs(list) do
      local ok, f = pcall(function() return m[k] end)
      if ok and type(f) == 'function' then
        rawset(m, k, function(a, ...) return f(fix(a), ...) end)
      end
    end
  end
  return m
end

_G.__mp_pcpaths = M
return M
