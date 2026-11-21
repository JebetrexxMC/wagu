// 5.02.29: SampVoice voice chat for the phone on a PC server (included by sampgate.cpp, remote role).
//
// SampVoice (CyberMor/sampvoice v3.1 - the version Life of Player and most RP servers run; their client is
// sampvoice.asi + sv_037r*.dll) works like this on PC:
//   - ClientJoin carries EF BE AD DE + version 11 + micro (the gate already sends it, 5.02.11)
//   - the server answers with RakNet packet 222 "serverInfo" {key, UDP port}: the client opens a UDP socket to
//     server-IP:port and sends a keepAlive every 2 s; once the server sees it, "pluginInit" {bitrate, mute} comes
//   - talking: the client sends "pressKey" (222) for the key the server gave it (SvAddKey), the script attaches it as
//     a speaker to a stream (OnPlayerActivationKeyPress), voice goes out as UDP VoicePackets (Opus 48 kHz mono, one
//     100 ms frame each), then "releaseKey" and the packet numbers start at 0 again
//   - other players' voice comes back as VoicePackets {stream, sender}; the stream tells how to play it: global
//     (radio / phone) or local (around a point / player / car / object, up to its distance)
// The phone's MP voice is 20 ms Opus frames (PKT_VOICE). The gate turns 5 phone frames into one SampVoice frame and
// each SampVoice frame into 5 phone frames (paced 20 ms apart). The volume of local streams is worked out here from
// the stream's spot and the phone's spot (VOICE_GAIN), so a far radio / a global stream plays like it does on PC.
//
// MP_GATE_SV: libMP (Android.mk) has Opus; builds without it (mp_server) keep only the handshake.

#ifdef MP_GATE_SV
#include "opus.h"

namespace
{
    const int      kSvRate = 48000;
    const int      kSvFrame = 4800;            // SampVoice: 100 ms
    const int      kMpFrame = 960;             // MP: 20 ms
    const uint8_t  kSvPacketId = 222;
    const int      kSvHdr = 24;                // VoicePacket header (packed)
    const int      kSvMaxPacket = 1400;
    enum { SVC_SERVERINFO = 0, SVC_PLUGININIT, SVC_MUTEON, SVC_MUTEOFF, SVC_STARTREC, SVC_STOPREC, SVC_ADDKEY, SVC_REMOVEKEY,
           SVC_REMOVEALLKEYS, SVC_GSTREAM, SVC_LPSTREAM, SVC_LSTREAM_VEH, SVC_LSTREAM_PLAYER, SVC_LSTREAM_OBJECT,
           SVC_LSTREAM_DIST, SVC_LPSTREAM_POS, SVC_DELSTREAM, SVC_PRESSKEY, SVC_RELEASEKEY };

