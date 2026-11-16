// 4.13.64: native crash log for the overlay (libviceloader.so). On SIGSEGV / SIGABRT / SIGBUS / SIGILL / SIGFPE /
// SIGTRAP it writes files/crash_native.txt (signal, fault address, pc / lr, backtrace with library + symbol), then
// hands the signal to the previous handler so Android still makes its normal tombstone. CrashLog.java copies the
// file to Download/ViceLoader/crash_<time>.txt on the next start.
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>
#include <unwind.h>
#include <ucontext.h>
#include <sys/system_properties.h>
#include <string>

static char s_Path[512];
static struct sigaction s_Old[32];
static const int kSigs[] = { SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE, SIGTRAP };
static volatile sig_atomic_t s_In = 0;

static void W(int fd, const char* s) { if(s) { ssize_t r = write(fd, s, strlen(s)); (void)r; } }
static void Hex(int fd, uintptr_t v) { char b[2 + sizeof(v) * 2 + 1]; snprintf(b, sizeof(b), "0x%zx", (size_t)v); W(fd, b); }

struct BtState { uintptr_t* pcs; int n, max; };
static _Unwind_Reason_Code BtCb(_Unwind_Context* ctx, void* arg)
{
    BtState* s = (BtState*)arg;
    uintptr_t pc = _Unwind_GetIP(ctx);
    if(pc) { if(s->n >= s->max) return _URC_END_OF_STACK; s->pcs[s->n++] = pc; }
    return _URC_NO_REASON;
}

static void Frame(int fd, int i, uintptr_t pc)
{
    char b[24]; snprintf(b, sizeof(b), "  #%02d pc ", i); W(fd, b);
    Dl_info di{};
    if(dladdr((void*)pc, &di) && di.dli_fname)
    {
        Hex(fd, pc - (uintptr_t)di.dli_fbase); W(fd, "  "); W(fd, di.dli_fname);
        if(di.dli_sname) { W(fd, " ("); W(fd, di.dli_sname); W(fd, "+"); Hex(fd, pc - (uintptr_t)di.dli_saddr); W(fd, ")"); }
    }
    else Hex(fd, pc);
    W(fd, "\n");
}

static void Handler(int sig, siginfo_t* si, void* uc)
{
    if(!s_In)
    {
        s_In = 1;
        int fd = open(s_Path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(fd >= 0)
        {
            char prop[PROP_VALUE_MAX] = "";
            W(fd, "VICE Loader native crash (libviceloader.so)\nsignal: ");
            const char* nm = sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL" : sig == SIGFPE ? "SIGFPE" : "SIGTRAP";
            W(fd, nm); char b[48]; snprintf(b, sizeof(b), " (%d), code %d, tid %d\nfault addr: ", sig, si ? si->si_code : 0, gettid()); W(fd, b);
            Hex(fd, si ? (uintptr_t)si->si_addr : 0); W(fd, "\n");
            __system_property_get("ro.product.model", prop); W(fd, "device: "); W(fd, prop);
            __system_property_get("ro.build.version.release", prop); W(fd, ", Android "); W(fd, prop); W(fd, "\n");
            uintptr_t pc = 0, lr = 0;
#if defined(__aarch64__)
            if(uc) { auto* m = &((ucontext_t*)uc)->uc_mcontext; pc = m->pc; lr = m->regs[30]; }
            W(fd, "abi: arm64-v8a\n");
#elif defined(__arm__)
            if(uc) { auto* m = &((ucontext_t*)uc)->uc_mcontext; pc = m->arm_pc; lr = m->arm_lr; }
            W(fd, "abi: armeabi-v7a\n");
#endif
            W(fd, "\ncrash pc / lr:\n"); Frame(fd, 0, pc); Frame(fd, 1, lr);
            uintptr_t pcs[48]; BtState st{ pcs, 0, 48 };
            _Unwind_Backtrace(BtCb, &st);
            W(fd, "\nbacktrace (handler thread):\n");
            for(int i = 0; i < st.n; i++) Frame(fd, i, pcs[i]);
            close(fd);
        }
    }
    // the previous handler (Android's debuggerd) still writes its tombstone
    struct sigaction* o = (sig > 0 && sig < 32) ? &s_Old[sig] : nullptr;
    if(o && (o->sa_flags & SA_SIGINFO) && o->sa_sigaction) { o->sa_sigaction(sig, si, uc); return; }
    if(o && o->sa_handler && o->sa_handler != SIG_IGN && o->sa_handler != SIG_DFL) { o->sa_handler(sig); return; }
    signal(sig, SIG_DFL); raise(sig);
}

extern "C" void VL_CrashInstall(const std::string& filesDir)
{
    static bool done = false;
    if(done) return;
    done = true;
    snprintf(s_Path, sizeof(s_Path), "%s/crash_native.txt", filesDir.c_str());
    static char alt[64 * 1024];   // a stack overflow still gets a log
    stack_t ss{}; ss.ss_sp = alt; ss.ss_size = sizeof(alt); sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = Handler; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
    for(int s : kSigs) sigaction(s, &sa, &s_Old[s]);
}
