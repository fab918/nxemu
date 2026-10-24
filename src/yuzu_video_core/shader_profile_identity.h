// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "nxemu-module-spec/system_loader.h"
#include "yuzu_common/common_types.h"
#include "yuzu_video_core/shader_profile_hash.h"
#include "yuzu_video_core/shader_profile_context.h"
#include <filesystem>
#include <memory>
#include <vector>

namespace VideoCommon::ShaderProfile
{
template <class T>
struct ReleaseInterface
{
    void operator()(T * p) const
    {
        if (p) p->Release();
    }
};
template <class T>
using InterfacePtr = std::unique_ptr<T, ReleaseInterface<T>>;

// Startup only. Unknown selection is deliberately not cacheable. In particular,
// the current public loader API cannot identify a selected older update among
// several installed versions, nor fingerprint arbitrary layered mods reliably.
inline std::string LoadedContentIdentity(ISystemloader & loader, u64 title,
                                         [[maybe_unused]] const std::filesystem::path & game)
{
    try
    {
        InterfacePtr<IRomInfo> info(loader.LoadedRomInfo());
        if (!info) return {};
        const auto count = info->GetGamePatches(nullptr, 0);
        if (count > 1024) return {};
        std::vector<GamePatchInfo> patches(count);
        if (info->GetGamePatches(patches.data(), count) != count) return {};
        size_t update_count{};
        bool update_enabled{};
        std::string base_part, update_part;
        for (const auto & patch : patches)
        {
            if (patch.type == GamePatchType::Mod && patch.enabled) return {};
            if (patch.type == GamePatchType::Update)
            {
                ++update_count;
                update_enabled = patch.enabled;

            }
        }
        if (update_count > 1) return {};
        if (update_count == 0)
        {
            InterfacePtr<IFileSysNCA> unknown_update(loader.GetContentProviderEntry(title | 0x800, LoaderContentRecordType::Program));
            if (unknown_update) return {};
        }
        const auto append = [&](IFileSysNCA * raw, std::string& part) {
            InterfacePtr<IFileSysNCA> nca(raw);
            if (!nca) return false;
            InterfacePtr<IVirtualFile> file(nca->GetBaseFile());
            if (!file || file->GetSize() < 0xc00) return false;
            std::string header(0xc00, '\0');
            if (file->ReadBytes(reinterpret_cast<uint8_t *>(header.data()), header.size(), 0) != header.size()) return false;
            part = ContentPart(file->GetSize(), header);
            return true;
        };
        if (!append(loader.GetContentProviderEntry(title, LoaderContentRecordType::Program), base_part)) return {};
        if (update_enabled && !append(loader.GetContentProviderEntry(title | 0x800, LoaderContentRecordType::Program), update_part)) return {};
        return ContentContext(title, base_part, update_part);
    }
    catch (...)
    {
        return {};
    }
}
inline std::string ImportIdentity(std::string_view profile_hash, std::string_view content,
                                  u64 title, u32 features, u32 cache_abi)
{
    if (content.empty()) return {};
    return ResourceSha256("nxshader-import-2:" + std::string(profile_hash) + ":" + std::string(content) + ":" +
                          std::to_string(title) + ":" + std::to_string(features) + ":" + std::to_string(cache_abi));
}
} // namespace VideoCommon::ShaderProfile
