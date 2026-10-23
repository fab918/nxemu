// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include "yuzu_common/common_types.h"

namespace VideoCommon::CacheStore {
// Local cache only: code, environments and pipeline combinations have separate identities.
// All lengths are explicit little endian; shader ABI is independent of the storage revision.
struct Record {
    std::vector<std::string> environments;
    std::string key;
};
struct ReadLimits {
    size_t index_bytes{512 * 1024 * 1024};
    size_t record_bytes{256 * 1024 * 1024};
};
std::vector<Record> Read(const std::filesystem::path& path, u32 shader_abi,
                          const ReadLimits& limits = {});
void Append(const std::filesystem::path& path, u32 shader_abi, const Record& record);
// Vulkan v20 migration: validates both inputs, preserves every distinct record,
// verifies the resulting store before replacing it, and leaves the legacy file intact.
std::vector<Record> ReadLegacyVulkan(const std::filesystem::path& path, u32 shader_abi);
bool MigrateLegacyVulkan(const std::filesystem::path& legacy,
                         const std::filesystem::path& destination, u32 shader_abi);
} // namespace VideoCommon::CacheStore
