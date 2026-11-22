#include "file_manager.h"
#include "utils/android.h"
#include <filesystem>
#include <string>
#include <system_error>
#include <cstdlib>

void file_manager::init()
{
  external_storage = std::filesystem::path { andutils::jni::get_external_storage() }.parent_path();
  public_external_storage = std::filesystem::path { andutils::jni::get_external_media() };

  // Initialize internal storage path
  internal_storage = "/data/data/" + andutils::get_package_name() + "/";

  // MP Multiplayer: use the folder that really has files/TYFMC/MP (MP logs it as "Data folder: ...")
  std::error_code ec;
  mp_root = external_storage;
  const std::string pkg = andutils::get_package_name();
  const std::filesystem::path cands[] = {
    external_storage,
    std::filesystem::path { "/storage/emulated/0/Android_unprotected/data" } / pkg,
    std::filesystem::path { "/sdcard/Android_unprotected/data" } / pkg,
  };
  // 5.02.44: MP hands over its own data folder (MP_DATA_ROOT = the folder that holds files/). Before, the first
  // candidate with files/TYFMC/MP won - a leftover Android/data/<pkg>/files/TYFMC/MP made MonetLoader write
  // moonloader.log and load scripts there while MP used Android_unprotected. Without it: Android_unprotected first.
  const char* env = getenv("MP_DATA_ROOT");
  if (env && *env && std::filesystem::exists(std::filesystem::path { env } / "files" / "TYFMC" / "MP", ec)) {
    mp_root = std::filesystem::path { env };
    return;
  }
  for (const auto& c : { cands[1], cands[2], cands[0] }) {
    if (std::filesystem::exists(c / "files" / "TYFMC" / "MP", ec)) {
      mp_root = c;
      break;
    }
  }
}