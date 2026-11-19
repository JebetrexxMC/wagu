#pragma once
#include "script/script.h"
#include <spdlog/spdlog.h>
#include <ctime>
#include <deque>


namespace logger {
inline std::filesystem::path absolute_path;
inline std::filesystem::path relative_path;

enum levels {
  LV_DEBUG = 2,
  LV_ERROR = 3,
  LV_WARN = 4,
  LV_SYSTEM = 5
};
void init();

// MonetLoader for MP: the last crashes (newest last) for MP's Script Manager. One entry:
// "unix time\tscript name\tscript path\terror text".
inline std::deque<std::string> mp_crashes {};
inline void mp_note_error(lua::script* script, const std::string& msg)
{
  if (!script) {
    return;
  }
  if (msg != "Script died due to an error.") {
    script->mp_last_error = msg;
    return;
  }
  std::string e = script->mp_last_error;
  for (auto& c : e) {
    if (c == '\t') c = ' ';
  }
  mp_crashes.push_back(std::to_string(static_cast<long long>(std::time(nullptr))) + "\t" + script->info.name + "\t" + script->info.path + "\t" + e);
  while (mp_crashes.size() > 30) {
    mp_crashes.pop_front();
  }
}

void invoke_message_callbacks(int level, lua::script* script, const std::string& msg);
void invoke_script_message_callbacks(lua::script& script, const std::string& msg);

template <int Level, bool Callbacks = true, typename... Args>
void log(lua::script* script, fmt::format_string<Args...> fmt, Args&&... args)
{
  std::string msg = fmt::format(fmt, std::forward<Args>(args)...);

  if constexpr (Level == LV_SYSTEM) {
    if (script) {
      spdlog::info("{}: {}", script->info.name, msg);
    } else {
      spdlog::info("{}", msg);
    }
  } else if constexpr (Level == LV_DEBUG) {
    if (script) {
      spdlog::debug("{}: {}", script->info.name, msg);
    } else {
      spdlog::debug("{}", msg);
    }
  } else if constexpr (Level == LV_ERROR) {
    mp_note_error(script, msg);
    if (script) {
      spdlog::error("{}: {}", script->info.name, msg);
    } else {
      spdlog::error("{}", msg);
    }
  } else if constexpr (Level == LV_WARN) {
    if (script) {
      spdlog::warn("{}: {}", script->info.name, msg);
    } else {
      spdlog::warn("{}", msg);
    }
  }

  if constexpr (Callbacks) {
    invoke_message_callbacks(Level, script, msg);
  }
}

template <bool Callbacks = true, typename... Args>
void script_log(lua::script& script, fmt::format_string<Args...> fmt, Args&&... args)
{
  std::string msg = fmt::format(fmt, std::forward<Args>(args)...);

  spdlog::trace("{}: {}", script.info.name, msg);

  if constexpr (Callbacks) {
    invoke_script_message_callbacks(script, msg);
  }
}

}