#pragma once
#include <math.h>
namespace Sound
{
    bool PlayFile(const char* absPath, float volume = 1.0f);
    bool PlayUi(const char* absPath, float volume = 1.0f);   // 3.29: UI clicks (own voices, never cut the hit / fx)
    bool PreloadHit(const char* absPath);                    // 3.00: hit sound, loaded once
    bool PlayHit(const char* absPath, float volume = 1.0f);  // 3.00: restarts a ready voice (cheap, no stutter)
    // 3.04: one looping music voice (decoded on a thread; call MusicUpdate() every game tick)
    bool PlayMusic(const char* absPath, float volume = 1.0f, bool loop = true);
    void MusicUpdate();
    void SetMusicVolume(float volume);
    void StopMusic();
    bool MusicPlaying();
    // 3.63: long tracks (1 h Day Z ambient): decoded on a thread a second at a time and streamed
    // into a buffer-queue voice (no full decode in RAM, no media framework / URI player)
    bool  StreamPlay(const char* absPath, float volume = 1.0f, float startSec = 0.0f);   // false = file missing / not an mp3; 5.02.06: startSec = resume there
    bool  StreamPlayUrl(const char* url, float volume = 1.0f);    // 4.07: media player (http/https, Android decoder, async)
    void  StreamUpdate();                // call every game tick (starts the queue when data is ready)
    void  StreamSetVolume(float volume);
    void  StreamStop();
    bool  StreamActive();                // started and not finished yet
    bool  StreamEnded();                 // the file played to its end
    float StreamPosition();              // seconds played
    float StreamDuration();              // seconds (estimated from the bitrate), 0 = unknown
    const char* StreamInfo();            // 4.00.1: what the file is / which decoder plays it / why it failed
    // 5.00.07: a short sound downloaded into memory (never saved in files/TYFMC/MP): DecodeMem on any
    // thread (once per slot), PlayMem on the game thread. slot 0 = notify.mp3 (notification system)
    bool  DecodeMem(int slot, const void* data, size_t size);
    bool  MemReady(int slot);
    bool  PlayMem(int slot, float volume = 1.0f);
    void* EngineItf();   // 3.11: the shared OpenSL engine (SLEngineItf) for voice chat
    void* OutputMix();   // 3.11: SLObjectItf output mix
}
