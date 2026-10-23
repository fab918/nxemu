// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
#include "yuzu_common/common_types.h"

namespace VideoCommon::ShaderProfile {
namespace Timing { struct Metrics; }
struct Recipe {
    bool compute{};
    std::vector<u32> guest_words;
    std::array<u64,6> shader_hashes{};
    std::array<std::array<u32,18>,5> cbuf_sizes{};
    std::array<u32,2> texture_pool{};
    u32 shared_memory_size{};
    std::array<u32,3> workgroup_size{};
    std::array<u32,8> compute_cbuf_sizes{};
    u32 texture_table_size{};
    std::vector<std::string> environments;
};
// Only metadata is accepted from the profile; all instruction bytes come from game_root.
std::vector<Recipe> Read(const std::filesystem::path& path,
                         const std::filesystem::path& game_root, u64 title_id,
                         size_t* unresolved_count = nullptr, Timing::Metrics* timings = nullptr);
} // namespace VideoCommon::ShaderProfile
