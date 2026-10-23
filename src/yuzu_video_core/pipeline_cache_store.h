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
std::vector<Record> Read(const std::filesystem::path& path, u32 shader_abi);
void Append(const std::filesystem::path& path, u32 shader_abi, const Record& record);
} // namespace VideoCommon::CacheStore
