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
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" const unsigned char mp_monetloader_blob[];
extern "C" const unsigned char mp_monetloader_blob_end[];
// 5.01.06: MP's own moonloader entry scripts (src/embed/moonloader_core_blob.S)
extern "C" const unsigned char mp_core_lsl[], mp_core_lsl_end[], mp_core_mods[], mp_core_mods_end[];
extern "C" const unsigned char mp_core_zip[], mp_core_zip_end[], mp_core_pcp[], mp_core_pcp_end[], mp_core_mimgui[], mp_core_mimgui_end[];
namespace MP { const char* DataDir(); }

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

// 4.13.64: every separate libmonetloader.so (path from /proc/self/maps = the copy AML loaded, + AML's mods folders) -> .old
static bool s_bSeparateMoved = false;
static bool MoveOne(const std::string& p)
{
    if(p.empty() || access(p.c_str(), F_OK) != 0) return false;
    std::string to = p + ".old";
    unlink(to.c_str());
    if(rename(p.c_str(), to.c_str()) == 0) { logger->Info("MonetLoader: %s -> %s", p.c_str(), to.c_str()); return true; }
    if(unlink(p.c_str()) == 0) { logger->Info("MonetLoader: %s deleted", p.c_str()); return true; }
    logger->Error("MonetLoader: can't move %s (errno %d)", p.c_str(), errno);
    return false;
}
static int MoveSeparateAway()
{
    std::vector<std::string> paths;
    if(FILE* m = fopen("/proc/self/maps", "r"))
    {
        char l[1024];
        while(fgets(l, sizeof(l), m))
        {
            char* q = strchr(l, '/'); if(!q) continue;
            q[strcspn(q, "\r\n")] = 0;
            const char* b = strrchr(q, '/'); b = b ? b + 1 : q;
            if(strcmp(b, "libmonetloader.so")) continue;
            bool have = false; for(auto& x : paths) if(x == q) have = true;
            if(!have) paths.push_back(q);
        }
        fclose(m);
    }
    if(const char* dp = aml->GetDataPath())   // the game's folders: <data>/mods, Android/data/<pkg>/mods, AMLMods/<pkg>
    {
        std::string s = dp; if(!s.empty() && s.back() == '/') s.pop_back();
        paths.push_back(s + "/libmonetloader.so");
        paths.push_back(s + "/mods/libmonetloader.so");
        size_t at = s.rfind("/files");
        std::string base = at != std::string::npos ? s.substr(0, at) : s;
        paths.push_back(base + "/mods/libmonetloader.so");
        const char* pk = strrchr(base.c_str(), '/');
        if(pk && pk[1])
        {
            std::string pkg = pk + 1;
            paths.push_back("/storage/emulated/0/Android/data/" + pkg + "/mods/libmonetloader.so");
            paths.push_back("/sdcard/Android/data/" + pkg + "/mods/libmonetloader.so");
            paths.push_back("/storage/emulated/0/AMLMods/" + pkg + "/libmonetloader.so");
            paths.push_back("/sdcard/AMLMods/" + pkg + "/libmonetloader.so");
        }
    }
    int n = 0;
    for(auto& p : paths) if(MoveOne(p)) ++n;
    return n;
}
bool SeparateMoved() { return s_bSeparateMoved; }

