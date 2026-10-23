// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yuzu_shader_recompiler/shader_info.h"
#include <cstddef>

namespace VideoCommon::ShaderProfile
{
inline bool ValidEnvironmentMapValue(size_t map, u32 value)
{
    if (map == 0) return value < Shader::NUM_TEXTURE_TYPES;
    if (map == 1) return value <= static_cast<u32>(Shader::TexturePixelFormat::D32_FLOAT_S8_UINT);
    if (map == 3) return value <= static_cast<u32>(Shader::ReplaceConstant::DrawID);
    return true;
}
} // namespace VideoCommon::ShaderProfile
