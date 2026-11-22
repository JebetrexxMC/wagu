#include "logger.h"
#include "file_manager.h"
#include "script/script_manager.h"
#include <filesystem>
#include <spdlog/common.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/callback_sink.h>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

namespace {
class custom_level_formatter : public spdlog::custom_flag_formatter {
  public:
  void format(const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest) override
  {
    dest.append(std::string_view { "(" });
    if (msg.level == spdlog::level::level_enum::info) {
      dest.append(std::string_view { "system" });
    } else if (msg.level == spdlog::level::level_enum::trace) {
      dest.append(std::string_view { "script" });
    } else {
      dest.append(spdlog::level::to_string_view(msg.level));
    }
    dest.append(std::string_view { ")" });
  }

  [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override
  {
    return spdlog::details::make_unique<custom_level_formatter>();
  }
};
}

void logger::init()
{
  std::error_code ec;
  if (!std::filesystem::exists(file_manager::mp_moonloader_path().parent_path(), ec)) {
    return;
  }

  // MP Multiplayer: files/TYFMC/MP/moonloader/moonloader.log (next to the scripts, like PC MoonLoader)
  auto rel_file_path = std::filesystem::path{ "files" } / "TYFMC" / "MP" / "moonloader" / "moonloader.log";
  auto file_path = file_manager::mp_moonloader_path() / "moonloader.log";
  relative_path = rel_file_path.parent_path();
  absolute_path = file_path.parent_path();
  std::filesystem::create_directories(absolute_path, ec);

  // 5.02.44: a folder that can't be written no longer throws out of init (no MonetLoader at all); the important
  // lines (warnings, errors, [LuaScripts] / [mimgui] / HassleHUD) are mirrored into MP's own log as "[ML] ..."
  std::vector<spdlog::sink_ptr> sinks;
  try {
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file_path, true);
    file_sink->set_level(spdlog::level::trace);
    sinks.push_back(file_sink);
  } catch (...) {
  }
  typedef void (*mp_log_fn)(int, const char*);
  mp_log_fn mp_log = nullptr;
  if (const char* e = getenv("MP_LOG_FN")) mp_log = (mp_log_fn)(uintptr_t)strtoull(e, nullptr, 16);
  if (mp_log) {
    sinks.push_back(std::make_shared<spdlog::sinks::callback_sink_mt>([mp_log](const spdlog::details::log_msg& m) {
      std::string t(m.payload.data(), m.payload.size());
      if (m.level >= spdlog::level::warn || t.find("[LuaScripts]") != std::string::npos || t.find("[mimgui]") != std::string::npos
          || t.find("HassleHUD") != std::string::npos || t.find("rror") != std::string::npos)
        mp_log((int)m.level, t.c_str());
    }));
  }
  if (sinks.empty()) return;

  auto logger = std::make_shared<spdlog::logger>("monetloader", sinks.begin(), sinks.end());
  logger->set_level(spdlog::level::trace);
  logger->flush_on(spdlog::level::trace);

  auto formatter = std::make_unique<spdlog::pattern_formatter>();
  formatter->add_flag<custom_level_formatter>('*').set_pattern("[%T.%f] %* %v");
  logger->set_formatter(std::move(formatter));

  spdlog::set_default_logger(logger);
}

void logger::invoke_message_callbacks(int level, lua::script* script, const std::string& msg)
{
  lua::script_manager::for_each_event_handler("onSystemMessage", [level, script, &msg](sol::protected_function fn) -> std::optional<sol::error> {
    auto pfr = script == nullptr ? fn(msg, level, sol::lua_nil) : fn(msg, level, script->info);
    if (!pfr.valid()) {
      sol::error e = pfr;
      return e;
    }
    return std::nullopt;
  });
}

void logger::invoke_script_message_callbacks(lua::script& script, const std::string& msg)
{
  lua::script_manager::for_each_event_handler("onScriptMessage", [&script, &msg](sol::protected_function fn) -> std::optional<sol::error> {
    auto pfr = fn(msg, script.info);
    if (!pfr.valid()) {
      sol::error e = pfr;
      return e;
    }
    return std::nullopt;
  });
}