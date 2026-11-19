// MP (wagu) 5.02: open.mp RakNet built inside libMP / mp_server without the open.mp SDK.
// The few SDK types SAMPRakNet uses, as plain std types.
#pragma once
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <cstdarg>
#include <cstdio>

using StringView = std::string_view;
template <typename A, typename B> using Pair = std::pair<A, B>;
template <typename K, typename V> using FlatHashMap = std::unordered_map<K, V>;
template <typename K> using FlatHashSet = std::unordered_set<K>;

enum class LogLevel { Debug, Message, Warning, Error };

// logging goes to MP's log (set by SampNet); nothing else of open.mp's ICore is used
typedef void (*OmpLogFn)(const char* text);
struct ICore
{
    OmpLogFn fn = nullptr;
    void printLn(const char* fmt, ...)
    {
        char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
        if(fn) fn(b);
    }
    void logLn(LogLevel, const char* fmt, ...)
    {
        char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
        if(fn) fn(b);
    }
};

// SA-MP query ('SAMP' i / r / c / d / p): answered by the MP server's SA-MP gate.
// Returns the reply length written to out (0 = no reply).
typedef int (*OmpQueryFn)(const char* in, int inLen, char* out, int outCap);