// 5.01.06: moonloader/lua_scripts_loader.lua, mods_loader.lua, lib/mp_zip.lua and lib/mp_pcpaths.lua come with libMP.
// Players who only replaced libMP32.so / libMP64.so had no lua_scripts_loader.lua, so nothing in moonloader/lua_scripts/
// (HassleHUD...) started and the Script Manager listed none of it. Each file is written when it is missing, or when
// the phone has an older MP copy of it (same signature, lower version). A file without MP's signature is never touched.
static std::string CoreVersion(const std::string& s, const char* sig)
{
    const std::string head = s.substr(0, 4096);
    if(head.find(sig) == std::string::npos) return "";
    size_t p = head.find("script_version('");
    if(p != std::string::npos) { p += 16; size_t e = head.find('\'', p); return e == std::string::npos ? "0" : head.substr(p, e - p); }
    p = head.find(sig) + strlen(sig);   // "-- mp_zip 1.0:"
    while(p < head.size() && head[p] == ' ') ++p;
    size_t e = p; while(e < head.size() && (isdigit((unsigned char)head[e]) || head[e] == '.')) ++e;
    return e > p ? head.substr(p, e - p) : "0";
}
static int CoreVerCmp(const std::string& a, const std::string& b)   // 1.10-mp > 1.9-mp
{
    size_t i = 0, j = 0;
    while(i < a.size() || j < b.size())
    {
        long x = 0, y = 0;
        while(i < a.size() && isdigit((unsigned char)a[i])) x = x * 10 + (a[i++] - '0');
        while(j < b.size() && isdigit((unsigned char)b[j])) y = y * 10 + (b[j++] - '0');
        if(x != y) return x < y ? -1 : 1;
        if(i < a.size() && a[i] == '.') ++i; else i = a.size();
        if(j < b.size() && b[j] == '.') ++j; else j = b.size();
    }
    return 0;
}
static void InstallCore()
{
    const char* dd = MP::DataDir();
    if(!dd || !dd[0]) return;
    const std::string root = std::string(dd) + "/moonloader";
    mkdir(root.c_str(), 0777); mkdir((root + "/lib").c_str(), 0777); mkdir((root + "/lua_scripts").c_str(), 0777);
    mkdir((root + "/mods").c_str(), 0777); mkdir((root + "/config").c_str(), 0777);
    struct Core { const char* rel; const unsigned char* a; const unsigned char* b; const char* sig; const char* stock = nullptr; };   // stock: an unmarked older MP copy
    const Core cores[] = {
        { "lua_scripts_loader.lua", mp_core_lsl, mp_core_lsl_end, "script_name('MP Lua Scripts Loader')" },
        { "mods_loader.lua",        mp_core_mods, mp_core_mods_end, "script_name('MP Mods Loader')" },
        { "lib/mp_zip.lua",         mp_core_zip, mp_core_zip_end, "-- mp_zip " },
        { "lib/mp_pcpaths.lua",     mp_core_pcp, mp_core_pcp_end, "-- mp_pcpaths " },
        // 5.02.20: mimgui's frame guard (HassleHUD). Only replaces an existing mimgui/init.lua - the rest of mimgui comes from assets/MP
        { "lib/mimgui/init.lua",    mp_core_mimgui, mp_core_mimgui_end, "-- mp_mimgui ", "mimgui._VERSION = '2.0.1'" } };
    for(const Core& c : cores)
    {
        const std::string mine((const char*)c.a, (size_t)(c.b - c.a));
        const std::string path = root + "/" + c.rel;
        std::string have; bool exists = false;
        if(FILE* f = fopen(path.c_str(), "rb"))
        {
            exists = true; char buf[8192]; size_t n;
            while((n = fread(buf, 1, sizeof(buf), f)) > 0) have.append(buf, n);
            fclose(f);
        }
        if(exists && have == mine) continue;
        if(!exists && c.stock) continue;
        if(exists)
        {
            std::string hv = CoreVersion(have, c.sig); const std::string mv = CoreVersion(mine, c.sig);
            if(hv.empty() && c.stock && have.find(c.stock) != std::string::npos) hv = "0";
            if(hv.empty()) { logger->Info("MonetLoader: moonloader/%s is not MP's - left as it is", c.rel); continue; }
            if(CoreVerCmp(hv, mv) >= 0) continue;   // the phone has the same or a newer MP copy (assets/MP update)
        }
        if(WriteFile(path.c_str(), c.a, (size_t)(c.b - c.a)))
            logger->Info("MonetLoader: moonloader/%s %s (built into MP)", c.rel, exists ? "updated" : "installed");
    }
}

static void MonetLogLine(int level, const char* text)   // 5.02.44: called by MonetLoader's logger (any thread)
{
    if(!text) return;
    if(level >= 4) logger->Error("[ML] %s", text); else logger->Info("[ML] %s", text);
}

void Start()
{
    if(s_hLib) return;
    ConfigEntry* e = cfg->Bind("MonetLoader", true, "MP");   // written to TYFMC.MP.ini so it can be switched off
    const bool on = e->GetBool(); delete e; cfg->Save();
    if(!on) { logger->Info("MonetLoader: off (MonetLoader = 0 in TYFMC.MP.ini)"); return; }
    InstallCore();   // 5.01.06: lua_scripts_loader.lua + mods_loader.lua before MonetLoader loads moonloader/
    if(aml->HasMod("com.monetloader.aml") || dlopen("libmonetloader.so", RTLD_NOW | RTLD_NOLOAD))
    {
        // 4.13.64: no separate MonetLoader any more - MonetLoader for MP 4.0.0-mp (mimgui fix) is built into libMP.
        // An old separate libmonetloader.so (the one that gave "undefined symbol: luaJIT_BC_mimgui_cdefs") is already
        // running for this start (two MonetLoaders can't run), so it is moved out of the mods folder now: from the
        // next start only the built-in copy runs, and there is never a duplicate.
        const int moved = MoveSeparateAway();
        if(moved > 0) logger->Info("MonetLoader: an old separate libmonetloader.so was found and moved to *.old (%d file%s). It runs one last time now - restart the game once and the built-in MonetLoader for MP 4.0.0-mp is used.", moved, moved == 1 ? "" : "s");
        else logger->Info("MonetLoader: a separate libmonetloader.so is running and could not be moved - delete it from the mods folder, MonetLoader for MP 4.0.0-mp is built into libMP.");
        s_bSeparateMoved = moved > 0;
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
    {   // 5.02.44: MonetLoader uses MP's data folder (moonloader.log + scripts in the same MP/moonloader MP reads)
        std::string root = MP::DataDir() ? MP::DataDir() : "";
        const size_t k = root.rfind("/TYFMC/MP");
        if(k != std::string::npos)
        {
            root.resize(k);
            while(!root.empty() && root.back() == '/') root.pop_back();
            if(root.size() > 6 && root.compare(root.size() - 6, 6, "/files") == 0) root.resize(root.size() - 6);
            setenv("MP_DATA_ROOT", root.c_str(), 1);
            char fn[32]; snprintf(fn, sizeof(fn), "%llx", (unsigned long long)(uintptr_t)&MonetLogLine);
            setenv("MP_LOG_FN", fn, 1);   // MonetLoader mirrors its errors / [LuaScripts] lines into this log
            logger->Info("MonetLoader: data root %s (moonloader.log -> %s/moonloader/moonloader.log)", root.c_str(), MP::DataDir());
        }
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
