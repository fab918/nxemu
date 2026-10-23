// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <string_view>
namespace VideoCommon::ShaderProfile {
// Encoding envelope only: schema/recipes remain v2 and plain JSON remains readable.
std::string Pack(std::string_view json, int level = 3);
std::string Unpack(std::string_view bytes);
} // namespace VideoCommon::ShaderProfile
