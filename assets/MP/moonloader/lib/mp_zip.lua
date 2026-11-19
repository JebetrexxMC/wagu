-- mp_zip 1.0: read .zip files from Lua on MonetLoader (Android) and PC MoonLoader.
--
--   local zip = require 'mp_zip'
--   local z, err = zip.open(path)            -- reads the central directory
--   for _, e in ipairs(z.entries) do ... end  -- e.name, e.size, e.csize, e.method, e.dir, e.mtime
--   local data, err = z:read(e)               -- the uncompressed bytes of one entry
--   local n, err = zip.extract(path, dest)    -- unpack everything below dest/ (zip-slip safe)
--
-- Stored (0) and deflate (8) entries. Deflate uses the system zlib through LuaJIT ffi (libz.so is on every Android
-- phone) and falls back to the pure-Lua inflate below when ffi or zlib is not there. Encrypted and zip64 entries are
-- skipped with a reason. CRC-32 is checked when zlib is available.

local M = { VERSION = '1.0' }

local bit = bit or (pcall(require, 'bit') and require('bit')) or bit32
local band, bor, rshift, lshift = bit.band, bit.bor, bit.rshift, bit.lshift
local byte, sub, char, concat = string.byte, string.sub, string.char, table.concat

-- ---------------------------------------------------------------- zlib through ffi
local Z
do
  local okf, ffi = pcall(require, 'ffi')
  if okf and ffi then
    pcall(ffi.cdef, [[
      typedef struct mpzip_stream {
        const uint8_t *next_in; unsigned avail_in; unsigned long total_in;
        uint8_t *next_out; unsigned avail_out; unsigned long total_out;
        const char *msg; void *state; void *zalloc; void *zfree; void *opaque;
        int data_type; unsigned long adler; unsigned long reserved;
      } mpzip_stream;
      int inflateInit2_(mpzip_stream *strm, int windowBits, const char *version, int stream_size);
      int inflate(mpzip_stream *strm, int flush);
      int inflateEnd(mpzip_stream *strm);
      const char *zlibVersion(void);
      unsigned long crc32(unsigned long crc, const uint8_t *buf, unsigned len);
    ]])
    for _, n in ipairs({ 'z', 'libz.so', 'libz.so.1', 'zlib1', 'zlib' }) do
      local okl, lib = pcall(ffi.load, n)
      if okl and lib and pcall(function() return lib.inflateInit2_ and lib.zlibVersion end) then
        Z = { ffi = ffi, lib = lib }
        break
      end
    end
  end
end
M.has_zlib = Z ~= nil

local function zlib_inflate(data, size)
  local ffi, lib = Z.ffi, Z.lib
  local strm = ffi.new('mpzip_stream')
  local out = ffi.new('uint8_t[?]', math.max(size, 1))
  local inp = ffi.cast('const uint8_t*', data)
  strm.next_in, strm.avail_in = inp, #data
  strm.next_out, strm.avail_out = out, size
  if lib.inflateInit2_(strm, -15, lib.zlibVersion(), ffi.sizeof('mpzip_stream')) ~= 0 then
    return nil, 'zlib: inflateInit2 failed'
  end
  local r = lib.inflate(strm, 4)   -- Z_FINISH
  local got = tonumber(strm.total_out)
  lib.inflateEnd(strm)
  if r ~= 1 and not (r == 0 and got == size) and not (r == -5 and got == size) then
    return nil, 'zlib: inflate error ' .. tostring(r)
  end
  return ffi.string(out, got)
end

local function zlib_crc(s)
  return tonumber(Z.lib.crc32(0, Z.ffi.cast('const uint8_t*', s), #s))
end

-- ---------------------------------------------------------------- pure-Lua inflate (RFC 1951)
local LBASE = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 }
local LEXT = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 }
local DBASE = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
  4097, 6145, 8193, 12289, 16385, 24577 }
local DEXT = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 }
local CLORDER = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 }

-- canonical Huffman table: t[len] = { [code] = symbol }, codes read bit by bit (LSB-first stream, MSB-first codes)
local function huff(lengths, n)
  local count, maxl = {}, 0
  for i = 0, 15 do count[i] = 0 end
  for i = 0, n - 1 do local l = lengths[i] or 0 if l > 0 then count[l] = count[l] + 1 if l > maxl then maxl = l end end end
  local next_code, code = {}, 0
  for l = 1, 15 do code = lshift(code + count[l - 1], 1) next_code[l] = code end
  local t = { max = maxl }
  for l = 1, maxl do t[l] = {} end
  for i = 0, n - 1 do
    local l = lengths[i] or 0
    if l > 0 then t[l][next_code[l]] = i; next_code[l] = next_code[l] + 1 end
  end
  return t
end

