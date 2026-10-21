#include "SlippiGameFileLoader.h"

#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/Timer.h"
#include "Core/Boot/Boot.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/DVD/DVDThread.h"
#include "Core/System.h"
#include "SlippiRustExtensions.h"

std::string getFilePath(std::string file_name)
{
  std::string dir_path = File::GetSysDirectory();

  std::string file_path = dir_path + "GameFiles/GALE01/" + file_name;  // TODO: Handle other games?

  if (File::Exists(file_path))
  {
    return file_path;
  }

  file_path = file_path + ".diff";
  if (File::Exists(file_path))
  {
    return file_path;
  }

  return "";
}

bool SlippiGameFileLoader::readDiscFile(void* ctx, const char* file_name, const u8** out_data,
                                        uintptr_t* out_len)
{
  auto* loader = static_cast<SlippiGameFileLoader*>(ctx);
  std::string name(file_name);

  // ReadFile leaves the buffer untouched when it can't read the file, so clear it first to avoid
  // handing Rust the previous file
  loader->disc_read_buffer.clear();
  Core::System::GetInstance().GetDVDThread().ReadFile(name, loader->disc_read_buffer);

  *out_data = loader->disc_read_buffer.data();
  *out_len = loader->disc_read_buffer.size();
  return !loader->disc_read_buffer.empty();
}

bool SlippiGameFileLoader::resolveLinks(const std::string& file_name, std::string& contents)
{
  u64 start_ms = Common::Timer::NowMs();

  SlippiResolvedGameFile result =
      slprs_gamefile_resolve(file_name.c_str(), reinterpret_cast<const u8*>(contents.data()),
                             contents.size(), this, &SlippiGameFileLoader::readDiscFile);
  disc_read_buffer.clear();
  disc_read_buffer.shrink_to_fit();

  if (result.status == SLPRS_GAMEFILE_NO_LINKS)
  {
    return true;
  }

  if (result.status != SLPRS_GAMEFILE_RESOLVED)
  {
    // Serve nothing rather than a file with dangling links. The reason was logged by Rust
    ERROR_LOG_FMT(SLIPPI, "Serving nothing for {} because its links could not be resolved",
                  file_name);
    contents.clear();

    // The game halts as soon as it finds the file missing, so the player only learns why from this
    CriticalAlertFmtT("Slippi could not load {0}, so the game cannot continue.\n\n"
                      "This is most likely caused by your ISO: a game file Slippi needs from it "
                      "is missing or modified. Try an unmodified NTSC 1.02 ISO.",
                      file_name);
    return false;
  }

  contents.assign(reinterpret_cast<const char*>(result.data), result.len);
  slprs_gamefile_free(result);
  INFO_LOG_FMT(SLIPPI, "Resolved links in {}, {} bytes, took {} ms", file_name, contents.size(),
               Common::Timer::NowMs() - start_ms);

  // Drop a copy for offline comparison when the user has created the dump folder
  std::string dump_dir = File::GetUserPath(D_DUMP_IDX) + "GameFiles/";
  if (File::IsDirectory(dump_dir))
  {
    std::string dump_path = dump_dir + file_name;
    if (File::WriteStringToFile(dump_path, contents))
      INFO_LOG_FMT(SLIPPI, "Wrote resolved {} to {}", file_name, dump_path);
  }
  else
  {
    INFO_LOG_FMT(SLIPPI, "Create {} to dump resolved game files", dump_dir);
  }

  return true;
}

u32 SlippiGameFileLoader::LoadFile(Core::System& system, std::string file_name, std::string& data)
{
  if (file_cache.count(file_name))
  {
    data = file_cache[file_name];
    return static_cast<u32>(data.size());
  }

  if (grpsx_strings.count(file_name) && Core::GetState(system) == Core::State::Running)
  {
    std::vector<u8> buf;
    Core::System::GetInstance().GetDVDThread().ReadFile(file_name, buf);
    std::string contents(buf.begin(), buf.end());

    file_cache[file_name] = contents;
    data = file_cache[file_name];
    INFO_LOG_FMT(SLIPPI, "Preloaded Transformation: {} -> {}", file_name.c_str(), static_cast<u32>(data.size()));
    return static_cast<u32>(data.size());
  }

  INFO_LOG_FMT(SLIPPI, "Loading file: {}", file_name.c_str());


  std::string game_file_path = getFilePath(file_name);
  if (game_file_path.empty())
  {
    file_cache[file_name] = "";
    data = "";
    return 0;
  }

  std::string file_contents;
  File::ReadFileToString(game_file_path, file_contents);

  // If the file was a diff file and the game is running, load the main file from ISO and apply
  // patch
  if (game_file_path.substr(game_file_path.length() - 5) == ".diff" &&
      Core::GetState(system) == Core::State::Running)
  {
    std::vector<u8> buf;
    INFO_LOG_FMT(SLIPPI, "Will process diff");
    Core::System::GetInstance().GetDVDThread().ReadFile(file_name, buf);
    std::string diff_contents = file_contents;
    decoder.Decode((char*)buf.data(), buf.size(), diff_contents, &file_contents);
  }

  // Pull in any assets the file links to on the disc
  if (!file_contents.empty())
  {
    resolveLinks(file_name, file_contents);
  }

  file_cache[file_name] = file_contents;
  data = file_cache[file_name];
  INFO_LOG_FMT(SLIPPI, "File size: {}", static_cast<u32>(data.size()));
  return static_cast<u32>(data.size());
}
