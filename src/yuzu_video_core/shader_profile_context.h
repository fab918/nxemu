// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yuzu_video_core/shader_profile_hash.h"
#include "yuzu_common/common_types.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace VideoCommon::ShaderProfile {
inline bool ValidContext(std::string_view context) {
    return context.size() == 64 && context.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}
inline std::string ContentPart(u64 size, std::string_view header) {
    return std::to_string(size) + ":" + ResourceSha256(header);
}
inline std::string ContentContext(u64 title, std::string_view base, std::string_view update) {
    if (base.empty()) return {};
    return ResourceSha256("nxshader-content-2:" + std::to_string(title) + ":" + std::string(base) + ":" + std::string(update));
}
inline std::string CurrentContext(const std::filesystem::path& cache) {
    const auto path = cache / "active-content.txt";
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) > 64) return {};
    std::ifstream in(path, std::ios::binary);
    const std::string context((std::istreambuf_iterator<char>(in)), {});
    return ValidContext(context) ? context : std::string{};
}
inline std::filesystem::path RecordingPath(const std::filesystem::path& cache, std::string_view context) {
    return cache / (ValidContext(context) ? "guest-recipes-" + std::string(context) + ".nxr" : "guest-recipes.nxr");
}
inline std::string RecordingContext(const std::filesystem::path& recording) {
    const auto name = recording.filename().string();
    if (name.starts_with("guest-recipes-") && name.ends_with(".nxr") && name.size() == 14+64+4) {
        const auto context = name.substr(14,64);
        if (ValidContext(context)) return context;
    }
    return {};
}
inline void SelectContext(const std::filesystem::path& cache, std::string_view context) {
    const auto path = cache / "active-content.txt";
    auto temporary = path; temporary += ".partial";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (ValidContext(context)) out.write(context.data(), context.size());
    out.flush(); out.close();
    if (!out) throw std::runtime_error("Cannot save shader capture context");
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish shader capture context");
#else
    std::filesystem::rename(temporary,path);
#endif
}
} // namespace VideoCommon::ShaderProfile
