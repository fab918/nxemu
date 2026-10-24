// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_settings.h"
#include "yuzu_common/logging/log.h"
#include "yuzu_video_core/shader_profile_codec.h"
#include "yuzu_video_core/shader_profile_compact.h"
#include "yuzu_video_core/shader_profile_export.h"
#include "yuzu_video_core/shader_profile_context.h"
#include <fstream>
#include <iostream>
VideoSettings videoSettings{};
namespace Common::Log {
LogLevel g_classLevel[static_cast<uint8_t>(LogClass::Count)]{};
void FmtLogMessageImpl(LogClass, LogLevel, const char *, unsigned, const char *,
                       const char *, const fmt::format_args &) {}
} // namespace Common::Log
void assert_fail_impl() {
  throw std::runtime_error("renderer assertion");
}
[[noreturn]] void unreachable_impl() {
  throw std::runtime_error("renderer unreachable");
}
int main(int argc, char **argv) try {
  if (argc == 4 && std::string_view(argv[1]) == "--compact") {
    if (std::filesystem::file_size(argv[2]) > 33 * 1024 * 1024)
      throw std::runtime_error("profile size limit");
    std::ifstream file(argv[2], std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    const auto profile = VideoCommon::ShaderProfile::ParseMetadata(
        VideoCommon::ShaderProfile::Unpack(bytes));
    const auto compact = VideoCommon::ShaderProfile::PackCompact(profile);
    VideoCommon::ShaderProfile::Publish(argv[3], compact);
    std::cout << compact.size() << '\n';
    return 0;
  }
  if (argc != 10 && argc != 11)
    throw std::runtime_error(
        "usage: nxshader-export cache recording game-root base update|- "
        "title-id output index label [--compact|--auto]");
  VideoCommon::ShaderProfile::ExportOptions options;
  options.cache = argv[1];
  options.recording = argv[2];
  options.game_root = argv[3];
  options.base = argv[4];
  options.update = std::string_view(argv[5]) == "-" ? "" : argv[5];
  options.title_id = std::stoull(argv[6], nullptr, 16);
  options.output = argv[7];
  options.index = argv[8];
  options.label = argv[9];
  options.allow_partial = true;
  if (argc == 11) {
    if (std::string_view(argv[10]) == "--cancel")
      options.progress = [](const char *, u64, u64) { return false; };
    else if (std::string_view(argv[10]) == "--cancel-search")
      options.progress = [](const char * phase, u64, u64) { return std::string_view(phase) != "raw" && std::string_view(phase) != "resources"; };
    else if (std::string_view(argv[10]) == "--auto") {
      options.context = VideoCommon::ShaderProfile::RecordingContext(options.recording);
      options.automatic_sources = true;
      options.compact = true;
    }
    else if (std::string_view(argv[10]) != "--compact")
      throw std::runtime_error("unknown option");
    else
      options.compact = true;
  }
  if (!options.progress)
    options.progress = [](const char *, u64, u64) { return true; };
  const auto r = VideoCommon::ShaderProfile::Export(options);
  std::cout << "{\"programs\":" << r.programs
            << ",\"environments\":" << r.environments
            << ",\"graphics\":" << r.graphics << ",\"compute\":" << r.compute
            << ",\"resolved_hle_aliases\":" << r.resolved_hle_aliases
            << ",\"unresolved\":" << r.unresolved
            << ",\"missing\":" << r.missing << ",\"reused\":" << r.reused
            << ",\"resources\":" << r.searched_resources
            << ",\"bytes\":" << r.bytes << ",\"seconds\":" << r.seconds
            << "}\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
