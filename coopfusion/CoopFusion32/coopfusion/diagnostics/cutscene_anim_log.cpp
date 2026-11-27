#include "cutscene_anim_log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace {
constexpr char kPrimaryPath[] = "/sdcard/cutscenes anim log";
constexpr char kFallbackPath[] = "/storage/emulated/0/cutscenes anim log";
constexpr char kPrivateFallbackPath[] =
    "/data/user/0/com.rockstargames.gtasaclone/files/cutscenes anim log";
std::mutex g_cutsceneLogMutex;

FILE* OpenLog(const char* mode) {
    FILE* file = std::fopen(kPrimaryPath, mode);
    if (!file) file = std::fopen(kFallbackPath, mode);
    if (!file) file = std::fopen(kPrivateFallbackPath, mode);
    return file;
}
}

void CutsceneAnimLog(const char* format, ...) {
    if (!format) return;
    char message[768]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    std::scoped_lock lock(g_cutsceneLogMutex);
    FILE* file = OpenLog("a");
    if (!file) return;
    const std::time_t now = std::time(nullptr);
    std::fprintf(file, "%lld %s\n", static_cast<long long>(now), message);
    std::fflush(file);
    std::fclose(file);
}

void CutsceneAnimLogReset() {
    std::scoped_lock lock(g_cutsceneLogMutex);
    FILE* file = OpenLog("w");
    if (!file) return;
    std::fprintf(file, "CoopFusion cutscene animation diagnostics started\n");
    std::fflush(file);
    std::fclose(file);
}
