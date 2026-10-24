// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
#include <functional>
#include <set>
#include <map>
#include "yuzu_common/common_types.h"
#include "yuzu_video_core/shared_shader_bytes.h"

namespace VideoCommon::ShaderProfile {
namespace Timing { struct Metrics; }
using ResourceProgress = std::function<bool(size_t,size_t)>;
struct Recipe {
    bool compute{};
    std::string source_context, identity;
    std::vector<u32> guest_words;
    std::array<u64,6> shader_hashes{};
    std::array<std::array<u32,18>,5> cbuf_sizes{};
    std::array<u32,2> texture_pool{};
    u32 shared_memory_size{};
    std::array<u32,3> workgroup_size{};
    std::array<u32,8> compute_cbuf_sizes{};
    u32 texture_table_size{};
    std::vector<SharedShaderBytes> environments;
};
struct ImportReport {
    size_t accepted{}, reused{}, skipped{}, missing_resources{}, incompatible_resources{}, incompatible_contexts{};
    std::vector<std::string> reused_ids;
};
struct ProfileInspection { size_t graphics{},compute{},unresolved{},bytes{}; };
// Structure/title validation only. Reads no game resources and creates no pipelines.
ProfileInspection Inspect(const std::filesystem::path& profile,u64 title_id);
// Only metadata is accepted from the profile; all instruction bytes come from game_root.
std::vector<Recipe> Read(const std::filesystem::path& path,
                         const std::filesystem::path& game_root, u64 title_id,
                         size_t* unresolved_count = nullptr, Timing::Metrics* timings = nullptr,
                         const ResourceProgress& progress = {}, ImportReport* report = nullptr, std::string_view target_context = {}, const std::set<std::string>* existing_recipes = nullptr,
                         const std::map<std::string,std::shared_ptr<const std::string>>* existing_programs = nullptr);
} // namespace VideoCommon::ShaderProfile
