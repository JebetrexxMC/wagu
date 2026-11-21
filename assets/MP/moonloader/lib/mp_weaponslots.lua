-- mp_weaponslots 1.0 (MP 5.02.37): CPed weapon slots on Android, for PC scripts.
--
-- PC MoonLoader scripts read the player's weapon slots with PC offsets: CPed + 0x5A0, 0x1C bytes per slot, ammo in
-- clip at +0x8. libGTASA.so's CPed is laid out differently (and pointers are 8 bytes on arm64), so on the phone that
-- address is something else. This finds the real offset once: it looks inside the player's CPed for the slot of the
-- weapon in the hand (CWeapon = type, state, ammo in clip, total ammo ...) and checks the total ammo against
-- getAmmoInCharWeapon. Until a weapon with ammo was held, the functions return nil (callers fall back).
--
--   local ws = require 'mp_weaponslots'
--   ws.clip(ped, weaponId)   -> ammo in the clip or nil
--   ws.slotType(ped, slot)   -> weapon type in that slot or nil
--   ws.base(ped)             -> address of slot 0 or nil
-- A ped other than the player uses the same offset (same struct).

if rawget(_G, '__mp_weaponslots') then return _G.__mp_weaponslots end

local M = { VERSION = '1.0' }
local okm, memory = pcall(require, 'memory')
local OFF, STRIDE
local tried = 0

local function u32(a)
  local ok, v = pcall(memory.getuint32, a, false)
  if ok then return v end
end

local function find(ptr)
  local w = getCurrentCharWeapon(PLAYER_PED)
  if not w or w < 16 then return end            -- needs a weapon with ammo in the hand
  local okS, slot = pcall(getWeapontypeSlot, w)
  if not okS or type(slot) ~= 'number' or slot < 1 or slot > 12 then return end
  local total = getAmmoInCharWeapon(PLAYER_PED, w)
  if not total or total <= 0 then return end
  for _, stride in ipairs({ 0x1C, 0x20, 0x24, 0x28 }) do
    for off = 0x300, 0xE00, 4 do
      local a = ptr + off + slot * stride
      if u32(a) == w and u32(a + 0xC) == total then
        local c = u32(a + 0x8)
        local fist = u32(ptr + off)           -- slot 0: fist / brass knuckles
        if c and c <= total and (fist == 0 or fist == 1) then
          OFF, STRIDE = off, stride
          print(('[mp_weaponslots] CPed weapon slots at +0x%X, %d bytes each'):format(off, stride))
          return true
        end
      end
    end
  end
end

function M.base(ped)
  if not okm then return nil end
  ped = ped or PLAYER_PED
  local okp, ptr = pcall(getCharPointer, ped)
  if not okp or type(ptr) ~= 'number' or ptr < 0x10000 then return nil end
  if not OFF then
    local now = os.clock()
    if now - tried < 1.0 then return nil end
    tried = now
    local okp2, pp = pcall(getCharPointer, PLAYER_PED)
    if not okp2 or not pp or not find(pp) then return nil end
  end
  return ptr + OFF, STRIDE
end

function M.slotType(ped, slot)
  local b, s = M.base(ped)
  if not b then return nil end
  return u32(b + slot * s)
end

function M.clip(ped, weapon)
  local b, s = M.base(ped)
  if not b then return nil end
  local okS, slot = pcall(getWeapontypeSlot, weapon)
  if not okS or type(slot) ~= 'number' or slot < 0 or slot > 12 then return nil end
  if u32(b + slot * s) ~= weapon then return nil end
  return u32(b + slot * s + 0x8)
end

function M.offsets() return OFF, STRIDE end

_G.__mp_weaponslots = M
return M