    uint32_t SvCrc32c(const uint8_t* b, int n)
    {
        uint32_t c = ~0u;
        while(n--) { c ^= *b++; for(int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0x82f63b78u : c >> 1; }
        return ~c;
    }
}

struct SvStream { int type = SVC_GSTREAM; float dist = 0; float pos[3] = { 0, 0, 0 }; uint32_t target = 0xFFFFFFFF; };
struct SvSpeaker
{
    OpusDecoder* dec = nullptr; OpusEncoder* enc = nullptr;
    std::deque<std::vector<uint8_t>> q;        // MP frames waiting to go to the phone
    uint64_t firstAt = 0, lastAt = 0, nextAt = 0;
    uint32_t expPack = 0; bool any = false, playing = false;
    uint16_t seq = 0; uint8_t gainCode = 32 << 2;
    int slot = -1, madeSlot = -1;              // the ghost slot the voice plays on (madeSlot: made for the voice only)
};
struct SvState
{
    int sock = -1; uint32_t key = 0; uint16_t port = 0; sockaddr_in to{};
    bool info = false, ready = false, mute = false, forced = false;   // info: serverInfo, ready: pluginInit
    uint32_t bitrate = 24000;
    std::vector<uint8_t> keys;
    std::map<uint32_t, SvStream> streams;
    uint64_t kaAt = 0, infoAt = 0;
    // phone -> server
    OpusDecoder* upDec = nullptr; OpusEncoder* upEnc = nullptr;
    int16_t pcm[kSvFrame]; int pcmN = 0;
    bool talking = false, keyDown = false, haveSeq = false; uint16_t upSeq = 0; uint8_t keyHeld = 0;
    uint32_t packid = 0; uint64_t lastUpAt = 0;
    bool saidNoKey = false, saidMute = false, saidReady = false;
    uint32_t rxN = 0, txN = 0, rxBad = 0;
    std::map<uint16_t, SvSpeaker> spk;        // SA-MP id of the speaker
};

static void SvCloseSock(SvState& s)
{
    if(s.sock < 0) return;
#ifdef _WIN32
    closesocket(s.sock);
#else
    close(s.sock);
#endif
    s.sock = -1;
}

static void SvSpeakerFree(Impl& I, MPServer* srv, uint16_t id, SvSpeaker& k)
{
    if(k.dec) opus_decoder_destroy(k.dec);
    if(k.enc) opus_encoder_destroy(k.enc);
    k.dec = nullptr; k.enc = nullptr;
    if(k.madeSlot >= 0)
    {   // a ghost made only to play his voice (he is not streamed in): it goes when he is quiet
        Impl::Rp* p = RemPlayer(I, id);
        if(p && p->slot == k.madeSlot && !p->hasPos) { srv->BridgeRemove(p->slot); p->slot = -1; }
        k.madeSlot = -1;
    }
}

static void SvFree(Impl& I, MPServer* srv)
{   // a new / ended connection: SampVoice starts again with the next serverInfo
    if(!I.sv) return;
    SvState& s = *I.sv;
    if(s.rxN || s.txN) printf("[crossplay] SampVoice: %u voice packets in, %u out (%u bad)\n", s.rxN, s.txN, s.rxBad);
    SvCloseSock(s);
    for(auto& kv : s.spk) SvSpeakerFree(I, srv, kv.first, kv.second);
    if(s.upDec) opus_decoder_destroy(s.upDec);
    if(s.upEnc) opus_encoder_destroy(s.upEnc);
    delete I.sv; I.sv = nullptr;
}

static void SvControlSend(Impl& I, uint16_t type, const void* data, uint16_t len)
{
    if(!I.C) return;
    BitStream o; o.Write(kSvPacketId); o.Write(type); o.Write(len);
    if(len) o.Write((const char*)data, len);
    I.C->Send(&o, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
}

static void SvUdpSend(SvState& s, uint32_t type, const uint8_t* data, int len)
{
    if(s.sock < 0 || len < 0 || kSvHdr + len > kSvMaxPacket) return;
    uint8_t b[kSvMaxPacket];
    const uint32_t zero = 0; const uint16_t z16 = 0, l16 = (uint16_t)len;
    const uint32_t pid = type ? s.packid : 0;
    memcpy(b + 4, &s.key, 4); memcpy(b + 8, &type, 4); memcpy(b + 12, &zero, 4);
    memcpy(b + 16, &z16, 2); memcpy(b + 18, &l16, 2); memcpy(b + 20, &pid, 4);
    const uint32_t h = SvCrc32c(b + 4, kSvHdr - 4); memcpy(b, &h, 4);
    if(len) memcpy(b + kSvHdr, data, (size_t)len);
    sendto(s.sock, (const char*)b, kSvHdr + len, 0, (const sockaddr*)&s.to, sizeof(s.to));
    if(type) { ++s.packid; ++s.txN; }
}

static void SvSay(Impl& I, MPServer* srv, const char* t)
{
    if(I.phoneSlot >= 0) srv->GateMsg(I.phoneSlot, 0xA9C4E4FF, t);
    printf("%s\n", t);
}

// RakNet packet 222 from the PC server
static void SvControl(Impl& I, MPServer* srv, const uint8_t* d, int n)
{
    if(n < 5) return;
    uint16_t type, len; memcpy(&type, d + 1, 2); memcpy(&len, d + 3, 2);
    if(5 + (int)len != n) return;
    const uint8_t* p = d + 5;
    if(!I.sv) I.sv = new SvState();
    SvState& s = *I.sv;
    auto u32 = [&](int at) { uint32_t v = 0; if(at + 4 <= (int)len) memcpy(&v, p + at, 4); return v; };
    auto f32 = [&](int at) { float v = 0; if(at + 4 <= (int)len) memcpy(&v, p + at, 4); return v; };
    switch(type)
    {
        case SVC_SERVERINFO:
        {
            if(len != 6) return;
            uint16_t port; memcpy(&port, p + 4, 2);
            uint32_t ip = 0;
            if(!Resolve(I.rHost.c_str(), &ip)) return;
            SvCloseSock(s);
            s.sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
            if(s.sock < 0) return;
#ifdef _WIN32
            { u_long nb = 1; ioctlsocket(s.sock, FIONBIO, &nb); }
#else
            fcntl(s.sock, F_SETFL, fcntl(s.sock, F_GETFL, 0) | O_NONBLOCK);
#endif
            memset(&s.to, 0, sizeof(s.to)); s.to.sin_family = AF_INET; s.to.sin_port = htons(port); s.to.sin_addr.s_addr = ip;
            s.key = u32(0); s.port = port; s.info = true; s.ready = false; s.packid = 0; s.infoAt = NowMs();
            SvUdpSend(s, 0, nullptr, 0); s.kaAt = NowMs();   // keepAlive: the server learns our UDP address
            printf("[crossplay] SampVoice server on UDP port %u (key %08x)\n", port, s.key);
            break;
        }
        case SVC_PLUGININIT:
        {
            if(len != 5) return;
            s.bitrate = u32(0); s.mute = p[4] != 0; s.ready = true;
            if(s.bitrate < 6000 || s.bitrate > 256000) s.bitrate = 24000;
            if(s.upEnc) opus_encoder_ctl(s.upEnc, OPUS_SET_BITRATE((opus_int32)s.bitrate));
            printf("[crossplay] SampVoice connected (bitrate %u, mute %d)\n", s.bitrate, s.mute ? 1 : 0);
            if(!s.saidReady)
            {
                s.saidReady = true;
                SvSay(I, srv, "[open.mp] This server has SampVoice voice chat: you hear the PC players, and with your MP mic on (voice button) they hear you. The talk key is pressed for you while you speak.");
            }
            break;
        }
        case SVC_MUTEON: s.mute = true; break;
        case SVC_MUTEOFF: s.mute = false; s.saidMute = false; break;
        case SVC_STARTREC: s.forced = true; break;
        case SVC_STOPREC: s.forced = false; break;
        case SVC_ADDKEY: if(len >= 1 && std::find(s.keys.begin(), s.keys.end(), p[0]) == s.keys.end()) s.keys.push_back(p[0]); s.saidNoKey = false; break;
        case SVC_REMOVEKEY: if(len >= 1) s.keys.erase(std::remove(s.keys.begin(), s.keys.end(), p[0]), s.keys.end()); break;
        case SVC_REMOVEALLKEYS: s.keys.clear(); break;
        case SVC_GSTREAM: if(len >= 8) { SvStream st; st.type = SVC_GSTREAM; s.streams[u32(0)] = st; } break;
        case SVC_LPSTREAM: if(len >= 24) { SvStream st; st.type = SVC_LPSTREAM; st.dist = f32(4); st.pos[0] = f32(8); st.pos[1] = f32(12); st.pos[2] = f32(16); s.streams[u32(0)] = st; } break;
        case SVC_LSTREAM_VEH: case SVC_LSTREAM_PLAYER: case SVC_LSTREAM_OBJECT:
            if(len >= 16) { SvStream st; st.type = type; st.dist = f32(4); st.target = u32(8); s.streams[u32(0)] = st; }
            break;
        case SVC_LSTREAM_DIST: if(len >= 8) { auto it = s.streams.find(u32(0)); if(it != s.streams.end()) it->second.dist = f32(4); } break;
        case SVC_LPSTREAM_POS: if(len >= 16) { auto it = s.streams.find(u32(0)); if(it != s.streams.end()) { it->second.pos[0] = f32(4); it->second.pos[1] = f32(8); it->second.pos[2] = f32(12); } } break;
        case SVC_DELSTREAM: if(len >= 4) s.streams.erase(u32(0)); break;
        default: break;   // stream parameters / effects (volume slides, echo...): PC-only sound effects
    }
}

static bool SvRpPos(const Impl::Rp& p, float* out)
{
    if(!p.hasPos) return false;
    memcpy(out, (p.last.flags & SYNC_IN_CAR) ? (const void*)p.last.carPos : (const void*)p.last.pos, sizeof(float) * 3);
    return true;
}

// volume of a stream for the phone: 32 = 100 % (VOICE_GAIN level, 6 bits). Global = full, local = fades out to its distance
static int SvGainLevel(Impl& I, MPServer* srv, const SvStream* st, uint16_t sender)
{
    if(!st || st->type == SVC_GSTREAM) return 32;
    float me[3], h; int hp;
    if(!srv->GatePos(I.phoneSlot, me, &h, &hp)) return 28;
    float at[3]; bool have = false;
    switch(st->type)
    {
        case SVC_LPSTREAM: memcpy(at, st->pos, sizeof(at)); have = true; break;
        case SVC_LSTREAM_PLAYER:
            if((int)st->target == I.myId) return 32;
            if(Impl::Rp* p = RemPlayer(I, (int)st->target)) have = SvRpPos(*p, at);
            break;
        case SVC_LSTREAM_VEH:
            if((int)st->target == I.myVeh) return 32;
            { auto it = I.vehPos.find((int)st->target); if(it != I.vehPos.end()) { memcpy(at, it->second.data(), sizeof(at)); have = true; } }
            break;
        case SVC_LSTREAM_OBJECT:
            { auto it = I.objs.find((int)st->target); if(it != I.objs.end()) { memcpy(at, it->second.p, sizeof(at)); have = true; } }
            break;
    }
    if(!have) { if(Impl::Rp* p = RemPlayer(I, sender)) have = SvRpPos(*p, at); }
    if(!have) return 24;
    const float dx = at[0] - me[0], dy = at[1] - me[1], dz = at[2] - me[2];
    const float d = sqrtf(dx * dx + dy * dy + dz * dz), r = st->dist > 1.0f ? st->dist : 40.0f;
    if(d >= r) return 0;
    // full up close, then softer like SampVoice's 3D sound (about half way it is at 40 %)
    const float t = d / r; float g = (1.0f - t) * (1.0f - t) * 1.1f + (1.0f - t) * 0.1f; if(t < 0.1f) g = 1.0f;
    int lv = (int)(g * 32.0f + 0.5f);
    return lv < 1 ? 1 : (lv > 32 ? 32 : lv);
}

// a VoicePacket from the PC server -> 5 MP frames queued for the phone
static void SvVoiceIn(Impl& I, MPServer* srv, const uint8_t* b, int n, uint64_t now)
{
    SvState& s = *I.sv;
    if(n < kSvHdr) { ++s.rxBad; return; }
    uint32_t h, type, stream, packid; uint16_t sender, len;
    memcpy(&h, b, 4); memcpy(&type, b + 8, 4); memcpy(&stream, b + 12, 4); memcpy(&sender, b + 16, 2); memcpy(&len, b + 18, 2); memcpy(&packid, b + 20, 4);
    if(h != SvCrc32c(b + 4, kSvHdr - 4) || kSvHdr + (int)len != n) { ++s.rxBad; return; }
    if(type != 1 || !len || I.phoneSlot < 0) return;
    ++s.rxN;
    Impl::Rp* p = RemPlayer(I, sender); if(!p) return;
    SvSpeaker& k = s.spk[sender];
    if(!k.dec)
    {
        int err = 0; k.dec = opus_decoder_create(kSvRate, 1, &err);
        k.enc = opus_encoder_create(kSvRate, 1, OPUS_APPLICATION_VOIP, &err);
        if(!k.dec || !k.enc) { SvSpeakerFree(I, srv, sender, k); s.spk.erase(sender); return; }
        opus_encoder_ctl(k.enc, OPUS_SET_BITRATE(32000)); opus_encoder_ctl(k.enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(k.enc, OPUS_SET_COMPLEXITY(5)); opus_encoder_ctl(k.enc, OPUS_SET_INBAND_FEC(1)); opus_encoder_ctl(k.enc, OPUS_SET_PACKET_LOSS_PERC(10));
    }
    if(p->slot < 0)
    {   // not streamed in (far away / a global stream like a radio or a phone call): a ghost so the phone can play him
        p->hasPos = false; RemGhostIn(I, srv, sender, nullptr, 0);
        if(p->slot < 0) return;
        k.madeSlot = p->slot;
    }
    k.slot = p->slot;
    static int16_t pcm[5760 * 3];
    int got = 0;
    if(!k.any || packid == 0 || packid < k.expPack || packid > k.expPack + 20)
    {   // a new talk spurt
        if(k.any && packid != k.expPack) opus_decoder_ctl(k.dec, OPUS_RESET_STATE);
        k.any = true;
    }
    else if(packid > k.expPack)
    {   // lost 1-2 packets: conceal (Opus FEC from this one for the last)
        const uint32_t miss = std::min<uint32_t>(packid - k.expPack, 2);
        for(uint32_t m = 0; m < miss; ++m)
        {
            int r = (m + 1 == miss) ? opus_decode(k.dec, b + kSvHdr, len, pcm + got, kSvFrame, 1) : opus_decode(k.dec, nullptr, 0, pcm + got, kSvFrame, 0);
            if(r > 0) got += r;
        }
    }
    k.expPack = packid + 1;
    int r = opus_decode(k.dec, b + kSvHdr, len, pcm + got, 5760, 0);
    if(r > 0) got += r;
    if(got <= 0) return;
    const SvStream* st = nullptr; { auto it = s.streams.find(stream); if(it != s.streams.end()) st = &it->second; }
    const int lv = SvGainLevel(I, srv, st, sender);
    k.gainCode = (uint8_t)((lv & 63) << 2);
    for(int at = 0; at + kMpFrame <= got; at += kMpFrame)
    {
        uint8_t out[MP_VOICE_MAX_BYTES];
        const int nb = opus_encode(k.enc, pcm + at, kMpFrame, out, MP_VOICE_MAX_BYTES);
        if(nb <= 0) continue;
        k.q.emplace_back(out, out + nb);
    }
    while(k.q.size() > 25) k.q.pop_front();   // never more than 500 ms behind
    if(!k.playing && k.q.size() && !k.firstAt) k.firstAt = now;
    k.lastAt = now;
}

static void SvSendEnd(Impl& I, MPServer* srv, SvSpeaker& k)
{   // a silent frame flagged VOICE_END: the speaking icon goes away now
    static int16_t z[kMpFrame];
    uint8_t out[MP_VOICE_MAX_BYTES];
    const int nb = k.enc ? opus_encode(k.enc, z, kMpFrame, out, MP_VOICE_MAX_BYTES) : 0;
    if(nb > 0 && k.slot >= 0) srv->GateVoice(k.slot, I.phoneSlot, k.seq++, (uint8_t)(VOICE_END | VOICE_GAIN | k.gainCode), out, nb);
}

// phone PKT_VOICE (20 ms Opus) -> SampVoice
static void SvEncodeSend(SvState& s)
{
    uint8_t out[kSvMaxPacket - kSvHdr];
    const int nb = opus_encode(s.upEnc, s.pcm, kSvFrame, out, (opus_int32)sizeof(out));
    s.pcmN = 0;
    if(nb > 0) SvUdpSend(s, 1, out, nb);
}

static void SvEndSpurt(Impl& I)
{
    SvState& s = *I.sv;
    if(!s.talking) return;
    if(s.pcmN > 0) { memset(s.pcm + s.pcmN, 0, sizeof(int16_t) * (size_t)(kSvFrame - s.pcmN)); s.pcmN = kSvFrame; SvEncodeSend(s); }
    if(s.keyDown) { SvControlSend(I, SVC_RELEASEKEY, &s.keyHeld, 1); s.keyDown = false; }
    s.packid = 0; s.talking = false; s.haveSeq = false;
}

static void SvPhoneVoice(Impl& I, MPServer* srv, const uint8_t* d, int n, uint64_t now)
{
    if(!I.sv || !I.sv->ready || I.sv->sock < 0 || n < (int)sizeof(PktVoiceHdr)) return;
    SvState& s = *I.sv;
    PktVoiceHdr vh; memcpy(&vh, d, sizeof(vh));
    if(!vh.len || (int)sizeof(vh) + vh.len > n) return;
    if(s.mute)
    {
        if(!s.saidMute) { s.saidMute = true; SvSay(I, srv, "[open.mp] SampVoice: the server muted your voice."); }
        SvEndSpurt(I); return;
    }
    if(s.keys.empty() && !s.forced)
    {   // like the PC plugin: no talk key from the server (not logged in yet / no voice for you) = nothing is sent
        if(!s.saidNoKey && !(vh.flags & VOICE_END)) { s.saidNoKey = true; printf("[crossplay] SampVoice: no talk key from the server yet - voice not sent\n"); }
        return;
    }
    if(!s.upDec)
    {
        int err = 0; s.upDec = opus_decoder_create(kSvRate, 1, &err);
        s.upEnc = opus_encoder_create(kSvRate, 1, OPUS_APPLICATION_VOIP, &err);
        if(!s.upDec || !s.upEnc) return;
        opus_encoder_ctl(s.upEnc, OPUS_SET_BITRATE((opus_int32)s.bitrate)); opus_encoder_ctl(s.upEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(s.upEnc, OPUS_SET_COMPLEXITY(8)); opus_encoder_ctl(s.upEnc, OPUS_SET_INBAND_FEC(1)); opus_encoder_ctl(s.upEnc, OPUS_SET_DTX(0));
    }
    if(!s.talking)
    {
        if(vh.flags & VOICE_END) return;   // the end of a spurt we never started
        s.talking = true; s.pcmN = 0; s.packid = 0; s.haveSeq = false;
        opus_decoder_ctl(s.upDec, OPUS_RESET_STATE);
        if(!s.forced && !s.keys.empty()) { s.keyHeld = s.keys[0]; SvControlSend(I, SVC_PRESSKEY, &s.keyHeld, 1); s.keyDown = true; }
    }
    s.lastUpAt = now;
    int16_t pcm[kMpFrame * 4];
    int got = 0;
    if(s.haveSeq)
    {
        const int16_t gap = (int16_t)(vh.seq - s.upSeq - 1);
        if(gap < 0) return;   // late / repeated
        for(int m = 0; m < gap && m < 3; ++m) { int r = opus_decode(s.upDec, nullptr, 0, pcm + got, kMpFrame, 0); if(r > 0) got += r; }
    }
    s.upSeq = vh.seq; s.haveSeq = true;
    { int r = opus_decode(s.upDec, d + sizeof(vh), vh.len, pcm + got, kMpFrame, 0); if(r > 0) got += r; }
    for(int i = 0; i < got; ++i)
    {
        s.pcm[s.pcmN++] = pcm[i];
        if(s.pcmN == kSvFrame) SvEncodeSend(s);
    }
    if(vh.flags & VOICE_END) SvEndSpurt(I);
}

static void SvTick(Impl& I, MPServer* srv, uint64_t now)
{
    if(!I.sv) return;
    SvState& s = *I.sv;
    if(s.sock < 0) return;
    if(now - s.kaAt >= 2000) { SvUdpSend(s, 0, nullptr, 0); s.kaAt = now; }
    uint8_t b[2048];
    for(int k = 0; k < 64; ++k)
    {
        sockaddr_in from; socklen_t fl = sizeof(from);
        const int r = (int)recvfrom(s.sock, (char*)b, sizeof(b), 0, (sockaddr*)&from, &fl);
        if(r <= 0) break;
        if(from.sin_addr.s_addr != s.to.sin_addr.s_addr) continue;
        SvVoiceIn(I, srv, b, r, now);
    }
    if(s.talking && now - s.lastUpAt > 600) SvEndSpurt(I);   // the phone's end frame got lost
    for(auto it = s.spk.begin(); it != s.spk.end();)
    {
        SvSpeaker& k = it->second;
        if(!k.playing && !k.q.empty() && (k.q.size() >= 8 || now - k.firstAt >= 60)) { k.playing = true; k.nextAt = now; }   // ~60 ms in hand for the network
        if(k.playing)
        {
            if(k.nextAt + 100 < now) k.nextAt = now;
            while(!k.q.empty() && now >= k.nextAt)
            {
                const std::vector<uint8_t>& f = k.q.front();
                if(k.slot >= 0) srv->GateVoice(k.slot, I.phoneSlot, k.seq++, (uint8_t)(VOICE_GAIN | k.gainCode), f.data(), (int)f.size());
                k.q.pop_front(); k.nextAt += 20;
            }
            if(k.q.empty() && now - k.lastAt > 250) { SvSendEnd(I, srv, k); k.playing = false; k.firstAt = 0; k.any = false; }
        }
        if(!k.playing && k.q.empty() && now - k.lastAt > 8000) { SvSpeakerFree(I, srv, it->first, k); it = s.spk.erase(it); continue; }
        ++it;
    }
}
#else
struct SvState {};
static void SvFree(Impl& I, MPServer*) { I.sv = nullptr; }
static void SvControl(Impl&, MPServer*, const uint8_t*, int) {}
static void SvPhoneVoice(Impl&, MPServer*, const uint8_t*, int, uint64_t) {}
static void SvTick(Impl&, MPServer*, uint64_t) {}
#endif
