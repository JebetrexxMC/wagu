// Plays MP sound effects (mp3) with OpenSL ES so we don't depend on the game's audio engine.
//
// 3.01 fix (crash in libstagefright_foundation.so, android::MediaBuffer::~MediaBuffer, SIGSEGV,
// Android 16): the mp3s used to be played by OpenSL "URI" players, which decode through Android's
// media framework (stagefright) on its own threads. Restarting a hit voice (STOPPED -> PLAYING) and
// destroying the "connected" player while it was still decoding freed media buffers that decoder
// thread was still using. Now every mp3 is decoded once by MP itself (minimp3, CC0) into PCM, and
// played by buffer-queue players: no media framework, no decoder threads, nothing is destroyed while
// playing. A replay is Clear() + Enqueue() of the same PCM, which is safe at any time.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <chrono>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "../../third_party/minimp3/minimp3_ex.h"
#include "sound.h"
#include "../game/jvmthread.h"   // 5.02.65: audio worker threads run attached to the JVM (binder abort)
#include <fcntl.h>
#include <unistd.h>
#include <SLES/OpenSLES_Android.h>

// 5.02.31: Settings > Sound > Host Media volumes - one volume per sound file (by its name, any folder or URL).
// Every Play* call multiplies its own volume by it, so a sound can be made quieter / louder everywhere at once.
#include <map>
namespace
{
    std::mutex s_VolMx;
    std::map<std::string, float> s_FileVol;   // lower case file name -> 0..1.5
    std::string VolKey(const char* path)
    {
        if(!path) return std::string();
        const char* b = path; for(const char* c = path; *c; ++c) if(*c == '/' || *c == '\\') b = c + 1;
        std::string k; for(const char* c = b; *c && *c != '?' && *c != '#'; ++c) k += (char)tolower((unsigned char)*c);
        return k;
    }
    float VolOf(const char* path)
    {
        std::lock_guard<std::mutex> lk(s_VolMx);
        auto it = s_FileVol.find(VolKey(path));
        return it == s_FileVol.end() ? 1.0f : it->second;
    }
    float s_fStreamMul = 1.0f, s_fMusicMul = 1.0f;
    // 5.02.47: the file volume of what plays now is read live. Before, the multiplier was taken once when the
    // track started, so moving the Solo / Host Media slider while it played changed nothing until a restart.
    std::string s_StreamKey, s_MusicKey;          // VolKey of the stream / music that plays now
    float s_fStreamBase = 1.0f, s_fMusicBase = 1.0f;   // the caller's own volume (fades, distance), before the file volume
    void ReapplyStreamVolume();
    void ReapplyMusicVolume();
}
void  Sound::SetFileVolume(const char* name, float v)
{
    if(v < 0) v = 0; if(v > 1.5f) v = 1.5f;
    std::string k = VolKey(name);
    { std::lock_guard<std::mutex> lk(s_VolMx); s_FileVol[k] = v; }
    if(!k.empty() && k == s_StreamKey) ReapplyStreamVolume();   // 5.02.47: right away, while it plays
    if(!k.empty() && k == s_MusicKey)  ReapplyMusicVolume();
}
float Sound::FileVolume(const char* name) { return VolOf(name); }

// 5.02.47: 0..1.5 -> millibel. Above 100 % uses the player's own headroom (GetMaxVolumeLevel, 0 on most
// phones, so there it stays at 100 %); 0 = muted.
// 5.02.58: the slider is a loudness curve now (cubic, like PulseAudio / most players). 5.02.47 mapped it straight to
// the amplitude (20 dB per decade): 50 % was only -6 dB and 30 % -10 dB, so moving the Solo / Host Media / menu music
// slider down while it played sounded like nothing changed until the very bottom. Now 50 % = -18 dB, 25 % = -36 dB.
// The master volume (Settings > Sound > MP volume, every MP sound) is multiplied in here as well.
static std::atomic<float> s_fMaster{1.0f};
static SLmillibel SlLevel(SLVolumeItf vol, float volume)
{
    volume *= s_fMaster.load();
    if(volume <= 0.01f) return SL_MILLIBEL_MIN;
    SLmillibel mx = 0; if(vol) (*vol)->GetMaxVolumeLevel(vol, &mx);
    float mb = 6000.0f * log10f(volume > 1.5f ? 1.5f : volume);
    if(mb > (float)mx) mb = (float)mx;
    if(mb < (float)SL_MILLIBEL_MIN) mb = (float)SL_MILLIBEL_MIN;
    return (SLmillibel)mb;
}
#define SL_SET_VOL(itf, volume) do { if(itf) (*(itf))->SetVolumeLevel((itf), SlLevel((itf), (volume))); } while(0)

static SLObjectItf g_Engine = nullptr, g_Mix = nullptr;
static SLEngineItf g_EngineItf = nullptr;

