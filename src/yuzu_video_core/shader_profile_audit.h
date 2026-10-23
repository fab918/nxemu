// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yuzu_common/common_types.h"
#include "yuzu_shader_recompiler/shader_info.h"
#include <utility>
#include "yuzu_video_core/shader_profile_hash.h"
#include <span>
#include <vector>

namespace VideoCommon::ShaderProfile::Audit
{
// Enabled only by an explicit offline startup audit. Reports contain hashes,
// never SPIR-V or guest instructions. Normal draw execution does not use this.
struct Capture {
    std::vector<std::string> spirv;
    std::vector<std::pair<size_t, Shader::Info>> interfaces;
    bool interfaces_captured{};
    bool has_vtg{};
    bool rasterizes_lines{};
    bool operator==(const Capture&) const = default;
};
inline thread_local Capture* current{};
inline void Record(std::span<const u32> code)
{
    if (current) current->spirv.push_back(ResourceSha256(std::string_view(
        reinterpret_cast<const char *>(code.data()), code.size_bytes())));
}
class Context
{
public:
    explicit Context(Capture & hashes) :
        previous(current)
    {
        current = &hashes;
    }
    ~Context()
    {
        current = previous;
    }
    Context(const Context &) = delete;

private:
    Capture* previous;
};
} // namespace VideoCommon::ShaderProfile::Audit
