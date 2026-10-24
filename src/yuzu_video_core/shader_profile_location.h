// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
namespace VideoCommon::ShaderProfile {
inline std::string ProfileFilename(uint64_t title) {
    std::ostringstream name;name<<std::hex<<std::setfill('0')<<std::setw(16)<<title<<".nxshader";return name.str();
}
inline std::filesystem::path FindGameProfile(const std::filesystem::path& game,uint64_t title) {
    if(game.empty() || !title)return {};
    const auto candidate=game.parent_path()/ProfileFilename(title);
    std::error_code error;
    return std::filesystem::is_regular_file(candidate,error) ? candidate : std::filesystem::path{};
}
}