static bool EnsureEngine()
{
    if(g_EngineItf) return true;
    if(slCreateEngine(&g_Engine, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS) { g_Engine = nullptr; return false; }
    if((*g_Engine)->Realize(g_Engine, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) return false;
    if((*g_Engine)->GetInterface(g_Engine, SL_IID_ENGINE, &g_EngineItf) != SL_RESULT_SUCCESS) { g_EngineItf = nullptr; return false; }
    if((*g_EngineItf)->CreateOutputMix(g_EngineItf, &g_Mix, 0, nullptr, nullptr) != SL_RESULT_SUCCESS) { g_EngineItf = nullptr; return false; }
    if((*g_Mix)->Realize(g_Mix, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) { g_EngineItf = nullptr; return false; }
    return true;
}

namespace
{
    // ---- decoded sounds (kept for the whole session: the players read straight from them) ----
    struct Pcm { std::string path; std::vector<int16_t> data; int ch = 0, hz = 0; bool ok = false; };
    std::vector<Pcm*> s_Pcm;

    bool DecodeInto(const char* path, Pcm* p);
    const Pcm* Decode(const char* path)
    {
        for(Pcm* p : s_Pcm) if(p->path == path) return p->ok ? p : nullptr;
        Pcm* p = new Pcm(); p->path = path; s_Pcm.push_back(p);
        return DecodeInto(path, p) ? p : nullptr;
    }
    bool DecodeInto(const char* path, Pcm* p)
    {
        FILE* f = fopen(path, "rb");
        if(!f) { logger->Error("Sound: %s not found", path); return false; }
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        if(len <= 0 || len > 32 * 1024 * 1024) { fclose(f); logger->Error("Sound: %s has a bad size", path); return false; }
        std::vector<uint8_t> raw((size_t)len);
        size_t got = fread(raw.data(), 1, raw.size(), f); fclose(f);
        mp3dec_t dec; mp3dec_file_info_t info; memset(&info, 0, sizeof(info));
        if(mp3dec_load_buf(&dec, raw.data(), got, &info, nullptr, nullptr) || !info.buffer || !info.samples || info.channels < 1 || info.channels > 2 || info.hz < 8000)
        {
            if(info.buffer) free(info.buffer);
            logger->Error("Sound: could not decode %s", path);
            return false;
        }
        p->data.assign(info.buffer, info.buffer + info.samples);
        p->ch = info.channels; p->hz = info.hz; p->ok = true;
        free(info.buffer);
        logger->Info("Sound: %s decoded (%d Hz, %d ch, %.1f s)", path, p->hz, p->ch, (float)info.samples / p->ch / p->hz);
        return true;
    }

    // ---- buffer-queue voices -------------------------------------------------------------------
    struct Voice
    {
        SLObjectItf obj = nullptr; SLPlayItf play = nullptr; SLVolumeItf vol = nullptr;
        SLAndroidSimpleBufferQueueItf bq = nullptr; int ch = 0, hz = 0;
    };
    bool BuildVoice(Voice& v, int ch, int hz)
    {
        SLDataLocator_AndroidSimpleBufferQueue loc = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
        SLDataFormat_PCM fmt = { SL_DATAFORMAT_PCM, (SLuint32)ch, (SLuint32)hz * 1000, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                                 ch == 2 ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT) : SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN };
        SLDataSource src = { &loc, &fmt };
        SLDataLocator_OutputMix outMix = { SL_DATALOCATOR_OUTPUTMIX, g_Mix };
        SLDataSink sink = { &outMix, nullptr };
        const SLInterfaceID ids[2] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_VOLUME };
        const SLboolean req[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_FALSE };
        if((*g_EngineItf)->CreateAudioPlayer(g_EngineItf, &v.obj, &src, &sink, 2, ids, req) != SL_RESULT_SUCCESS) { v = Voice(); return false; }
        if((*v.obj)->Realize(v.obj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS
        || (*v.obj)->GetInterface(v.obj, SL_IID_PLAY, &v.play) != SL_RESULT_SUCCESS
        || (*v.obj)->GetInterface(v.obj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &v.bq) != SL_RESULT_SUCCESS)
        { (*v.obj)->Destroy(v.obj); v = Voice(); return false; }
        if((*v.obj)->GetInterface(v.obj, SL_IID_VOLUME, &v.vol) != SL_RESULT_SUCCESS) v.vol = nullptr;
        (*v.play)->SetPlayState(v.play, SL_PLAYSTATE_PLAYING);   // always "playing": an empty queue is silent
        v.ch = ch; v.hz = hz;
        return true;
    }
    bool PlayOn(Voice& v, const Pcm& p, float volume)
    {
        if(!v.obj || v.ch != p.ch || v.hz != p.hz)
        {
            if(v.obj) { (*v.obj)->Destroy(v.obj); v = Voice(); }   // never playing anything from a decoder: safe
            if(!BuildVoice(v, p.ch, p.hz)) return false;
        }
        SL_SET_VOL(v.vol, volume);
        (*v.bq)->Clear(v.bq);
        return (*v.bq)->Enqueue(v.bq, p.data.data(), (SLuint32)(p.data.size() * sizeof(int16_t))) == SL_RESULT_SUCCESS;
    }

    Voice s_Fx;          // "connected" and other one-shot sounds
    Voice s_Hit[3];      // hit sound, round robin (a minigun's hits overlap)
    int   s_nNextHit = 0;
    Voice s_Ui[2];       // 3.29: click.mp3 / click2.mp3 / click3.mp3
    int   s_nNextUi = 0;
}

bool Sound::PlayFile(const char* path, float volume)
{
    if(!path || !*path) return false;
    volume *= VolOf(path);   // 5.02.31
    if(!EnsureEngine()) { logger->Error("OpenSL ES init failed"); return false; }
    const Pcm* p = Decode(path);
    return p && PlayOn(s_Fx, *p, volume);
}

bool Sound::PreloadHit(const char* path)
{
    if(!path || !*path || !EnsureEngine()) return false;
    const Pcm* p = Decode(path);
    if(!p) return false;
    for(Voice& v : s_Hit) if(!v.obj || v.ch != p->ch || v.hz != p->hz) { if(v.obj) { (*v.obj)->Destroy(v.obj); v = Voice(); } BuildVoice(v, p->ch, p->hz); }
    return s_Hit[0].obj != nullptr;
}

bool Sound::PlayHit(const char* path, float volume)
{
    if(!PreloadHit(path)) return false;
    volume *= VolOf(path);   // 5.02.31
    const Pcm* p = Decode(path);
    for(int tries = 0; tries < 3; ++tries)
    {
        Voice& v = s_Hit[s_nNextHit]; s_nNextHit = (s_nNextHit + 1) % 3;
        if(v.obj && PlayOn(v, *p, volume)) return true;
    }
    return false;
}

// ---- 5.00.07: sounds streamed from the media host into memory (notify.mp3) ------------------------
namespace
{
    std::mutex s_MemMx;
    Pcm* s_Mem[4] = {};   // decoded once, never freed (a voice may still read it)
    Voice s_MemV;
}
bool Sound::DecodeMem(int slot, const void* data, size_t size)
{
    if(slot < 0 || slot >= 4 || !data || size < 512) return false;
    { std::lock_guard<std::mutex> lk(s_MemMx); if(s_Mem[slot]) return true; }
    mp3dec_t dec; mp3dec_file_info_t info; memset(&info, 0, sizeof(info));
    if(mp3dec_load_buf(&dec, (const uint8_t*)data, size, &info, nullptr, nullptr) || !info.buffer || !info.samples || info.channels < 1 || info.channels > 2 || info.hz < 8000)
    {
        if(info.buffer) free(info.buffer);
        logger->Error("Sound: could not decode the downloaded sound %d (%u bytes)", slot, (unsigned)size);
        return false;
    }
    Pcm* p = new Pcm(); p->path = "mem"; p->data.assign(info.buffer, info.buffer + info.samples); p->ch = info.channels; p->hz = info.hz; p->ok = true;
    free(info.buffer);
    std::lock_guard<std::mutex> lk(s_MemMx);
    if(s_Mem[slot]) { delete p; return true; }
    s_Mem[slot] = p;
    logger->Info("Sound: downloaded sound %d decoded (%d Hz, %d ch, %.1f s)", slot, p->hz, p->ch, (float)p->data.size() / p->ch / p->hz);
    return true;
}
bool Sound::MemReady(int slot) { std::lock_guard<std::mutex> lk(s_MemMx); return slot >= 0 && slot < 4 && s_Mem[slot]; }
bool Sound::PlayMem(int slot, float volume)
{
    if(slot < 0 || slot >= 4 || !EnsureEngine()) return false;
    const Pcm* p; { std::lock_guard<std::mutex> lk(s_MemMx); p = s_Mem[slot]; }
    if(slot == 0) volume *= VolOf("notify.mp3");   // 5.02.31: slot 0 = notify.mp3
    return p && PlayOn(s_MemV, *p, volume);
}

// ---- 3.04: looping music (Brief Case War: war_ambient.mp3, 45 %) ------------------------------
// The mp3 is decoded on its own thread (a 3-minute song takes a moment on a phone), then played by
// a buffer-queue voice whose "buffer done" callback queues the same PCM again = endless loop.
namespace
{
    struct Music
    {
        Voice v; Pcm* pcm = nullptr; std::string path;
        std::atomic<int> state{0};   // 0 idle, 1 decoding, 2 ready, 3 playing, -1 failed
        std::atomic<bool> loop{true}, want{false};
        float vol = 1.0f;
    };
    Music s_Music;
    void MusicDone(SLAndroidSimpleBufferQueueItf bq, void*)
    {
        if(s_Music.want && s_Music.loop && s_Music.pcm && s_Music.pcm->ok)
            (*bq)->Enqueue(bq, s_Music.pcm->data.data(), (SLuint32)(s_Music.pcm->data.size() * sizeof(int16_t)));
    }
    void SetVoiceVolume(Voice& v, float volume)
    {
        SL_SET_VOL(v.vol, volume);
    }
}

bool Sound::PlayMusic(const char* path, float volume, bool loop)
{
    if(!path || !*path || !EnsureEngine()) return false;
    s_MusicKey = VolKey(path); s_fMusicBase = volume;   // 5.02.47
    s_fMusicMul = VolOf(path); volume *= s_fMusicMul;   // 5.02.31
    s_Music.vol = volume; s_Music.loop = loop; s_Music.want = true;
    if(s_Music.path == path && s_Music.state != -1)
    {
        if(s_Music.state == 3) SetVoiceVolume(s_Music.v, volume);
        return true;   // decoding / ready / already playing
    }
    if(s_Music.state == 1) return false;   // another song still decoding
    if(s_Music.v.obj) { (*s_Music.v.bq)->Clear(s_Music.v.bq); }
    s_Music.path = path;
    FILE* f = fopen(path, "rb");
    if(!f) { s_Music.state = -1; logger->Error("Music: %s not found (put war_ambient.mp3 in files/TYFMC/MP)", path); return false; }
    fclose(f);
    s_Music.state = 1;
    Pcm* old = s_Music.pcm; s_Music.pcm = nullptr;
    std::string p = path;
    std::thread([p, old]() {
        MPJvm::ThreadScope jvm("MP music");
        Pcm* n = new Pcm(); n->path = p;
        bool ok = DecodeInto(p.c_str(), n);
        s_Music.pcm = n;   // the old PCM is kept (a voice may still have read from it)
        (void)old;
        s_Music.state = ok ? 2 : -1;
    }).detach();
    return true;
}

void Sound::MusicUpdate()
{
    if(s_Music.state == 2 && s_Music.want)
    {
        Pcm* p = s_Music.pcm;
        if(!p || !p->ok) { s_Music.state = -1; return; }
        Voice& v = s_Music.v;
        if(!v.obj || v.ch != p->ch || v.hz != p->hz)
        {
            if(v.obj) { (*v.obj)->Destroy(v.obj); v = Voice(); }
            if(!BuildVoice(v, p->ch, p->hz)) { s_Music.state = -1; return; }
            (*v.bq)->RegisterCallback(v.bq, MusicDone, nullptr);
        }
        SetVoiceVolume(v, s_Music.vol);
        (*v.bq)->Clear(v.bq);
        (*v.bq)->Enqueue(v.bq, p->data.data(), (SLuint32)(p->data.size() * sizeof(int16_t)));
        s_Music.state = 3;
    }
}

void Sound::SetMusicVolume(float volume)
{
    s_fMusicBase = volume;
    s_fMusicMul = VolOf(s_MusicKey.c_str()); volume *= s_fMusicMul;   // 5.02.47: live file volume
    s_Music.vol = volume;
    if(s_Music.state == 3) SetVoiceVolume(s_Music.v, volume);
}

void Sound::StopMusic()
{
    s_Music.want = false;
    if(s_Music.v.obj && s_Music.v.bq) (*s_Music.v.bq)->Clear(s_Music.v.bq);
    if(s_Music.state == 3) s_Music.state = 2;   // decoded PCM stays: the next PlayMusic of the same file is instant
}

bool Sound::MusicPlaying() { return s_Music.state == 3 && s_Music.want; }


// ---- 3.63: streamed music (Day Z ambient, ~1 h mp3) --------------------------------------------
namespace
{
    enum { ST_SLOTS = 4 };
    struct Stream
    {
        Voice v; std::mutex mx;
        std::vector<int16_t> slot[ST_SLOTS]; int slotState[ST_SLOTS] = {0};   // 0 free, 1 ready, 2 queued
        int head = 0;                          // next slot to play
        std::atomic<int> gen{0};
        std::atomic<bool> eof{false}, active{false}, failed{false};
        std::atomic<long long> played{0};      // sample frames done
        int ch = 0, hz = 0; float vol = 1.0f, dur = 0.0f, startSec = 0.0f;   // 5.02.06: startSec = where a resumed track starts
    };
    Stream s_St;
    bool StEnqueueNextLocked()
    {   // the slot at head (if ready) goes into the OpenSL queue
        for(int k = 0; k < ST_SLOTS; ++k)
        {
            int i = (s_St.head + k) % ST_SLOTS;
            if(s_St.slotState[i] == 2) continue;
            if(s_St.slotState[i] != 1) return false;
            if(!s_St.v.bq) return false;
            if((*s_St.v.bq)->Enqueue(s_St.v.bq, s_St.slot[i].data(), (SLuint32)(s_St.slot[i].size() * sizeof(int16_t))) != SL_RESULT_SUCCESS) return false;
            s_St.slotState[i] = 2;
            return true;
        }
        return false;
    }
    void StDone(SLAndroidSimpleBufferQueueItf, void*)
    {
        std::lock_guard<std::mutex> lk(s_St.mx);
        int i = s_St.head;
        if(s_St.slotState[i] == 2)
        {
            if(s_St.ch > 0) s_St.played += (long long)s_St.slot[i].size() / s_St.ch;
            s_St.slotState[i] = 0; s_St.head = (i + 1) % ST_SLOTS;
        }
        while(StEnqueueNextLocked()) {}
    }
    void StDecodeThread(std::string path, int myGen, float startSec)
    {
        MPJvm::ThreadScope jvm("MP stream");
        FILE* f = fopen(path.c_str(), "rb");
        if(!f) { s_St.failed = true; s_St.eof = true; return; }
        fseek(f, 0, SEEK_END); long fileLen = ftell(f); fseek(f, 0, SEEK_SET);
        long skip = 0;   // ID3v2 tag (cover art can be MBs)
        unsigned char h[10];
        if(fread(h, 1, 10, f) == 10 && h[0] == 'I' && h[1] == 'D' && h[2] == '3')
            skip = 10 + (((long)(h[6] & 0x7F) << 21) | ((long)(h[7] & 0x7F) << 14) | ((long)(h[8] & 0x7F) << 7) | (long)(h[9] & 0x7F));
        fseek(f, skip, SEEK_SET);
        mp3dec_t dec; mp3dec_init(&dec);   // per thread (an old thread may still be ending)
        std::vector<uint8_t> in(64 * 1024); size_t pos = 0, avail = 0; bool fileEof = false;
        mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        std::vector<int16_t> block; bool first = true; int ch = 0, hz = 0; long long dropped = 0;
        while(s_St.gen == myGen)
        {
            if(avail < 16 * 1024 && !fileEof)
            {
                if(pos) { memmove(in.data(), in.data() + pos, avail); pos = 0; }
                size_t got = fread(in.data() + avail, 1, in.size() - avail, f);
                if(got == 0) fileEof = true; avail += got;
            }
            if(avail == 0) break;
            mp3dec_frame_info_t info; memset(&info, 0, sizeof(info));
            int n = mp3dec_decode_frame(&dec, in.data() + pos, (int)avail, pcm, &info);
            if(info.frame_bytes <= 0) { if(fileEof) break; pos += avail > 1 ? 1 : avail; avail -= avail > 1 ? 1 : avail; continue; }
            pos += info.frame_bytes; avail -= info.frame_bytes;
            if(n <= 0) continue;
            if(first)
            {
                first = false; ch = info.channels; hz = info.hz;
                if(ch < 1 || ch > 2 || hz < 8000) { s_St.failed = true; break; }
                std::lock_guard<std::mutex> lk(s_St.mx);
                if(s_St.gen != myGen) break;
                s_St.ch = ch; s_St.hz = hz;
                s_St.dur = info.bitrate_kbps > 0 ? (float)(fileLen - skip) * 8.0f / (info.bitrate_kbps * 1000.0f) : 0.0f;
            }
            if(info.channels != ch || info.hz != hz) continue;   // odd frame: skip
            if(startSec > 0.0f)
            {   // 5.02.06: resume - frames before startSec are decoded and dropped (a 5 min mp3 takes ~1 s)
                dropped += n;
                if(dropped < (long long)(startSec * hz)) continue;
                startSec = 0.0f;
            }
            block.insert(block.end(), pcm, pcm + n * ch);
            if((int)block.size() >= hz * ch)   // ~1 s per slot
            {
                for(;;)
                {
                    if(s_St.gen != myGen) { fclose(f); return; }
                    {
                        std::lock_guard<std::mutex> lk(s_St.mx);
                        int freeSlot = -1;
                        for(int k = 0; k < ST_SLOTS; ++k) { int i = (s_St.head + k) % ST_SLOTS; if(s_St.slotState[i] == 0) { freeSlot = i; break; } }
                        if(freeSlot >= 0) { s_St.slot[freeSlot].swap(block); s_St.slotState[freeSlot] = 1; block.clear(); break; }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                }
            }
        }
        // the last part
        while(!block.empty() && s_St.gen == myGen)
        {
            {
                std::lock_guard<std::mutex> lk(s_St.mx);
                int freeSlot = -1;
                for(int k = 0; k < ST_SLOTS; ++k) { int i = (s_St.head + k) % ST_SLOTS; if(s_St.slotState[i] == 0) { freeSlot = i; break; } }
                if(freeSlot >= 0) { s_St.slot[freeSlot].swap(block); s_St.slotState[freeSlot] = 1; block.clear(); break; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        fclose(f);
        if(s_St.gen == myGen) { if(first) s_St.failed = true; s_St.eof = true; }
    }
}

// 4.00.1: the Android media decoder (an OpenSL player on a file descriptor) for files minimp3 can't
// read: m4a / aac / ogg / opus / webm saved as ".mp3" (YouTube downloads), or odd mp3s. It streams
// from the file itself, any length.
namespace
{
    struct FdPlayer { SLObjectItf obj = nullptr; SLPlayItf play = nullptr; SLVolumeItf vol = nullptr; SLSeekItf seek = nullptr; int fd = -1; };
    // 5.02.58: the freeze with Wi-Fi / mobile data off (neon main menu). A URL player (the menu music, Solo, Day Z
    // ambience, server audio) whose network went away blocks inside Android's media player: Destroy() waits for the
    // stuck HTTP read / DNS lookup, and GetPosition() can wait on the same player. Both ran on the game thread every
    // tick, so the game froze until the network timed out. Now the player lives in a shared handle: a small thread reads
    // its position / length (the game thread only reads the numbers), and it is stopped and destroyed on its own thread.
    struct FdHandle
    {
        FdPlayer p;
        std::atomic<uint32_t> posMs{0}, durMs{0};
        std::atomic<bool> dead{false}, end{false};
        // 5.02.68: the SIGABRT "Binder thread started or Java binder used, but env null" when the menu music stops
        // at a join. Destroying a URL player makes Android's media server call back into the game's Java HTTP
        // connection (MediaHTTPConnection.disconnect) on the thread that destroys it. The last reference used to
        // go away in the position thread *after* it had left the JVM - no JNIEnv, Android aborts the game.
        // The player is now always destroyed with the thread attached, whichever thread lets it go.
        ~FdHandle() { MPJvm::ThreadScope jvm("MP audio destroy"); if(p.obj) (*p.obj)->Destroy(p.obj); if(p.fd >= 0) close(p.fd); }
    };
    std::shared_ptr<FdHandle> s_FdH;
    bool s_FdMode = false, s_FdTried = false;
    std::string s_StPath;
    char s_StInfo[160] = "";
    void FdCb(SLPlayItf, void* ctx, SLuint32 ev) { if((ev & SL_PLAYEVENT_HEADATEND) && ctx) ((FdHandle*)ctx)->end = true; }
    bool FdEnded() { return s_FdH && s_FdH->end.load(); }
    // drops the player off the game thread (its last reference goes away there)
    void FdRelease(std::shared_ptr<FdHandle> h)
    {
        if(!h) return;
        h->dead = true;
        try
        {
            std::thread([h]() mutable {
                MPJvm::ThreadScope jvm("MP audio release");
                if(h->p.play) (*h->p.play)->SetPlayState(h->p.play, SL_PLAYSTATE_STOPPED);
                h.reset();   // Destroy (may wait for a dead network - fine here)
            }).detach();
        }
        catch(...) { h.reset(); }   // no thread left (32-bit, many mods): do it here
    }
    void FdRelease(FdPlayer& p)   // a bare player (not started yet)
    {
        if(!p.obj) { if(p.fd >= 0) close(p.fd); p = FdPlayer(); return; }
        auto h = std::make_shared<FdHandle>(); h->p = p; p = FdPlayer();
        FdRelease(h);
    }
    void FdStop()
    {
        std::shared_ptr<FdHandle> h; h.swap(s_FdH);
        s_FdMode = false;
        FdRelease(h);
    }
    // the player becomes the current one: callbacks, the position thread
    void FdAdopt(const FdPlayer& p)
    {
        auto h = std::make_shared<FdHandle>(); h->p = p;
        (*p.play)->RegisterCallback(p.play, FdCb, h.get());
        (*p.play)->SetCallbackEventsMask(p.play, SL_PLAYEVENT_HEADATEND);
        s_FdH = h; s_FdMode = true;
        try
        {
            std::weak_ptr<FdHandle> wh = h;   // 5.02.68: a weak reference - the player is never let go after the JVM detach
            std::thread([wh]() {
                MPJvm::ThreadScope jvm("MP audio pos");
                for(;;)
                {
                    {
                        std::shared_ptr<FdHandle> h = wh.lock();
                        if(!h || h->dead.load()) break;
                        SLmillisecond ms = 0;
                        if(h->p.play && (*h->p.play)->GetPosition(h->p.play, &ms) == SL_RESULT_SUCCESS) h->posMs = ms;
                        ms = 0;
                        if(h->p.play && (*h->p.play)->GetDuration(h->p.play, &ms) == SL_RESULT_SUCCESS) h->durMs = ms == SL_TIME_UNKNOWN ? 0 : ms;
                    }   // (if this was the last reference, the player is destroyed here, still attached)
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            }).detach();
        }
        catch(...) {}
    }
    void FdVolume(float volume)
    {
        if(s_FdH) SL_SET_VOL(s_FdH->p.vol, volume);
    }
    bool FdStart(const std::string& path, float volume)
    {
        FdStop(); s_FdTried = true;
        int fd = open(path.c_str(), O_RDONLY);
        if(fd < 0) { snprintf(s_StInfo, sizeof(s_StInfo), "Android decoder: can't open the file"); return false; }
        SLDataLocator_AndroidFD loc = { SL_DATALOCATOR_ANDROIDFD, fd, 0, SL_DATALOCATOR_ANDROIDFD_USE_FILE_SIZE };
        SLDataFormat_MIME mime = { SL_DATAFORMAT_MIME, nullptr, SL_CONTAINERTYPE_UNSPECIFIED };
        SLDataSource src = { &loc, &mime };
        SLDataLocator_OutputMix outMix = { SL_DATALOCATOR_OUTPUTMIX, g_Mix };
        SLDataSink sink = { &outMix, nullptr };
        const SLInterfaceID ids[1] = { SL_IID_VOLUME };
        const SLboolean req[1] = { SL_BOOLEAN_FALSE };
        FdPlayer p; p.fd = fd;
        if((*g_EngineItf)->CreateAudioPlayer(g_EngineItf, &p.obj, &src, &sink, 1, ids, req) != SL_RESULT_SUCCESS)
        { close(fd); snprintf(s_StInfo, sizeof(s_StInfo), "Android decoder: format not supported"); return false; }
        if((*p.obj)->Realize(p.obj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS || (*p.obj)->GetInterface(p.obj, SL_IID_PLAY, &p.play) != SL_RESULT_SUCCESS)
        { (*p.obj)->Destroy(p.obj); close(fd); snprintf(s_StInfo, sizeof(s_StInfo), "Android decoder: the file can't be decoded (not audio / damaged)"); return false; }
        if((*p.obj)->GetInterface(p.obj, SL_IID_VOLUME, &p.vol) != SL_RESULT_SUCCESS) p.vol = nullptr;
        FdAdopt(p);   // 5.02.58
        FdVolume(volume);
        if((*p.play)->SetPlayState(p.play, SL_PLAYSTATE_PLAYING) != SL_RESULT_SUCCESS)
        { FdStop(); snprintf(s_StInfo, sizeof(s_StInfo), "Android decoder: could not start"); return false; }
        snprintf(s_StInfo, sizeof(s_StInfo), "playing with the Android decoder");
        return true;
    }
    // 4.07: media player: music streamed from a URL (Host/Media in the repo) by the Android decoder, over
    // Wi-Fi or mobile data, so the big mp3s don't have to be in files/TYFMC/MP. Prepare (Realize) blocks while it
    // connects, so it runs on its own thread; the game thread takes the ready player in StreamUpdate.
    std::mutex s_UrlMx; int s_UrlGen = 0; bool s_UrlPending = false; FdPlayer s_UrlReady; bool s_UrlHave = false; float s_UrlVol = 0.0f;
    float s_UrlStart = 0.0f;   // 5.02.44: where a URL track starts (a stalled stream carries on there)
    std::string s_UrlErr;
    void UrlThread(std::string url, int gen)
    {
        MPJvm::ThreadScope jvm("MP url audio");
        SLDataLocator_URI loc = { SL_DATALOCATOR_URI, (SLchar*)url.c_str() };
        SLDataFormat_MIME mime = { SL_DATAFORMAT_MIME, nullptr, SL_CONTAINERTYPE_UNSPECIFIED };
        SLDataSource src = { &loc, &mime };
        SLDataLocator_OutputMix outMix = { SL_DATALOCATOR_OUTPUTMIX, g_Mix };
        SLDataSink sink = { &outMix, nullptr };
        const SLInterfaceID ids[2] = { SL_IID_VOLUME, SL_IID_SEEK };
        const SLboolean req[2] = { SL_BOOLEAN_FALSE, SL_BOOLEAN_FALSE };
        FdPlayer p; const char* err = nullptr;
        if((*g_EngineItf)->CreateAudioPlayer(g_EngineItf, &p.obj, &src, &sink, 2, ids, req) != SL_RESULT_SUCCESS) { p.obj = nullptr; err = "media player: this phone can't play URLs"; }
        else if((*p.obj)->Realize(p.obj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS || (*p.obj)->GetInterface(p.obj, SL_IID_PLAY, &p.play) != SL_RESULT_SUCCESS)
        { (*p.obj)->Destroy(p.obj); p = FdPlayer(); err = "media player: no internet / the music URL did not answer"; }
        else
        {
            if((*p.obj)->GetInterface(p.obj, SL_IID_VOLUME, &p.vol) != SL_RESULT_SUCCESS) p.vol = nullptr;
            if((*p.obj)->GetInterface(p.obj, SL_IID_SEEK, &p.seek) != SL_RESULT_SUCCESS) p.seek = nullptr;
        }
        std::lock_guard<std::mutex> lk(s_UrlMx);
        if(gen != s_UrlGen) { if(p.obj) (*p.obj)->Destroy(p.obj); return; }   // stopped meanwhile
        s_UrlPending = false;
        if(err) { s_UrlErr = err; return; }
        s_UrlReady = p; s_UrlHave = true;
    }
    void UrlCancel() { std::lock_guard<std::mutex> lk(s_UrlMx); ++s_UrlGen; s_UrlPending = false; if(s_UrlHave) FdRelease(s_UrlReady); s_UrlHave = false; s_UrlReady = FdPlayer(); }   // 5.02.58: off the game thread
    // what is in the file? 1 = mp3, 2 = something else (m4a / ogg / webm / wav ...), 0 = unreadable / empty
    int Sniff(const char* path, char* what, size_t n)
    {
        FILE* f = fopen(path, "rb"); if(!f) { snprintf(what, n, "missing"); return 0; }
        unsigned char h[16] = {}; size_t got = fread(h, 1, sizeof(h), f); fclose(f);
        if(got < 12) { snprintf(what, n, "empty / too small"); return 0; }
        if(h[0] == 'I' && h[1] == 'D' && h[2] == '3') { snprintf(what, n, "mp3 (ID3)"); return 1; }
        if(h[0] == 0xFF && (h[1] & 0xE0) == 0xE0 && (h[1] & 0x06) != 0) { snprintf(what, n, (h[1] & 0x06) == 0x02 ? "mp3" : "aac / mp2 (adts)"); return (h[1] & 0x06) == 0x02 ? 1 : 2; }
        if(!memcmp(h + 4, "ftyp", 4)) { snprintf(what, n, "m4a / mp4 (AAC) renamed to .mp3"); return 2; }
        if(!memcmp(h, "OggS", 4)) { snprintf(what, n, "ogg / opus renamed to .mp3"); return 2; }
        if(h[0] == 0x1A && h[1] == 0x45 && h[2] == 0xDF && h[3] == 0xA3) { snprintf(what, n, "webm / opus renamed to .mp3"); return 2; }
        if(!memcmp(h, "RIFF", 4)) { snprintf(what, n, "wav"); return 2; }
        if(!memcmp(h, "fLaC", 4)) { snprintf(what, n, "flac"); return 2; }
        if(h[0] == '<' || (h[0] == 0xEF && h[1] == 0xBB)) { snprintf(what, n, "a web page (html), not audio - the download failed"); return 0; }
        snprintf(what, n, "unknown (%02X %02X %02X %02X)", h[0], h[1], h[2], h[3]); return 2;
    }
}

bool Sound::StreamPlay(const char* path, float volume, float startSec)
{
    s_StInfo[0] = 0;
    if(!path || !*path || !EnsureEngine()) { snprintf(s_StInfo, sizeof(s_StInfo), "no audio engine"); return false; }
    char what[96]; int kind = Sniff(path, what, sizeof(what));
    StreamStop();
    s_StreamKey = VolKey(path); s_fStreamBase = volume;   // 5.02.47
    s_fStreamMul = VolOf(path); volume *= s_fStreamMul;   // 5.02.31
    s_StPath = path; s_FdTried = false;
    if(kind == 0) { snprintf(s_StInfo, sizeof(s_StInfo), "file: %s", what); return false; }
    if(kind == 2)
    {
        bool ok = FdStart(s_StPath, volume);
        char tmp[160]; snprintf(tmp, sizeof(tmp), "file: %s - %s", what, s_StInfo); snprintf(s_StInfo, sizeof(s_StInfo), "%s", tmp);
        return ok;
    }
    {
        std::lock_guard<std::mutex> lk(s_St.mx);
        int g = s_St.gen + 1; s_St.gen = g;
        for(int i = 0; i < ST_SLOTS; ++i) { s_St.slotState[i] = 0; s_St.slot[i].clear(); }
        s_St.head = 0; s_St.played = 0; s_St.eof = false; s_St.failed = false; s_St.active = true;
        s_St.ch = 0; s_St.hz = 0; s_St.dur = 0; s_St.vol = volume;
        s_St.startSec = startSec > 0.0f ? startSec : 0.0f;
        std::thread(StDecodeThread, std::string(path), g, s_St.startSec).detach();
    }
    snprintf(s_StInfo, sizeof(s_StInfo), "file: %s - decoding", what);
    return true;
}

bool Sound::StreamPlayUrl(const char* url, float volume, float startSec)
{
    s_StInfo[0] = 0;
    if(!url || strncmp(url, "http", 4) || !EnsureEngine()) { snprintf(s_StInfo, sizeof(s_StInfo), "no audio engine / bad URL"); return false; }
    StreamStop();
    s_StreamKey = VolKey(url); s_fStreamBase = volume;   // 5.02.47
    s_fStreamMul = VolOf(url); volume *= s_fStreamMul;   // 5.02.31
    s_StPath.clear(); s_FdTried = true;
    { std::lock_guard<std::mutex> ls(s_St.mx); s_St.eof = false; s_St.failed = false; s_St.active = false; }
    {
        std::lock_guard<std::mutex> lk(s_UrlMx);
        int g = ++s_UrlGen; s_UrlPending = true; s_UrlVol = volume; s_UrlErr.clear(); s_UrlStart = startSec > 0.0f ? startSec : 0.0f;
        std::thread(UrlThread, std::string(url), g).detach();
    }
    snprintf(s_StInfo, sizeof(s_StInfo), "media player: connecting");
    return true;
}

void Sound::StreamUpdate()
{
    {   // 4.07: the URL player is ready (or failed)
        std::unique_lock<std::mutex> lk(s_UrlMx);
        if(s_UrlHave)
        {
            FdPlayer p = s_UrlReady; s_UrlHave = false; s_UrlReady = FdPlayer(); float vol = s_UrlVol, from = s_UrlStart; s_UrlStart = 0.0f; lk.unlock();
            FdStop();
            FdAdopt(p);   // 5.02.58
            FdVolume(vol);
            if(from > 0.0f && p.seek) (*p.seek)->SetPosition(p.seek, (SLmillisecond)(from * 1000.0f), SL_SEEKMODE_FAST);   // 5.02.44
            if((*p.play)->SetPlayState(p.play, SL_PLAYSTATE_PLAYING) != SL_RESULT_SUCCESS) { FdStop(); snprintf(s_StInfo, sizeof(s_StInfo), "media player: could not start"); s_St.failed = true; }
            else snprintf(s_StInfo, sizeof(s_StInfo), "media player: streaming");
            return;
        }
        if(!s_UrlPending && !s_UrlErr.empty()) { snprintf(s_StInfo, sizeof(s_StInfo), "%s", s_UrlErr.c_str()); s_UrlErr.clear(); }
    }
    if(s_FdMode) { if(FdEnded()) { FdStop(); s_St.eof = true; s_St.failed = false; s_St.active = false; } return; }
    if(!s_St.active) return;
    std::unique_lock<std::mutex> lk(s_St.mx);
    if(s_St.failed)
    {
        s_St.active = false;
        if(s_St.played == 0 && !s_FdTried && !s_StPath.empty())
        {   // minimp3 couldn't read it: the Android decoder plays (almost) everything
            float vol = s_St.vol; lk.unlock();
            if(FdStart(s_StPath, vol)) { char tmp[160]; snprintf(tmp, sizeof(tmp), "mp3 decoder failed - %s", s_StInfo); snprintf(s_StInfo, sizeof(s_StInfo), "%s", tmp); }
        }
        return;
    }
    if(s_St.ch <= 0) return;
    Voice& v = s_St.v;
    if(!v.obj || v.ch != s_St.ch || v.hz != s_St.hz)
    {
        bool anyQueued = false; for(int i = 0; i < ST_SLOTS; ++i) if(s_St.slotState[i] == 2) anyQueued = true;
        if(anyQueued) return;
        if(v.obj) { (*v.obj)->Destroy(v.obj); v = Voice(); }   // nothing queued: safe
        if(!BuildVoice(v, s_St.ch, s_St.hz)) { s_St.failed = true; return; }   // (the Android decoder next tick)
        (*v.bq)->RegisterCallback(v.bq, StDone, nullptr);
        snprintf(s_StInfo, sizeof(s_StInfo), "playing mp3 (%d Hz, %d ch)", s_St.hz, s_St.ch);
    }
    SetVoiceVolume(v, s_St.vol);
    while(StEnqueueNextLocked()) {}
    if(s_St.eof) { bool any = false; for(int i = 0; i < ST_SLOTS; ++i) if(s_St.slotState[i]) any = true; if(!any) s_St.active = false; }
}

const char* Sound::StreamInfo() { return s_StInfo; }

void Sound::StreamSetVolume(float volume)
{
    s_fStreamBase = volume;
    s_fStreamMul = VolOf(s_StreamKey.c_str()); volume *= s_fStreamMul;   // 5.02.47: live file volume
    { std::lock_guard<std::mutex> lu(s_UrlMx); s_UrlVol = volume; }
    if(s_FdMode) { FdVolume(volume); return; }
    std::lock_guard<std::mutex> lk(s_St.mx);
    s_St.vol = volume;
    if(s_St.v.obj) SetVoiceVolume(s_St.v, volume);
}

namespace
{
    void ReapplyStreamVolume() { Sound::StreamSetVolume(s_fStreamBase); }
    void ReapplyMusicVolume()  { Sound::SetMusicVolume(s_fMusicBase); }
}

void Sound::StreamStop()
{
    UrlCancel();   // 4.07
    FdStop();
    std::lock_guard<std::mutex> lk(s_St.mx);
    s_St.gen = s_St.gen + 1;   // the decoder thread ends itself
    if(s_St.v.obj && s_St.v.bq) (*s_St.v.bq)->Clear(s_St.v.bq);
    for(int i = 0; i < ST_SLOTS; ++i) s_St.slotState[i] = 0;
    s_St.head = 0; s_St.active = false;
}

bool  Sound::StreamActive() { { std::lock_guard<std::mutex> lk(s_UrlMx); if(s_UrlPending || s_UrlHave) return true; } return s_FdMode || s_St.active; }
bool  Sound::StreamEnded() { { std::lock_guard<std::mutex> lk(s_UrlMx); if(s_UrlPending || s_UrlHave) return false; } return !s_FdMode && !s_St.active && s_St.eof && !s_St.failed; }
float Sound::StreamPosition()
{
    if(s_FdMode) return s_FdH ? s_FdH->posMs.load() / 1000.0f : 0.0f;   // 5.02.58: read by the player's own thread
    return s_St.hz > 0 ? (float)((double)s_St.played / s_St.hz) + s_St.startSec : 0.0f;
}
float Sound::StreamBaseVolume() { return s_fStreamBase; }
float Sound::StreamDuration()
{
    if(s_FdMode) return s_FdH ? s_FdH->durMs.load() / 1000.0f : 0.0f;
    return s_St.dur;
}

// 5.02.58: what the volume of the stream is listed under (server audio streams have any URL: one row for all of them)
void Sound::StreamSetVolumeKey(const char* key)
{
    s_StreamKey = VolKey(key);
    Sound::StreamSetVolume(s_fStreamBase);
}
void  Sound::SetMasterVolume(float v)
{
    if(v < 0) v = 0; if(v > 1.0f) v = 1.0f;
    s_fMaster = v;
    ReapplyStreamVolume(); ReapplyMusicVolume();
}
float Sound::MasterVolume() { return s_fMaster.load(); }

void* Sound::EngineItf() { return EnsureEngine() ? (void*)g_EngineItf : nullptr; }   // 3.11
void* Sound::OutputMix() { return EnsureEngine() ? (void*)g_Mix : nullptr; }

// 3.29: UI clicks - a missing file is decoded (tried) once and then just stays silent
// 4.00.2: UI clicks the mp3 decoder can't read (a wav / ogg / m4a named .mp3): the Android decoder,
// one player per file, rewound and started again on every click
namespace
{
    struct UiFd { std::string path; SLObjectItf obj = nullptr; SLPlayItf play = nullptr; SLVolumeItf vol = nullptr; SLSeekItf seek = nullptr; int fd = -1; bool bad = false; };
    UiFd s_UiFd[3];
    bool UiFdPlay(const char* path, float volume)
    {
        UiFd* u = nullptr;
        for(UiFd& x : s_UiFd) if(x.path == path) { u = &x; break; }
        if(!u) { for(UiFd& x : s_UiFd) if(x.path.empty()) { u = &x; break; } }
        if(!u) { u = &s_UiFd[0]; if(u->obj) (*u->obj)->Destroy(u->obj); if(u->fd >= 0) close(u->fd); *u = UiFd(); }
        if(u->bad) return false;
        if(!u->obj)
        {
            u->path = path;
            int fd = open(path, O_RDONLY); if(fd < 0) { u->bad = true; return false; }
            SLDataLocator_AndroidFD loc = { SL_DATALOCATOR_ANDROIDFD, fd, 0, SL_DATALOCATOR_ANDROIDFD_USE_FILE_SIZE };
            SLDataFormat_MIME mime = { SL_DATAFORMAT_MIME, nullptr, SL_CONTAINERTYPE_UNSPECIFIED };
            SLDataSource src = { &loc, &mime };
            SLDataLocator_OutputMix outMix = { SL_DATALOCATOR_OUTPUTMIX, g_Mix };
            SLDataSink sink = { &outMix, nullptr };
            const SLInterfaceID ids[2] = { SL_IID_VOLUME, SL_IID_SEEK };
            const SLboolean req[2] = { SL_BOOLEAN_FALSE, SL_BOOLEAN_FALSE };
            if((*g_EngineItf)->CreateAudioPlayer(g_EngineItf, &u->obj, &src, &sink, 2, ids, req) != SL_RESULT_SUCCESS) { close(fd); u->obj = nullptr; u->bad = true; return false; }
            if((*u->obj)->Realize(u->obj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS || (*u->obj)->GetInterface(u->obj, SL_IID_PLAY, &u->play) != SL_RESULT_SUCCESS)
            { (*u->obj)->Destroy(u->obj); close(fd); u->obj = nullptr; u->bad = true; logger->Error("Sound: %s can't be played", path); return false; }
            if((*u->obj)->GetInterface(u->obj, SL_IID_VOLUME, &u->vol) != SL_RESULT_SUCCESS) u->vol = nullptr;
            if((*u->obj)->GetInterface(u->obj, SL_IID_SEEK, &u->seek) != SL_RESULT_SUCCESS) u->seek = nullptr;
            u->fd = fd;
            logger->Info("Sound: %s plays with the Android decoder", path);
        }
        SL_SET_VOL(u->vol, volume);
        (*u->play)->SetPlayState(u->play, SL_PLAYSTATE_STOPPED);   // stopped = back to the start
        if(u->seek) (*u->seek)->SetPosition(u->seek, 0, SL_SEEKMODE_FAST);
        return (*u->play)->SetPlayState(u->play, SL_PLAYSTATE_PLAYING) == SL_RESULT_SUCCESS;
    }
}

bool Sound::PlayUi(const char* path, float volume)
{
    if(!path || !*path || !EnsureEngine()) return false;
    volume *= VolOf(path);   // 5.02.31
    const Pcm* p = Decode(path);
    if(!p) return UiFdPlay(path, volume);   // 4.00.2
    Voice& v = s_Ui[s_nNextUi]; s_nNextUi = (s_nNextUi + 1) % 2;
    return PlayOn(v, *p, volume);
}
