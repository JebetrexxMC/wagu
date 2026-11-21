// 5.02.36: extractGif(gif, outDir) for HassleHUD (and any MonetLoader mod that ships libGIF.so).
// HassleHUD's lib/libGIF.so is a 32-bit ARM file kept in /storage/... - Android does not dlopen a .so from shared
// storage (and never a 32-bit one on a 64-bit game), so the fist GIF stayed empty. MP exports the same function: the
// loader (lua_scripts_loader.lua / the MP runner) hands ffi.load("...libGIF.so") MP's own library instead.
// Same output as libGIF.so: outDir/0.bmp, 1.bmp ... (32-bit BMP, like stbi_write_bmp with 4 channels), returns the
// number of frames, -1 on error.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <mod/amlmod.h>
#include <mod/logger.h>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#include "../../imgui/stb_image.h"

static bool PutU16(FILE* f, unsigned v) { unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) }; return fwrite(b, 1, 2, f) == 2; }
static bool PutU32(FILE* f, unsigned v) { unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) }; return fwrite(b, 1, 4, f) == 4; }

static bool WriteBmp32(const char* path, int w, int h, const unsigned char* rgba)
{
    FILE* f = fopen(path, "wb"); if(!f) return false;
    const unsigned hdr = 14 + 108, img = (unsigned)w * (unsigned)h * 4;
    bool ok = fwrite("BM", 1, 2, f) == 2 && PutU32(f, hdr + img) && PutU32(f, 0) && PutU32(f, hdr);
    ok = ok && PutU32(f, 108) && PutU32(f, (unsigned)w) && PutU32(f, (unsigned)h) && PutU16(f, 1) && PutU16(f, 32) && PutU32(f, 3)
            && PutU32(f, img) && PutU32(f, 2835) && PutU32(f, 2835) && PutU32(f, 0) && PutU32(f, 0)
            && PutU32(f, 0x00FF0000u) && PutU32(f, 0x0000FF00u) && PutU32(f, 0x000000FFu) && PutU32(f, 0xFF000000u)
            && PutU32(f, 0x73524742u);   // 'sRGB'
    for(int i = 0; ok && i < 48; ++i) ok = fputc(0, f) != EOF;   // endpoints + gamma
    unsigned char* row = (unsigned char*)malloc((size_t)w * 4);
    for(int y = h - 1; ok && row && y >= 0; --y)   // bottom-up, BGRA
    {
        const unsigned char* s = rgba + (size_t)y * w * 4;
        for(int x = 0; x < w; ++x) { row[x * 4] = s[x * 4 + 2]; row[x * 4 + 1] = s[x * 4 + 1]; row[x * 4 + 2] = s[x * 4]; row[x * 4 + 3] = s[x * 4 + 3]; }
        ok = fwrite(row, 1, (size_t)w * 4, f) == (size_t)w * 4;
    }
    free(row);
    ok = (fclose(f) == 0) && ok && row;
    return ok;
}

extern "C" __attribute__((visibility("default"))) int extractGif(const char* gifPath, const char* outDir)
{
    if(!gifPath || !outDir) return -1;
    FILE* f = fopen(gifPath, "rb");
    if(!f) { logger->Error("extractGif: can't open %s", gifPath); return -1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if(n <= 0 || n > 64 * 1024 * 1024) { fclose(f); return -1; }
    unsigned char* buf = (unsigned char*)malloc((size_t)n);
    if(!buf) { fclose(f); return -1; }
    const bool rd = fread(buf, 1, (size_t)n, f) == (size_t)n; fclose(f);
    if(!rd) { free(buf); return -1; }
    int* delays = nullptr; int w = 0, h = 0, frames = 0, comp = 0;
    unsigned char* px = stbi_load_gif_from_memory(buf, (int)n, &delays, &w, &h, &frames, &comp, 4);
    free(buf);
    if(!px) { logger->Error("extractGif: %s is not a readable GIF (%s)", gifPath, stbi_failure_reason()); return -1; }
    mkdir(outDir, 0777);
    int written = 0; char path[1024];
    for(int i = 0; i < frames; ++i)
    {
        snprintf(path, sizeof(path), "%s/%d.bmp", outDir, i);
        if(WriteBmp32(path, w, h, px + (size_t)i * w * h * 4)) ++written;
    }
    stbi_image_free(px); if(delays) stbi_image_free(delays);
    logger->Info("extractGif: %s -> %d frame(s) %dx%d in %s", gifPath, written, w, h, outDir);
    return frames;
}
