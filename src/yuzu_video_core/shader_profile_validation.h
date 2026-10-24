// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yuzu_shader_recompiler/shader_info.h"
#include <cstddef>
#include <string_view>
#include "yuzu_common/cityhash.h"
#include "yuzu_video_core/shader_profile_resources.h"

namespace VideoCommon::ShaderProfile
{
inline bool ValidResourceName(std::string_view name) {
    return !name.empty() && name != "." && name != ".." &&
        name.find_first_of("/\\:\0", 0, 4) == std::string_view::npos;
}
inline bool MatchesProgramHash(std::string_view code, u64 hash) {
    if (!hash) return false;
    if (Common::CityHash64(code.data(), code.size()) == hash) return true;
    const auto terminal = [&](size_t offset) {
        u64 word{};
        for (size_t i=0;i<8;++i) word |= u64(static_cast<unsigned char>(code[offset+i])) << (8*i);
        return word == 0xE2400FFFFF87000FULL || word == 0xE2400FFFFF07000FULL;
    };
    // Analyze hashes up to the terminal instruction; CFG hashes the full span.
    if (code.size() >= 8 && terminal(code.size()-8) &&
        Common::CityHash64(code.data(), code.size()-8) == hash) return true;
    return false;
}
inline bool ValidEnvironmentMapValue(size_t map, u32 value)
{
    if (map == 0) return value < Shader::NUM_TEXTURE_TYPES;
    if (map == 1) return value <= static_cast<u32>(Shader::TexturePixelFormat::D32_FLOAT_S8_UINT);
    if (map == 3) return value <= static_cast<u32>(Shader::ReplaceConstant::DrawID);
    return true;
}
} // namespace VideoCommon::ShaderProfile
