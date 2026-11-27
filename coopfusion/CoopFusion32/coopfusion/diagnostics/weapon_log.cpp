#include "weapon_log.h"

#include <android/log.h>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace {
constexpr char kTag[] = "CoopFusion32";
constexpr char kLogPath[] = "/sdcard/CoopFusion32_weapon_sync.log";
constexpr char kFallbackLogPath[] = "/sdcard/Android/data/com.rockstargames.gtasa/files/CoopFusion32_weapon_sync.log";
std::mutex g_logMutex;
}

void WeaponFileLog(const char* format, ...)
{
    char message[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    __android_log_print(ANDROID_LOG_INFO, kTag, "%s", message);
    std::scoped_lock lock(g_logMutex);
    FILE* file = std::fopen(kLogPath, "a");
    if (!file) file = std::fopen(kFallbackLogPath, "a");
    if (!file) return;
    std::time_t now = std::time(nullptr);
    std::fprintf(file, "%lld %s\n", static_cast<long long>(now), message);
    std::fflush(file);
    std::fclose(file);
}

void WeaponFileLogReset()
{
    std::scoped_lock lock(g_logMutex);
    FILE* file = std::fopen(kLogPath, "w");
    if (!file) file = std::fopen(kFallbackLogPath, "w");
    if (file) {
        std::fprintf(file, "CoopFusion32 diagnostics started\n");
        std::fclose(file);
    }
}
