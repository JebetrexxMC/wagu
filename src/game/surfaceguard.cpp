// 5.02.66: SurfaceGuard - the SurfaceInfos_c getters of libGTASA (32-bit) checked and, when one was damaged, the game's
// calls to it moved to MP's own copy.
//
// Crash (realme RE6054, Android 16, 32-bit, 70 AML mods, MP 5.02.64 and 5.02.65, while a save loads / fades in):
//   SIGSEGV SEGV_MAPERR, fault address 0x6C, PC libGTASA + 0x41DFB8, R0 = g_surfaceInfos, R1 = 6, LR in the player's
//   on-foot code. In GTA SA 2.00 (32-bit) libGTASA + 0x41DFB8 is the first instruction of
//   SurfaceInfos_c::CantSprintOn(unsigned int) (the crash log names the function before it, CreatesSparks). That
//   function is 18 bytes, "add r1; add r0; ldr r0, [r0, #0x92]; ubfx; bx lr": its first instruction can't read
//   address 0x6C, and R0 / R1 are still the arguments. So the bytes there were not the game's any more - a mod wrote
//   into it (a hook patch too long for these tiny functions spills into the next one).
//   5.02.65 hooked CreatesSparks, which is never called through the game's GOT, so it never saw this crash.
//
// What it does (no code of the game is written, no signal handler is needed):
//   * reads the code of the 42 bit getters and compares it with the game's original bytes (table below);
//   * original / a clean hook jump / a "return N" patch -> left alone (the hook owner is logged);
//   * anything else = damaged: the game's GOT slot of that function is pointed at MP's copy (same result, read
//     from g_surfaceInfos like the game does). The damaged bytes stay - they may be the tail of another mod's hook;
//   * runs when MP loads, after all mods loaded, on the first game frame and every ~10 s.
// The table is only trusted when most getters match it (else: log only).
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <elf.h>
#include <link.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include "surfaceguard.h"

