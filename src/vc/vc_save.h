// 5.00.02: Vice City save guard (no game headers - host test: test/vc_save_test.cpp)
//
// Crash report (realme RE6054, Android 16, GTA VC 32-bit): SIGABRT "Scudo ERROR: invalid chunk state when
// deallocating" in OS_FileClose <- CFileMgr::CloseFile <- CheckDataNotCorrupt <- CheckSlotDataValid <- MainGameTick,
// while the game checked save slot GTA3sf9.b. CheckDataNotCorrupt reads every block of the save file into one fixed
// work buffer using the block size written in the file. A damaged save (or one from another game version / PC) has
// a wrong block size, the read runs past the buffer and the heap breaks - the game dies on the next free().
//
// VcSaveScan() checks a save file the way the game reads it, before the game does:
//   [uint32 size][size bytes] ... [uint32 checksum]
// 1 = let the game read it, 0 = a block size runs past the end of the file and the checksum is wrong (skip the slot).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline int VcSaveScan(const uint8_t* d, size_t n)
{
    if(!d || n < 8) return 0;                       // too short to be a save
    uint32_t stored; memcpy(&stored, d + n - 4, 4);
    uint32_t sumAll = 0;                            // the checksum: the sum of every byte before it ...
    for(size_t i = 0; i < n - 4; ++i) sumAll += d[i];
    if(stored == sumAll) return 1;
    int overflows = 0;
    for(int align = 0; align < 2; ++align)          // block sizes as written, or rounded up to 4 (reVC align4bytes)
    {
        size_t pos = 0; uint32_t sumData = 0; bool blocksOk = true, overflow = false;
        while(pos < n - 4)
        {
            if(n - 4 - pos < 4) { blocksOk = false; break; }
            uint32_t sz; memcpy(&sz, d + pos, 4); pos += 4;
            if(align) sz = (sz + 3u) & ~3u;
            if(sz > n - 4 - pos) { blocksOk = false; overflow = true; break; }   // the block runs past the end = overflow
            if(sz == 0) { blocksOk = false; break; }
            for(uint32_t k = 0; k < sz; ++k) sumData += d[pos + k];
            pos += sz;
        }
        if(blocksOk && pos == n - 4) return 1;      // ... or the blocks end exactly before it
        if(stored == sumData) return 1;
        if(overflow) ++overflows;
    }
    return overflows == 2 ? 0 : 1;   // only a block size past the end of the file (both ways) is skipped; the rest = the game decides
}
