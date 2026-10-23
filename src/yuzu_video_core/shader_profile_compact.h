// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yuzu_video_core/shader_profile.h"
class JsonValue;
namespace VideoCommon::ShaderProfile
{
// NXSHBIN3: explicit bounded little-endian tables inside the existing Zstd envelope.
// This is distinct from the Mac experiment's NXCPROT1 / NXCPZ001 framing.
std::string PackCompact(const JsonValue & profile);
std::vector<Recipe> ReadCompact(std::string_view payload, const std::filesystem::path & root,
                                u64 title_id, size_t * unresolved_count, Timing::Metrics * timings,
                                const ResourceProgress & progress = {});
} // namespace VideoCommon::ShaderProfile
