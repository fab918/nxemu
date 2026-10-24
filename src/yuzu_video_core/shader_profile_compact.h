// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yuzu_video_core/shader_profile.h"
class JsonValue;
namespace VideoCommon::ShaderProfile
{
// NXSHBIN4: explicit bounded little-endian tables inside the existing Zstd envelope.
// One strict shared format; prototype payloads are not runtime inputs.
ProfileInspection InspectCompact(std::string_view payload, u64 title_id);
std::string PackCompact(const JsonValue & profile);
std::vector<Recipe> ReadCompact(std::string_view payload, const std::filesystem::path & root,
                                u64 title_id, size_t * unresolved_count, Timing::Metrics * timings,
                                const ResourceProgress & progress = {}, ImportReport* report = nullptr, std::string_view target_context = {}, const std::set<std::string>* existing_recipes = nullptr,
                         const std::map<std::string,std::shared_ptr<const std::string>>* existing_programs = nullptr);
} // namespace VideoCommon::ShaderProfile