#ifdef AML32
namespace
{
    struct Getter { const char* name; uint16_t off; uint8_t bits, lsb, width; const char* hex; };
    #define S(n, o, b, l, w, h) { n, o, b, l, w, h }
    const Getter kGetters[] =
    {
    S("_ZN14SurfaceInfos_c16GetAdhesionGroupEj", 0x92, 32, 0, 3, "01eb410100eb8100d0f8920000f007007047"),
    S("_ZN14SurfaceInfos_c15GetSkidmarkTypeEj", 0x92, 32, 3, 2, "01eb410100eb8100d0f89200c0f3c1007047"),
    S("_ZN14SurfaceInfos_c17GetFrictionEffectEj", 0x92, 32, 5, 3, "01eb410100eb8100d0f89200c0f342107047"),
    S("_ZN14SurfaceInfos_c11GetBulletFxEj", 0x92, 32, 8, 3, "01eb410100eb8100d0f89200c0f302207047"),
    S("_ZN14SurfaceInfos_c13IsSoftLandingEj", 0x92, 32, 11, 1, "01eb410100eb8100d0f89200c0f3c0207047"),
    S("_ZN14SurfaceInfos_c12IsSeeThroughEj", 0x92, 32, 12, 1, "01eb410100eb8100d0f89200c0f300307047"),
    S("_ZN14SurfaceInfos_c14IsShootThroughEj", 0x92, 32, 13, 1, "01eb410100eb8100d0f89200c0f340307047"),
    S("_ZN14SurfaceInfos_c6IsSandEj", 0x92, 32, 14, 1, "01eb410100eb8100d0f89200c0f380307047"),
    S("_ZN14SurfaceInfos_c7IsWaterEj", 0x92, 32, 15, 1, "01eb410100eb8100d0f89200c0f3c0307047"),
    S("_ZN14SurfaceInfos_c14IsShallowWaterEj", 0x94, 16, 0, 1, "01eb410100eb8100b0f8940000f001007047"),
    S("_ZN14SurfaceInfos_c7IsBeachEj", 0x92, 32, 17, 1, "01eb410100eb8100d0f89200c0f340407047"),
    S("_ZN14SurfaceInfos_c12IsSteepSlopeEj", 0x92, 32, 18, 1, "01eb410100eb8100d0f89200c0f380407047"),
    S("_ZN14SurfaceInfos_c7IsGlassEj", 0x92, 32, 19, 1, "01eb410100eb8100d0f89200c0f3c0407047"),
    S("_ZN14SurfaceInfos_c8IsStairsEj", 0x92, 32, 20, 1, "01eb410100eb8100d0f89200c0f300507047"),
    S("_ZN14SurfaceInfos_c11IsSkateableEj", 0x92, 32, 21, 1, "01eb410100eb8100d0f89200c0f340507047"),
    S("_ZN14SurfaceInfos_c10IsPavementEj", 0x92, 32, 22, 1, "01eb410100eb8100d0f89200c0f380507047"),
    S("_ZN14SurfaceInfos_c12GetRoughnessEj", 0x92, 32, 23, 2, "01eb410100eb8100d0f89200c0f3c1507047"),
    S("_ZN14SurfaceInfos_c15GetFlammabilityEj", 0x92, 32, 25, 2, "01eb410100eb8100d0f89200c0f341607047"),
    S("_ZN14SurfaceInfos_c13CreatesSparksEj", 0x92, 32, 27, 1, "01eb410100eb8100d0f89200c0f3c0607047"),
    S("_ZN14SurfaceInfos_c12CantSprintOnEj", 0x92, 32, 28, 1, "01eb410100eb8100d0f89200c0f300707047"),
    S("_ZN14SurfaceInfos_c15LeavesFootstepsEj", 0x92, 32, 29, 1, "01eb410100eb8100d0f89200c0f340707047"),
    S("_ZN14SurfaceInfos_c16ProducesFootDustEj", 0x92, 32, 30, 1, "01eb410100eb8100d0f89200c0f380707047"),
    S("_ZN14SurfaceInfos_c13MakesCarDirtyEj", 0x92, 32, 31, 1, "01eb410100eb8100d0f89200c00f7047"),
    S("_ZN14SurfaceInfos_c13MakesCarCleanEj", 0x96, 32, 0, 1, "01eb410100eb8100d0f8960000f001007047"),
    S("_ZN14SurfaceInfos_c17CreatesWheelGrassEj", 0x96, 32, 1, 1, "01eb410100eb8100d0f89600c0f340007047"),
    S("_ZN14SurfaceInfos_c18CreatesWheelGravelEj", 0x96, 32, 2, 1, "01eb410100eb8100d0f89600c0f380007047"),
    S("_ZN14SurfaceInfos_c15CreatesWheelMudEj", 0x96, 32, 3, 1, "01eb410100eb8100d0f89600c0f3c0007047"),
    S("_ZN14SurfaceInfos_c16CreatesWheelDustEj", 0x96, 32, 4, 1, "01eb410100eb8100d0f89600c0f300107047"),
    S("_ZN14SurfaceInfos_c16CreatesWheelSandEj", 0x96, 32, 5, 1, "01eb410100eb8100d0f89600c0f340107047"),
    S("_ZN14SurfaceInfos_c17CreatesWheelSprayEj", 0x96, 32, 6, 1, "01eb410100eb8100d0f89600c0f380107047"),
    S("_ZN14SurfaceInfos_c13CreatesPlantsEj", 0x96, 32, 7, 1, "01eb410100eb8100d0f89600c0f3c0107047"),
    S("_ZN14SurfaceInfos_c14CreatesObjectsEj", 0x96, 32, 8, 1, "01eb410100eb8100d0f89600c0f300207047"),
    S("_ZN14SurfaceInfos_c8CanClimbEj", 0x96, 32, 9, 1, "01eb410100eb8100d0f89600c0f340207047"),
    S("_ZN14SurfaceInfos_c15IsAudioConcreteEj", 0x96, 32, 10, 1, "01eb410100eb8100d0f89600c0f380207047"),
    S("_ZN14SurfaceInfos_c12IsAudioGrassEj", 0x96, 32, 11, 1, "01eb410100eb8100d0f89600c0f3c0207047"),
    S("_ZN14SurfaceInfos_c11IsAudioSandEj", 0x96, 32, 12, 1, "01eb410100eb8100d0f89600c0f300307047"),
    S("_ZN14SurfaceInfos_c13IsAudioGravelEj", 0x96, 32, 13, 1, "01eb410100eb8100d0f89600c0f340307047"),
    S("_ZN14SurfaceInfos_c11IsAudioWoodEj", 0x96, 32, 14, 1, "01eb410100eb8100d0f89600c0f380307047"),
    S("_ZN14SurfaceInfos_c12IsAudioWaterEj", 0x96, 32, 15, 1, "01eb410100eb8100d0f89600c0f3c0307047"),
    S("_ZN14SurfaceInfos_c12IsAudioMetalEj", 0x98, 16, 0, 1, "01eb410100eb8100b0f8980000f001007047"),
    S("_ZN14SurfaceInfos_c16IsAudioLongGrassEj", 0x96, 32, 17, 1, "01eb410100eb8100d0f89600c0f340407047"),
    S("_ZN14SurfaceInfos_c11IsAudioTileEj", 0x96, 32, 18, 1, "01eb410100eb8100d0f89600c0f380407047"),
    };
    #undef S
    enum { N = sizeof(kGetters) / sizeof(kGetters[0]), MAX_SURF = 195 };

