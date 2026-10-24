// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <string_view>
namespace VideoCommon::ShaderProfile {
// Bounded compression envelope; the runtime separately requires NXSHBIN4.
// Unpack also accepts raw bytes for offline diagnostic tools.
std::string Pack(std::string_view json, int level = 3);
std::string Unpack(std::string_view bytes);
} // namespace VideoCommon::ShaderProfile
