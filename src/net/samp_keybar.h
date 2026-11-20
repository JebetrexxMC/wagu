// 5.02.15: the crossplay key bar's key logic (SA-MP Mobile style << ESC TAB ALT SPC F H 2 Y N G), shared by the phone
// UI (mp_sampsrv.inc) and test/sampgate_test.cpp, so the test presses the bar exactly like a finger does.
#pragma once
#include <stdint.h>

enum : int { SKB_ESC = 0, SKB_TAB, SKB_ALT, SKB_SPC, SKB_F, SKB_H, SKB_2, SKB_Y, SKB_N, SKB_G, SKB_COUNT };

// SA-MP key bits (a_samp / omp_player.inc): 0 = an action (ESC), -1 = a client action (G = enter as passenger)
static const struct { const char* label; int bit; } kSampKeyBar[SKB_COUNT] = {
    { "ESC", 0 },     { "TAB", 1 },     { "ALT", 1024 },  { "SPC", 8 },      { "F", 16 },
    { "H", 262144 },  { "2", 512 },     { "Y", 65536 },   { "N", 131072 },   { "G", -1 } };

struct SampKeyBarState
{
    uint64_t latch[SKB_COUNT] = {};   // a tap stays down this long (one slot per button: Y and N used to share one)
    uint64_t resendAt = 0;
    int sent = 0;                     // what the server was last told
};

static const int kSkbTapMs = 300, kSkbResendMs = 250;   // the gate forgets held keys after 400 ms without news

// held / tapped: one bit per button (1 << SKB_x). Returns the SA-MP key bits now down; send = true when the phone must
// send SPO_KEYS (a change, or a key still down and the resend is due).
static inline int SampKeyBarStep(SampKeyBarState& s, uint64_t now, uint32_t held, uint32_t tapped, bool& send)
{
    int keys = 0;
    for(int i = 0; i < SKB_COUNT; ++i)
    {
        const int bit = kSampKeyBar[i].bit; if(bit <= 0) continue;
        const bool h = (held >> i) & 1, t = (tapped >> i) & 1;
        if(h || t) s.latch[i] = now + kSkbTapMs;
        if(h || now < s.latch[i]) keys |= bit;
    }
    send = keys != s.sent || (keys && now >= s.resendAt);
    if(send) { s.sent = keys; s.resendAt = now + kSkbResendMs; }
    return keys;
}
