// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/shader_profile.h"
#include "yuzu_video_core/shader_profile_export.h"
#include "yuzu_video_core/shader_profile_compact.h"
#include "yuzu_video_core/shader_profile_resources.h"
#include "yuzu_video_core/shader_profile_codec.h"
#include "yuzu_video_core/shader_profile_metrics.h"
#include "yuzu_video_core/shader_profile_hash.h"
#include "yuzu_video_core/shader_profile_validation.h"
#include "common/json.h"
#include "common/sha256.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <cstring>

namespace VideoCommon::ShaderProfile {
using Timing::Phase;
using Timing::Counter;
using Timing::Measure;
namespace {
constexpr size_t MaxResource = 512*1024*1024;
void Check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
std::string Bytes(const std::filesystem::path& path, u64 offset, u64 size, size_t limit=MaxResource,
                  Timing::Metrics* timings=nullptr) {
    Timing::Timer timer(timings,Phase::ResourceIO);
    Check(size <= limit,"Profile resource too large");
    const auto total=std::filesystem::file_size(path);
    Check(offset <= total && size <= total-offset,"Profile resource bounds");
    std::ifstream file(path,std::ios::binary); file.seekg(static_cast<std::streamoff>(offset));
    std::string out(static_cast<size_t>(size),'\0');
    file.read(out.data(),static_cast<std::streamsize>(size)); Check(bool(file),"Cannot read profile resource");
    if(timings) timings->Add(Counter::ResourceReadBytes,size);
    return out;
}
} // namespace

std::vector<Recipe> Read(const std::filesystem::path& path,const std::filesystem::path& root,u64 title_id,
                         size_t* unresolved_count, Timing::Metrics* timings,const ResourceProgress& progress, ImportReport* report, std::string_view target_context, const std::set<std::string>* existing_recipes, const std::map<std::string,std::shared_ptr<const std::string>>* existing_programs) {
    Timing::Timer total(timings,Phase::ReaderTotal);
    const auto packed=Measure(timings,Phase::ProfileIO,[&] {
        return Bytes(path,0,std::filesystem::file_size(path),33*1024*1024);
    });
    Check(packed.starts_with("NXSHADR2"),"Expected a compressed NXShader profile");
    const auto payload=Measure(timings,Phase::ProfileDecompression,[&] { return Unpack(packed); });
    if(timings) timings->Add(Counter::ProfileBytes,packed.size());
    return ReadCompact(payload, root, title_id, unresolved_count, timings, progress, report, target_context, existing_recipes, existing_programs);
}
} // namespace VideoCommon::ShaderProfile
