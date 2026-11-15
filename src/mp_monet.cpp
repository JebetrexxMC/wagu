// MP 4.13.32: MonetLoader for MP built into libMP32.so / libMP64.so.
// The library (monetloader/<abi>/libmonetloader.so, src/embed/monetloader_blob.S) is written to the game's private
// folder (aml->GetDataPath(), the same kind of folder AML runs the mods from) and started like AML starts a mod
// (OnModLoad). Off with [MP] MonetLoader = 0 in TYFMC.MP.ini. A separate libmonetloader.so in the mods folder wins
// (only one MonetLoader may run) - MP then does not start its own copy.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include "game/game.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" const unsigned char mp_monetloader_blob[];
extern "C" const unsigned char mp_monetloader_blob_end[];

namespace MonetEmbed
{
static void* s_hLib = nullptr;
static std::string s_Path;

// 4.13.62: mimgui (lib/mimgui/imgui.lua), lib/imgui.lua, monethook.lua and other mods call ffi.load("monetloader")
// = dlopen("libmonetloader.so"). MP's built-in copy is libMP_monetloader.so in the game's private folder, which a bare
// name never finds on Android, so every mimgui mod (HG menu...) died with "undefined symbol: luaJIT_BC_mimgui_cdefs"
// (a stale dlerror). The built-in copy's own dlopen import now hands those names its own handle.
static void* (*s_RealDlopen)(const char*, int) = nullptr;
static bool IsMonetName(const char* name)
{
    if(!name) return false;
    const char* b = strrchr(name, '/'); b = b ? b + 1 : name;
    return !strcmp(b, "libmonetloader.so") || !strcmp(b, "monetloader") || !strcmp(b, "libmonetloader") ||
           !strcmp(b, "monetloader.so") || !strcmp(b, "libMP_monetloader.so");
}
static void* MlDlopen(const char* name, int flags)
{
    if(s_hLib && IsMonetName(name))
    {
        void* h = s_RealDlopen ? s_RealDlopen(s_Path.c_str(), RTLD_NOW | RTLD_NOLOAD) : nullptr;   // + one reference, same handle
        return h ? h : s_hLib;
    }
    return s_RealDlopen ? s_RealDlopen(name, flags) : dlopen(name, flags);
}

static bool SameFile(const char* path, const unsigned char* data, size_t n)
{
    struct stat st;
    if(stat(path, &st) != 0 || (size_t)st.st_size != n) return false;
    FILE* f = fopen(path, "rb");
    if(!f) return false;
    std::vector<unsigned char> buf(1 << 16);
    size_t off = 0; bool same = true;
    while(same && off < n)
    {
        size_t want = n - off < buf.size() ? n - off : buf.size();
        size_t got = fread(buf.data(), 1, want, f);
        if(got != want || memcmp(buf.data(), data + off, got) != 0) same = false;
        off += got;
    }
    fclose(f);
    return same;
}

static bool WriteFile(const char* path, const unsigned char* data, size_t n)
{
    std::string tmp = std::string(path) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if(!f) { logger->Error("MonetLoader: can't write %s (errno %d)", tmp.c_str(), errno); return false; }
    bool ok = fwrite(data, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    if(!ok) { unlink(tmp.c_str()); logger->Error("MonetLoader: writing %s failed (full storage?)", tmp.c_str()); return false; }
    chmod(tmp.c_str(), 0700);
    if(rename(tmp.c_str(), path) != 0) { unlink(tmp.c_str()); logger->Error("MonetLoader: rename to %s failed (errno %d)", path, errno); return false; }
    return true;
}

void Start()
{
    if(s_hLib) return;
    ConfigEntry* e = cfg->Bind("MonetLoader", true, "MP");   // written to TYFMC.MP.ini so it can be switched off
    const bool on = e->GetBool(); delete e; cfg->Save();
    if(!on) { logger->Info("MonetLoader: off (MonetLoader = 0 in TYFMC.MP.ini)"); return; }
    if(aml->HasMod("com.monetloader.aml") || dlopen("libmonetloader.so", RTLD_NOW | RTLD_NOLOAD))
    {
        logger->Info("MonetLoader: a separate libmonetloader.so is in the mods folder - that one runs, MP's built-in copy is not started. Delete it from the mods folder to use the built-in MonetLoader for MP.");
        return;
    }
    const size_t n = (size_t)(mp_monetloader_blob_end - mp_monetloader_blob);
    if(n < 4096 || memcmp(mp_monetloader_blob, "\x7f" "ELF", 4) != 0) { logger->Error("MonetLoader: the built-in copy is missing"); return; }
    const char* dp = aml->GetDataPath();
    if(!dp || !dp[0]) { logger->Error("MonetLoader: no private game folder"); return; }
    std::string dir = dp;
    if(dir.back() != '/') dir += '/';
    std::string path = dir + "libMP_monetloader.so";
    if(!SameFile(path.c_str(), mp_monetloader_blob, n))
    {
        if(!WriteFile(path.c_str(), mp_monetloader_blob, n)) return;
        logger->Info("MonetLoader: built-in copy written to %s (%u KB)", path.c_str(), (unsigned)(n / 1024));
    }
    s_hLib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if(!s_hLib) { const char* er = dlerror(); logger->Error("MonetLoader: can't load the built-in copy: %s", er ? er : "?"); return; }
    typedef void (*EntryFn)();
    EntryFn entry = (EntryFn)dlsym(s_hLib, "OnModLoad");
    if(!entry) { logger->Error("MonetLoader: the built-in copy has no OnModLoad"); return; }
    s_Path = path;
    {
        void** slot = Game::FindPltSlot((void*)entry, "dlopen");
        if(slot && *slot && aml->HookPLT((void*)slot, (void*)&MlDlopen, (void**)&s_RealDlopen))
            logger->Info("MonetLoader: ffi.load(\"monetloader\") -> the built-in copy (mimgui mods)");
        else logger->Error("MonetLoader: no dlopen slot - ffi.load(\"monetloader\") may fail in mimgui mods");
    }
    logger->Info("MonetLoader for MP: starting (built into MP). Scripts: files/TYFMC/MP/moonloader, log: moonloader/moonloader.log");
    entry();
    logger->Info("MonetLoader for MP: started");
}

// 4.13.33: the running MonetLoader (MP's built-in copy, or a separate libmonetloader.so in the mods folder)
void* Handle()
{
    if(s_hLib) return s_hLib;
    static void* s_hExt = nullptr;
    if(!s_hExt) s_hExt = dlopen("libmonetloader.so", RTLD_NOW | RTLD_NOLOAD);
    return s_hExt;
}
}