local FIXED_L, FIXED_D
local function fixed()
  if not FIXED_L then
    local l = {}
    for i = 0, 143 do l[i] = 8 end
    for i = 144, 255 do l[i] = 9 end
    for i = 256, 279 do l[i] = 7 end
    for i = 280, 287 do l[i] = 8 end
    FIXED_L = huff(l, 288)
    local d = {}
    for i = 0, 29 do d[i] = 5 end
    FIXED_D = huff(d, 30)
  end
  return FIXED_L, FIXED_D
end

local function lua_inflate(data, size)
  local pos, bitbuf, bitcnt = 1, 0, 0
  local out, on = {}, 0
  local function need(n)
    while bitcnt < n do
      local b = byte(data, pos)
      if not b then error('inflate: unexpected end of data', 0) end
      pos = pos + 1
      bitbuf = bor(bitbuf, lshift(b, bitcnt))
      bitcnt = bitcnt + 8
    end
  end
  local function bits(n)
    if n == 0 then return 0 end
    need(n)
    local v = band(bitbuf, lshift(1, n) - 1)
    bitbuf = rshift(bitbuf, n); bitcnt = bitcnt - n
    return v
  end
  local function decode(t)
    local code = 0
    for l = 1, t.max do
      code = bor(lshift(code, 1), bits(1))
      local s = t[l][code]
      if s then return s end
    end
    error('inflate: bad huffman code', 0)
  end
  repeat
    local final = bits(1)
    local btype = bits(2)
    if btype == 0 then
      bitbuf, bitcnt = 0, 0
      local len = byte(data, pos) + byte(data, pos + 1) * 256
      pos = pos + 4
      for i = 0, len - 1 do on = on + 1; out[on] = byte(data, pos + i) end
      pos = pos + len
    elseif btype == 1 or btype == 2 then
      local lt, dt
      if btype == 1 then lt, dt = fixed() else
        local hlit, hdist, hclen = bits(5) + 257, bits(5) + 1, bits(4) + 4
        local cl = {}
        for i = 1, 19 do cl[CLORDER[i]] = 0 end
        for i = 1, hclen do cl[CLORDER[i]] = bits(3) end
        local ct = huff(cl, 19)
        local lens, i = {}, 0
        while i < hlit + hdist do
          local s = decode(ct)
          if s < 16 then lens[i] = s; i = i + 1
          elseif s == 16 then local r = 3 + bits(2) local p = lens[i - 1] for _ = 1, r do lens[i] = p; i = i + 1 end
          elseif s == 17 then local r = 3 + bits(3) for _ = 1, r do lens[i] = 0; i = i + 1 end
          else local r = 11 + bits(7) for _ = 1, r do lens[i] = 0; i = i + 1 end end
        end
        local ll, dl = {}, {}
        for k = 0, hlit - 1 do ll[k] = lens[k] end
        for k = 0, hdist - 1 do dl[k] = lens[hlit + k] end
        lt, dt = huff(ll, hlit), huff(dl, hdist)
      end
      while true do
        local s = decode(lt)
        if s < 256 then on = on + 1; out[on] = s
        elseif s == 256 then break
        else
          s = s - 257
          local len = LBASE[s + 1] + bits(LEXT[s + 1])
          local d = decode(dt)
          local dist = DBASE[d + 1] + bits(DEXT[d + 1])
          local from = on - dist
          for k = 1, len do on = on + 1; out[on] = out[from + k] end
        end
      end
    else
      error('inflate: bad block type', 0)
    end
  until final == 1
  -- bytes -> string in chunks (string.char has an argument limit)
  local parts, CH = {}, 4096
  for i = 1, on, CH do parts[#parts + 1] = char(unpack(out, i, math.min(i + CH - 1, on))) end
  local s = concat(parts)
  if size and #s ~= size then return nil, ('inflate: size %d, expected %d'):format(#s, size) end
  return s
end
M._lua_inflate = lua_inflate

function M.inflate(data, size)
  if Z then
    local s, e = zlib_inflate(data, size)
    if s then return s end
  end
  local ok, s, e = pcall(lua_inflate, data, size)
  if not ok then return nil, s end
  return s, e
end

-- ---------------------------------------------------------------- zip container
local function u16(s, p) local a, b = byte(s, p, p + 1) return a + b * 256 end
local function u32(s, p) local a, b, c, d = byte(s, p, p + 3) return a + b * 256 + c * 65536 + d * 16777216 end

local Zip = {}
Zip.__index = Zip

function M.open(path)
  local f, err = io.open(path, 'rb')
  if not f then return nil, err end
  local fsize = f:seek('end')
  local tail = math.min(fsize, 65557)
  f:seek('set', fsize - tail)
  local buf = f:read(tail) or ''
  local eocd
  for i = #buf - 21, 1, -1 do
    if byte(buf, i) == 0x50 and sub(buf, i, i + 3) == 'PK\5\6' then eocd = i break end
  end
  if not eocd then f:close() return nil, 'not a zip file (no end of central directory)' end
  local count = u16(buf, eocd + 10)
  local cdsize, cdoff = u32(buf, eocd + 12), u32(buf, eocd + 16)
  if cdoff == 0xFFFFFFFF or count == 0xFFFF then f:close() return nil, 'zip64 archives are not supported' end
  f:seek('set', cdoff)
  local cd = f:read(cdsize) or ''
  local entries, p = {}, 1
  for _ = 1, count do
    if sub(cd, p, p + 3) ~= 'PK\1\2' then break end
    local flags, method = u16(cd, p + 8), u16(cd, p + 10)
    local mtime, mdate = u16(cd, p + 12), u16(cd, p + 14)
    local crc, csize, size = u32(cd, p + 16), u32(cd, p + 20), u32(cd, p + 24)
    local nlen, xlen, clen = u16(cd, p + 28), u16(cd, p + 30), u16(cd, p + 32)
    local lho = u32(cd, p + 42)
    local name = sub(cd, p + 46, p + 45 + nlen):gsub('\\', '/')
    entries[#entries + 1] = {
      name = name, flags = flags, method = method, crc = crc, csize = csize, size = size, offset = lho,
      dir = name:sub(-1) == '/',
      encrypted = band(flags, 1) ~= 0,
      mtime = os.time({ year = 1980 + rshift(mdate, 9), month = band(rshift(mdate, 5), 15), day = band(mdate, 31),
        hour = rshift(mtime, 11), min = band(rshift(mtime, 5), 63), sec = band(mtime, 31) * 2 }),
    }
    p = p + 46 + nlen + xlen + clen
  end
  return setmetatable({ path = path, f = f, entries = entries, size = fsize }, Zip)
end

function Zip:read(e)
  if e.dir then return '' end
  if e.encrypted then return nil, 'encrypted entry' end
  if e.size == 0xFFFFFFFF or e.csize == 0xFFFFFFFF then return nil, 'zip64 entry' end
  local f = self.f
  f:seek('set', e.offset)
  local h = f:read(30)
  if not h or sub(h, 1, 4) ~= 'PK\3\4' then return nil, 'bad local header' end
  f:seek('set', e.offset + 30 + u16(h, 27) + u16(h, 29))
  local raw = e.csize > 0 and f:read(e.csize) or ''
  local data, err
  if e.method == 0 then data = raw
  elseif e.method == 8 then data, err = M.inflate(raw, e.size)
  else return nil, 'compression method ' .. e.method .. ' not supported' end
  if not data then return nil, err end
  if Z and e.crc and zlib_crc(data) ~= e.crc then return nil, 'CRC mismatch' end
  return data
end

function Zip:close() if self.f then self.f:close() self.f = nil end end

-- safe relative path: no absolute paths, no drive letters, no '..'
function M.safe_name(name)
  name = name:gsub('\\', '/'):gsub('^%a:', ''):gsub('^/+', '')
  for part in name:gmatch('[^/]+') do if part == '..' then return nil end end
  if name == '' then return nil end
  return name
end

local function mkdirs(path, lfs)
  local cur = path:sub(1, 1) == '/' and '' or '.'
  for part in path:gmatch('[^/]+') do
    cur = cur .. '/' .. part
    if lfs then if lfs.attributes(cur, 'mode') ~= 'directory' then lfs.mkdir(cur) end
    elseif createDirectory then createDirectory(cur) end
  end
end
M.mkdirs = mkdirs

-- extract(zip, dest[, filter(name) -> bool]) -> files written, list of skipped {name, reason}
function M.extract(path, dest, filter)
  local okl, lfs = pcall(require, 'lfs')
  if not okl then lfs = nil end
  local z, err = M.open(path)
  if not z then return nil, err end
  mkdirs(dest, lfs)
  local n, skipped = 0, {}
  for _, e in ipairs(z.entries) do
    local name = M.safe_name(e.name)
    if not name then skipped[#skipped + 1] = { e.name, 'unsafe path' }
    elseif filter and not filter(name, e) then -- skipped on purpose
    elseif e.dir then mkdirs(dest .. '/' .. name, lfs)
    else
      local data, rerr = z:read(e)
      if not data then skipped[#skipped + 1] = { name, rerr }
      else
        local d = (dest .. '/' .. name):match('^(.*)/[^/]*$')
        if d then mkdirs(d, lfs) end
        local w = io.open(dest .. '/' .. name, 'wb')
        if w then w:write(data); w:close(); n = n + 1 else skipped[#skipped + 1] = { name, 'cannot write' } end
      end
    end
  end
  z:close()
  return n, skipped
end

return M