    struct State
    {
        void*     handle = nullptr;
        uintptr_t base = 0;
        const ElfW(Phdr)* phdr = nullptr; int phnum = 0;
        uint8_t*  code[N] = {};
        uint8_t   ref[N][20] = {};
        uint8_t   len[N] = {};
        bool      moved[N] = {};
        char      status[N] = {};   // 'o' original, 'h' hooked, 'r' return patch, 'd' damaged
        uint8_t   good[N][20] = {};  // 5.02.70: the last good bytes (original or a clean hook) - put back when they get damaged
        bool      haveGood[N] = {};
        int       restored = 0;
        int       checks = 0, movedCount = 0;
        bool      ready = false;
    } s;

    int Hex(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

    template<int I> unsigned Safe(const uint8_t* self, unsigned id)
    {
        if(!self || (uintptr_t)self < 0x10000 || id >= MAX_SURF) return 0;
        const Getter& g = kGetters[I];
        const uint8_t* p = self + id * 12 + g.off;
        uint32_t v = 0;
        if(g.bits == 16) { uint16_t h; memcpy(&h, p, 2); v = h; } else memcpy(&v, p, 4);
        return (v >> g.lsb) & ((g.width >= 32) ? 0xFFFFFFFFu : ((1u << g.width) - 1));
    }
    template<int I> struct Tab { static void Fill(void** t) { t[I - 1] = (void*)&Safe<I - 1>; Tab<I - 1>::Fill(t); } };
    template<> struct Tab<0> { static void Fill(void**) {} };
    void* g_safe[N];

    int FindCb(struct dl_phdr_info* info, size_t, void* data)
    {
        if(info->dlpi_addr != s.base) return 0;
        s.phdr = info->dlpi_phdr; s.phnum = info->dlpi_phnum;
        (void)data; return 1;
    }

    const char* Owner(uintptr_t a, char* buf, size_t n)
    {
        Dl_info di; memset(&di, 0, sizeof(di));
        if(!a || !dladdr((void*)a, &di) || !di.dli_fname) { snprintf(buf, n, "%p", (void*)a); return buf; }
        const char* f = strrchr(di.dli_fname, '/');
        snprintf(buf, n, "%s+0x%X", f ? f + 1 : di.dli_fname, (unsigned)(a - (uintptr_t)di.dli_fbase));
        return buf;
    }

    uint16_t H16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

    // 'o' / 'h' / 'r' / 'd'; for hooks *target = where it jumps
    char Classify(int i, uintptr_t* target)
    {
        const uint8_t* c = s.code[i];
        *target = 0;
        if(!memcmp(c, s.ref[i], s.len[i])) return 'o';
        const uint8_t* p = c;
        if(H16(p) == 0xBF00) p += 2;   // nop before an aligned ldr.w pc
        if(H16(p) == 0xF8DF && H16(p + 2) == 0xF000)   // ldr.w pc, [pc, #0] + address
        {
            uintptr_t lit = (((uintptr_t)p + 4) & ~(uintptr_t)3);
            memcpy(target, (const void*)lit, 4);
            return 'h';
        }
        uint16_t a = H16(c), b = H16(c + 2);
        if((a & 0xF800) == 0xF000 && (b & 0xD000) == 0x9000) return 'h';   // b.w
        if((a & 0xFF00) == 0x2000 && b == 0x4770) return 'r';               // movs r0, #N; bx lr
        return 'd';
    }

    bool MoveGot(const char* name, void* to)
    {
        if(!s.phdr) return false;
        const ElfW(Dyn)* dyn = nullptr;
        for(int i = 0; i < s.phnum; ++i) if(s.phdr[i].p_type == PT_DYNAMIC) dyn = (const ElfW(Dyn)*)(s.base + s.phdr[i].p_vaddr);
        if(!dyn) return false;
        const ElfW(Sym)* symtab = nullptr; const char* strtab = nullptr; uintptr_t jmprel = 0; size_t jmpsz = 0;
        for(const ElfW(Dyn)* d = dyn; d->d_tag != DT_NULL; ++d)
        {
            if(d->d_tag == DT_SYMTAB) symtab = (const ElfW(Sym)*)(s.base + d->d_un.d_ptr);
            else if(d->d_tag == DT_STRTAB) strtab = (const char*)(s.base + d->d_un.d_ptr);
            else if(d->d_tag == DT_JMPREL) jmprel = s.base + d->d_un.d_ptr;
            else if(d->d_tag == DT_PLTRELSZ) jmpsz = d->d_un.d_val;
        }
        if(!symtab || !strtab || !jmprel) return false;
        long page = sysconf(_SC_PAGESIZE); if(page <= 0) page = 4096;
        const ElfW(Rel)* r = (const ElfW(Rel)*)jmprel;
        bool ok = false;
        for(size_t k = 0; k < jmpsz / sizeof(ElfW(Rel)); ++k)
        {
            uint32_t si = ELF32_R_SYM(r[k].r_info);
            if(!si || strcmp(strtab + symtab[si].st_name, name)) continue;
            void** slot = (void**)(s.base + r[k].r_offset);
            uintptr_t pg = (uintptr_t)slot & ~(uintptr_t)(page - 1);
            if(mprotect((void*)pg, page, PROT_READ | PROT_WRITE) != 0) continue;
            *slot = to;
            ok = true;
        }
        return ok;
    }

    // 5.02.70: put the last good bytes back (the game calls these getters with a direct BL, so moving the GOT slot
    // alone doesn't help). Crash 5.02.69: AbsoluteFix.lua wrote PC x86 code over JPatch's hook in CantSprintOn.
    bool Restore(int i)
    {
        if(!s.haveGood[i]) return false;
        const long page = sysconf(_SC_PAGESIZE);
        uintptr_t a = (uintptr_t)s.code[i], pg = a & ~(uintptr_t)(page - 1);
        size_t span = (a + s.len[i] - pg + page - 1) & ~(size_t)(page - 1);
        if(mprotect((void*)pg, span, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return false;
        memcpy(s.code[i], s.good[i], s.len[i]);
        __builtin___clear_cache((char*)s.code[i], (char*)s.code[i] + s.len[i]);
        mprotect((void*)pg, span, PROT_READ | PROT_EXEC);
        return !memcmp(s.code[i], s.good[i], s.len[i]);
    }

    void Short(const char* mangled, char* out, size_t n)   // _ZN14SurfaceInfos_c12CantSprintOnEj -> CantSprintOn
    {
        const char* p = mangled + strlen("_ZN14SurfaceInfos_c");
        int l = 0; while(*p >= '0' && *p <= '9') l = l * 10 + (*p++ - '0');
        snprintf(out, n, "%.*s", l, p);
    }
}

void SurfaceGuard::Init(void* h)
{
    s.handle = h;
    Tab<N>::Fill(g_safe);
    int found = 0;
    for(int i = 0; i < N; ++i)
    {
        const char* hx = kGetters[i].hex;
        s.len[i] = (uint8_t)(strlen(hx) / 2);
        for(int k = 0; k < s.len[i]; ++k) s.ref[i][k] = (uint8_t)(Hex(hx[2 * k]) << 4 | Hex(hx[2 * k + 1]));
        uintptr_t a = (uintptr_t)aml->GetSym(h, kGetters[i].name);
        if(a) { s.code[i] = (uint8_t*)(a & ~(uintptr_t)1); ++found; }
    }
    Dl_info di; memset(&di, 0, sizeof(di));
    if(!found || !s.code[0] || !dladdr(s.code[0], &di) || !di.dli_fbase)
    {
        logger->Info("SurfaceGuard: off (SurfaceInfos_c getters not found)");
        return;
    }
    s.base = (uintptr_t)di.dli_fbase;
    dl_iterate_phdr(FindCb, nullptr);
    s.ready = true;
    Check("MP load");
}

void SurfaceGuard::Check(const char* when)
{
    if(!s.ready) return;
    ++s.checks;
    int cnt[4] = {0, 0, 0, 0}, present = 0;
    char st[N];
    uintptr_t tg[N];
    for(int i = 0; i < N; ++i)
    {
        st[i] = 0;
        if(!s.code[i]) continue;
        ++present;
        st[i] = Classify(i, &tg[i]);
        if(st[i] == 'o' || st[i] == 'h') { memcpy(s.good[i], s.code[i], s.len[i]); s.haveGood[i] = true; }
        cnt[st[i] == 'o' ? 0 : st[i] == 'h' ? 1 : st[i] == 'r' ? 2 : 3]++;
    }
    // the table is the 2.00 code only if most getters are byte-for-byte the same
    const bool trusted = present >= 20 && cnt[0] * 10 >= present * 6;
    for(int i = 0; i < N; ++i)
    {
        if(!st[i] || st[i] == s.status[i]) continue;
        char nm[40], who[160]; Short(kGetters[i].name, nm, sizeof(nm));
        const uint8_t* c = s.code[i];
        if(st[i] == 'h') logger->Info("SurfaceGuard: %s is hooked by %s (left alone)", nm, Owner(tg[i], who, sizeof(who)));
        else if(st[i] == 'r') logger->Info("SurfaceGuard: %s is patched to return %d by a mod (left alone)", nm, c[0]);
        else if(st[i] == 'd')
        {
            // a pointer into a loaded library inside the damaged bytes = most likely the mod whose patch spilled here
            char spill[160] = "-";
            for(int k = 0; k + 4 <= s.len[i]; k += 2)
            {
                uintptr_t v; memcpy(&v, c + k, 4);
                Dl_info d2; memset(&d2, 0, sizeof(d2));
                if(v > 0x10000 && dladdr((void*)v, &d2) && d2.dli_fname) { Owner(v, spill, sizeof(spill)); break; }
            }
            logger->Error("SurfaceGuard: SurfaceInfos_c::%s at libGTASA+0x%X is damaged (%02X %02X %02X %02X %02X %02X %02X %02X, "
                          "pointer inside: %s) - this crashed the game (fault address 0x6C)", nm, (unsigned)((uintptr_t)c - s.base),
                          c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], spill);
            if(s.haveGood[i] && Restore(i))
            {
                ++s.restored;
                logger->Info("SurfaceGuard: %s repaired - its last good bytes (%s) are back", nm, s.status[i] == 'h' ? "the other mod's hook" : "the game's code");
                st[i] = s.status[i] ? s.status[i] : 'o';
                cnt[3]--;
                continue;
            }
            if(!trusted) logger->Info("SurfaceGuard: not repaired - only %d/%d getters match the 2.00 code, so the table is not trusted on this game", cnt[0], present);
            else if(!s.moved[i])
            {
                s.moved[i] = MoveGot(kGetters[i].name, g_safe[i]);
                if(s.moved[i]) ++s.movedCount;
                logger->Info("SurfaceGuard: %s -> %s", nm, s.moved[i] ? "the game's calls now go to MP's copy (same result, no crash)" : "could not move the game's GOT slot");
            }
        }
        else if(s.status[i] == 'd') logger->Info("SurfaceGuard: %s is the game's code again", nm);
        s.status[i] = st[i];
    }
    if(s.checks == 1 || cnt[3])
        logger->Info("SurfaceGuard: %s check - %d getters: %d original, %d hooked, %d patched, %d damaged (%d moved to MP's copy)%s",
                     when, present, cnt[0], cnt[1], cnt[2], cnt[3], s.movedCount, trusted ? "" : ", table not trusted on this game");
}

void SurfaceGuard::Tick()
{
    static unsigned n = 0;
    ++n;
    if(n == 1) Check("first game frame");
    else if(n % 30 == 0) Check("periodic");   // 5.02.70: twice a second (was every 600 frames) - 42 x 18-byte compares
}
#else
void SurfaceGuard::Init(void*) { logger->Info("SurfaceGuard: off (64-bit build - the table is the 32-bit code)"); }
void SurfaceGuard::Check(const char*) {}
void SurfaceGuard::Tick() {}
#endif
