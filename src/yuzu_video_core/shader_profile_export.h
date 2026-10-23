// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/json.h"
#include "yuzu_common/common_types.h"
#include <filesystem>
#include <functional>
#include <string>

namespace VideoCommon::ShaderProfile
{
// All callbacks execute on the caller's offline worker, never on a draw thread.
using Progress = std::function<bool(const char * phase, u64 completed, u64 total)>;
struct ExportOptions
{
    std::filesystem::path cache, recording, game_root, output, index;
    std::string base, update, label;
    u64 title_id{};
    bool allow_partial{}, compact{};
    Progress progress;
};
struct ExportReport
{
    u64 programs{}, environments{}, graphics{}, compute{}, unresolved{}, missing{};
    u64 reused{}, searched_resources{}, decoded_bytes{}, bytes{};
    double seconds{};
};
// Throws on cancellation/error; publication is atomic and never replaces an output.
ExportReport Export(const ExportOptions & options);
// Stable ASCII, sorted-key JSON used only for v2 compatibility and local metadata.
std::string Canonical(const JsonValue & value);
JsonValue ParseMetadata(std::string_view bytes, size_t limit = 32 * 1024 * 1024);
void Publish(const std::filesystem::path & output, std::string_view bytes);
} // namespace VideoCommon::ShaderProfile
