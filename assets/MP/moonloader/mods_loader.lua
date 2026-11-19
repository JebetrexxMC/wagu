script_name('MP Mods Loader')
script_author('ThirtyFoxMCx00E1')
script_version('1.2-mp')
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

-- MonetLoader for MP 4.0.0-mp: the old lib/ bundle (flattened copies with addresses guessed from other builds) and
-- the three remade mods (HG menu, hitmarker, TV player) are replaced by the real libraries built from source.
-- Run once: every stale file is renamed to <name>.old (nothing is deleted - rename it back to keep using it).
local MP_LIB_MARK = 'lib/.mp_lib_4.0.0'
local MP_STALE = {
  'AnimBlendFrameData.lua',
  'B',
  'C2dEffect.lua',
  'CAEAudioEntity.lua',
  'CAEPedAudioEntity.lua',
  'CAEPedSpeechAudioEntity.lua',
  'CAEPoliceScannerAudioEntity.lua',
  'CAESound.lua',
  'CAETwinLoopSoundEntity.lua',
  'CAEVehicleAudioEntity.lua',
  'CAEWeaponAudioEntity.lua',
  'CAttractorScanner.lua',
  'CAutoPilot.lua',
  'CAutomobile.lua',
  'CBike.lua',
  'CBmx.lua',
  'CBoat.lua',
  'CBouncingPanel.lua',
  'CCam.lua',
  'CCamPathSplines.lua',
  'CCamera.lua',
  'CColBox.lua',
  'CColDisk.lua',
  'CColLine.lua',
  'CColModel.lua',
  'CColPoint.lua',
  'CColSphere.lua',
  'CColTriangle.lua',
  'CColTrianglePlane.lua',
  'CCollisionData.lua',
  'CColourSet.lua',
  'CCopPed.lua',
  'CCrimeBeingQd.lua',
  'CCutsceneObject.lua',
  'CDamageManager.lua',
  'CDoor.lua',
  'CDummy.lua',
  'CEntity.lua',
  'CEntityScanner.lua',
  'CEventGroup.lua',
  'CEventHandler.lua',
  'CEventScanner.lua',
  'CFire.lua',
  'CHeli.lua',
  'CObject.lua',
  'CObjectInfo.lua',
  'CPed.lua',
  'CPedAcquaintance.lua',
  'CPedClothesDesc.lua',
  'CPedIK.lua',
  'CPedIntelligence.lua',
  'CPhysical.lua',
  'CPlaceable.lua',
  'CPlane.lua',
  'CPlayerData.lua',
  'CPlayerInfo.lua',
  'CPlayerPed.lua',
  'CPool.lua',
  'CQueuedMode.lua',
  'CRealTimeShadow.lua',
  'CReference.lua',
  'CRideAnimData.lua',
  'CShadowCamera.lua',
  'CSimpleTransform.lua',
  'CStoredCollPoly.lua',
  'CTask.lua',
  'CTaskManager.lua',
  'CTaskTimer.lua',
  'CTrain.lua',
  'CTransmission.lua',
  'CVehicle.lua',
  'CWanted.lua',
  'CWeapon.lua',
  'CWeaponEffects.lua',
  'CWeaponInfo.lua',
  'Date.lua',
  'ElementNode.lua',
  'FxSystem_c.lua',
  'Itn11.lua',
  'List.lua',
  'Map.lua',
  'MultiMap.lua',
  'OrderedMap.lua',
  'RenderWare.lua',
  'Set.lua',
  'aes.lua',
  'app.lua',
  'array2d.lua',
  'bcoder.lua',
  'bit.lua',
  'bit32_ops.lua',
  'bit_ops.lua',
  'bitstream.lua',
  'bitstream_io.lua',
  'bstream.lua',
  'buffer.lua',
  'cdefs.lua',
  'ciphermode.lua',
  'cjson/safe.lua',
  'class.lua',
  'client.lua',
  'client_copas.lua',
  'client_ev.lua',
  'client_sync.lua',
  'common.lua',
  'compat.lua',
  'comprehension.lua',
  'config.lua',
  'context.lua',
  'core.lua',
  'data.lua',
  'dir.lua',
  'dom.lua',
  'eCamMode.lua',
  'ePedState.lua',
  'ePedType.lua',
  'eVehicleHandlingFlags.lua',
  'eVehicleHandlingModelFlags.lua',
  'eWeaponType.lua',
  'ev_common.lua',
  'events.lua',
  'events/bitstream_io.lua',
  'events/core.lua',
  'events/extra_types.lua',
  'events/handlers.lua',
  'events/utils.lua',
  'extra_types.lua',
  'file.lua',
  'findfile.lua',
  'frame.lua',
  'fs.lua',
  'ftp.lua',
  'func.lua',
  'game/weapons.lua.zip',
  'gf.lua',
  'globals.lua',
  'handlers.lua',
  'handshake.lua',
  'headers.lua',
  'http.lua',
  'https.lua',
  'import_into.lua',
  'init.lua',
  'input.lua',
  'json.lua',
  'keys.lua',
  'lapp.lua',
  'lexer.lua',
  'lock.lua',
  'lua53_ops.lua',
  'luabalanced.lua',
  'matrix.lua',
  'mbox.lua',
  'message.lua',
  'metatype.lua',
  'mimgui/hgfont.ttf',
  'models.lua',
  'module.lua',
  'operator.lua',
  'permute.lua',
  'pretty.lua',
  'print.lua',
  'pure_lua_ops.lua',
  'quaternion.lua',
  'queue.lua',
  'raknet.lua',
  'renderer.lua',
  'samp/0_3_7-R1/enums.lua',
  'samp/0_3_7-R1/hg/.1.pngg',
  'samp/0_3_7-R1/hg/.Roboto-Black.ttf',
  'samp/0_3_7-R1/hg/.THE.ttf',
  'samp/0_3_7-R1/hg/.cj.pngg',
  'samp/0_3_7-R1/hg/.esque.pngg',
  'samp/0_3_7-R1/hg/.faicons.otf',
  'samp/0_3_7-R1/hg/.hg-font1.ttf',
  'samp/0_3_7-R1/hg/.hg-font2.ttf',
  'samp/0_3_7-R1/hg/.icone.pngg',
  'samp/0_3_7-R1/hg/.nickk.pngg',
  'samp/0_3_7-R1/hg/.poset.pngg',
  'samp/0_3_7-R1/hg/.sf.OTF',
  'samp/0_3_7-R1/hg/.teste.pngg',
  'samp/0_3_7-R1/stActorPool.lua',
  'samp/0_3_7-R1/stChatEntry.lua',
  'samp/0_3_7-R1/stChatInfo.lua',
  'samp/0_3_7-R1/stDialogInfo.lua',
  'samp/0_3_7-R1/stFontRenderer.lua',
  'samp/0_3_7-R1/stGameInfo.lua',
  'samp/0_3_7-R1/stGangzone.lua',
  'samp/0_3_7-R1/stGangzonePool.lua',
  'samp/0_3_7-R1/stInputInfo.lua',
  'samp/0_3_7-R1/stKillEntry.lua',
  'samp/0_3_7-R1/stKillInfo.lua',
  'samp/0_3_7-R1/stLocalPlayer.lua',
  'samp/0_3_7-R1/stObject.lua',
  'samp/0_3_7-R1/stObjectPool.lua',
  'samp/0_3_7-R1/stPickup.lua',
  'samp/0_3_7-R1/stPickupPool.lua',
  'samp/0_3_7-R1/stPlayerPool.lua',
  'samp/0_3_7-R1/stRemotePlayer.lua',
  'samp/0_3_7-R1/stSAMP.lua',
  'samp/0_3_7-R1/stSAMPEntity.lua',
  'samp/0_3_7-R1/stSAMPPed.lua',
  'samp/0_3_7-R1/stSAMPPools.lua',
  'samp/0_3_7-R1/stSAMPVehicle.lua',
  'samp/0_3_7-R1/stScoreboardInfo.lua',
  'samp/0_3_7-R1/stServerInfo.lua',
  'samp/0_3_7-R1/stServerPresets.lua',
  'samp/0_3_7-R1/stTextLabel.lua',
  'samp/0_3_7-R1/stTextLabelPool.lua',
  'samp/0_3_7-R1/stTextdraw.lua',
  'samp/0_3_7-R1/stTextdrawPool.lua',
  'samp/0_3_7-R1/stVehiclePool.lua',
  'samp/0_3_7-R1/sync.lua',
  'samp/0_3_DL-R1/enums.lua',
  'samp/0_3_DL-R1/stActorPool.lua',
  'samp/0_3_DL-R1/stChatEntry.lua',
  'samp/0_3_DL-R1/stChatInfo.lua',
  'samp/0_3_DL-R1/stDialogInfo.lua',
  'samp/0_3_DL-R1/stFontRenderer.lua',
  'samp/0_3_DL-R1/stGameInfo.lua',
  'samp/0_3_DL-R1/stGangzone.lua',
  'samp/0_3_DL-R1/stGangzonePool.lua',
  'samp/0_3_DL-R1/stInputInfo.lua',
  'samp/0_3_DL-R1/stKillEntry.lua',
  'samp/0_3_DL-R1/stKillInfo.lua',
  'samp/0_3_DL-R1/stLocalPlayer.lua',
  'samp/0_3_DL-R1/stObject.lua',
  'samp/0_3_DL-R1/stObjectPool.lua',
  'samp/0_3_DL-R1/stPickup.lua',
  'samp/0_3_DL-R1/stPickupPool.lua',
  'samp/0_3_DL-R1/stPlayerPool.lua',
  'samp/0_3_DL-R1/stRemotePlayer.lua',
  'samp/0_3_DL-R1/stSAMP.lua',
  'samp/0_3_DL-R1/stSAMPEntity.lua',
  'samp/0_3_DL-R1/stSAMPPed.lua',
  'samp/0_3_DL-R1/stSAMPPools.lua',
  'samp/0_3_DL-R1/stSAMPVehicle.lua',
  'samp/0_3_DL-R1/stScoreboardInfo.lua',
  'samp/0_3_DL-R1/stServerInfo.lua',
  'samp/0_3_DL-R1/stServerPresets.lua',
  'samp/0_3_DL-R1/stSpawnInfo.lua',
  'samp/0_3_DL-R1/stTargetInfo.lua',
  'samp/0_3_DL-R1/stTextLabel.lua',
  'samp/0_3_DL-R1/stTextLabelPool.lua',
  'samp/0_3_DL-R1/stTextdraw.lua',
  'samp/0_3_DL-R1/stTextdrawPool.lua',
  'samp/0_3_DL-R1/stVehiclePool.lua',
  'samp/0_3_DL-R1/sync.lua',
  'samp/definitions.lua',
  'samp/encode.lua',
  'samp/events/bot.lua.upd.ini',
  'samp/events_core.lua',
  'samp/packet.lua',
  'sandbox.lua',
  'scanner.lua',
  'semaphore.lua',
  'seq.lua',
  'server.lua',
  'server_copas.lua',
  'server_ev.lua',
  'shared.lua',
  'sip.lua',
  'smtp.lua',
  'strict.lua',
  'string.lua',
  'stringio.lua',
  'stringx.lua',
  'subprocess.lua',
  'sync.lua',
  'synchronization.lua',
  'tBoatHandlingData.lua',
  'tFlyingHandlingData.lua',
  'tHandlingData.lua',
  'tTransmissionGear.lua',
  'table.lua',
  'tablex.lua',
  'template.lua',
  'test.lua',
  'text.lua',
  'timer.lua',
  'tools.lua',
  'tp.lua',
  'tree.lua',
  'types.lua',
  'url.lua',
  'util.lua',
  'utils.lua',
  'vector2d.lua',
  'voidelements.lua',
  'weapons.lua',
  'xml.lua',
  'mods/hg.lua', 'mods/hitmarker.lua', 'mods/tvplayer.lua', '../hg.lua', '../hitmarker.lua', '../tvplayer.lua',
}
do
  local root = getWorkingDirectory() .. '/'
  local f = io.open(root .. MP_LIB_MARK, 'r')
  if f then f:close() else
    local n = 0
    for _, rel in ipairs(MP_STALE) do
      local p = root .. (rel:find('^mods/') and rel or ('lib/' .. rel))
      local h = io.open(p, 'rb')
      if h then h:close(); os.remove(p .. '.old'); if os.rename(p, p .. '.old') then n = n + 1 end end
    end
    local w = io.open(root .. MP_LIB_MARK, 'w')
    if w then w:write('MonetLoader for MP 4.0.0-mp library bundle\n'); w:close() end
    if n > 0 then print(('[MP] library update: %d old file(s) renamed to *.old'):format(n)) end
  end
end

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

  -- 5.02: mods switched off in MP's Script Manager (moon icon) stay off: config/mp_scripts_off.txt
  local off = {}
  do
    local fo = io.open(root .. '/config/mp_scripts_off.txt', 'r')
    if fo then
      for l in fo:lines() do l = l:gsub('\r', ''):gsub('^%s+', ''):gsub('%s+$', ''):gsub('\\', '/'):lower() if l ~= '' and l:sub(1, 1) ~= '#' then off[l] = true end end
      fo:close()
    end
  end
  local normal, cheats = 0, {}
  for _, f in ipairs(files) do
    local path = dir .. '/' .. f
    if off['mods/' .. f:lower()] then print('[Mods] mods/' .. f .. ' is switched off in the Script Manager')
    elseif fileIsCheat(path) then cheats[#cheats + 1] = path
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
