// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include "yuzu_video_core/shader_profile.h"
#include "yuzu_video_core/shader_profile_compact.h"
#include "yuzu_video_core/shader_profile_codec.h"
#include "yuzu_video_core/shader_profile_export.h"
#include "yuzu_video_core/shader_profile_hash.h"
#include <fstream>
#include "yuzu_video_core/shader_profile_metrics.h"
#include <cstdlib>
#include "yuzu_video_core/pipeline_cache_store.h"
#include "yuzu_video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "video_settings.h"
#include "yuzu_common/logging/log.h"
VideoSettings videoSettings{};
namespace Common::Log {
LogLevel g_classLevel[static_cast<uint8_t>(LogClass::Count)]{};
void FmtLogMessageImpl(LogClass, LogLevel, const char*, unsigned int, const char*, const char*, const fmt::format_args&) {}
}
void assert_fail_impl() {throw std::runtime_error("renderer assertion");}
[[noreturn]] void unreachable_impl() {throw std::runtime_error("renderer unreachable");}
void put(std::string& out,u64 value,size_t size) {for(size_t i=0;i<size;++i)out.push_back(static_cast<char>(value>>(8*i)));}
int main(int argc,char** argv) try {
    if(argc<6)throw std::runtime_error("usage: nxshader-reader profile game-root title-id output.nxc target-feature-bits");
    const auto bits=std::stoul(argv[5]);if(bits>127)throw std::runtime_error("invalid features");
    const Vulkan::DynamicFeatures features{bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8),bool(bits&16),bool(bits&32),bool(bits&64)};
    // Diagnostic metadata is compiled by this offline tool. Runtime Read/Inspect
    // accept only the single compressed NXSHBIN4 wire format.
    std::ifstream source(argv[1],std::ios::binary);
    if(std::filesystem::file_size(argv[1])>33*1024*1024)throw std::runtime_error("profile size limit");
    const std::string bytes((std::istreambuf_iterator<char>(source)),{});
    const bool runtime=std::any_of(argv+6,argv+argc,[](const char* option){return std::string_view(option)=="--runtime";});
    const bool metadata=!runtime && bytes.starts_with("{");
    const auto payload=metadata ? VideoCommon::ShaderProfile::Unpack(VideoCommon::ShaderProfile::PackCompact(VideoCommon::ShaderProfile::ParseMetadata(bytes))) : std::string{};
    if(argc==7 && std::string_view(argv[6])=="--inspect") {
        const auto m=metadata ? VideoCommon::ShaderProfile::InspectCompact(payload,std::stoull(argv[3],nullptr,16)) : VideoCommon::ShaderProfile::Inspect(argv[1],std::stoull(argv[3],nullptr,16));
        std::cout<<"{\"graphics\":"<<m.graphics<<",\"compute\":"<<m.compute<<",\"unresolved\":"<<m.unresolved<<"}\n";
        return 0;
    }
    if(std::filesystem::exists(argv[4]))throw std::runtime_error("output already exists");
    namespace Timing=VideoCommon::ShaderProfile::Timing;
    Timing::Metrics measurements;
    const char* option=std::getenv("NXEMU_NXSHADER_TIMING");
    auto* timings=option && *option && std::string_view(option)!="0" ? &measurements : nullptr;
    bool cancel{}; std::string_view context;
    std::set<std::string> existing;
    std::map<std::string,std::shared_ptr<const std::string>> programs;
    for(int i=6;i<argc;++i) {
        const std::string_view option=argv[i];
        if(option=="--cancel")cancel=true;
        else if(option=="--runtime"){}
        else if(option.starts_with("--program-cache=")) {
            for(const auto& record:VideoCommon::CacheStore::Read(std::string(option.substr(16)),20))
                for(const auto& env:record.environments)if(env.ProgramIdentity())
                    programs.emplace(VideoCommon::ShaderProfile::ResourceSha256(env.ProgramBytes()),env.ProgramStorage());
        }
        else if(option.starts_with("--context="))context=option.substr(10);
        else if(option.starts_with("--reuse=")) {
            std::ifstream file(std::string(option.substr(8)),std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(file)),{});
            const auto ids=VideoCommon::ShaderProfile::ParseMetadata(text);
            if(!ids.isArray())throw std::runtime_error("invalid recipe reuse fixture");
            for(const auto& id:ids)existing.insert(id.asString());
        } else throw std::runtime_error("invalid test option");
    }
    VideoCommon::ShaderProfile::ImportReport report;
    const VideoCommon::ShaderProfile::ResourceProgress progress=[&](size_t,size_t){return !cancel;};
    const auto recipes=metadata ? VideoCommon::ShaderProfile::ReadCompact(payload,argv[2],std::stoull(argv[3],nullptr,16),nullptr,timings,progress,&report,context,&existing,&programs) :
        VideoCommon::ShaderProfile::Read(argv[1],argv[2],std::stoull(argv[3],nullptr,16),nullptr,timings,progress,&report,context,&existing,&programs);
    size_t graphics{},compute{};
    for(const auto& r:recipes) {
        Timing::Timer projection_timer(timings,Timing::Phase::GuestProjection);
        VideoCommon::CacheStore::Record record;record.environments=r.environments;
        if(r.compute) {
            ++compute;put(record.key,r.shader_hashes[0],8);put(record.key,r.shared_memory_size,4);
            for(auto x:r.workgroup_size)put(record.key,x,4);
            for(auto x:r.compute_cbuf_sizes)put(record.key,x,4);
            put(record.key,r.texture_table_size,4);put(record.key,0,4);
        } else {
            ++graphics;const auto guest=Vulkan::GuestPipelineState::FromWords(r.guest_words);
            if(!guest)throw std::runtime_error("invalid canonical guest recipe");
            const auto state=guest->Project(features);
            for(auto x:r.shader_hashes)put(record.key,x,8);
            for(auto stage:r.cbuf_sizes)for(auto x:stage)put(record.key,x,4);
            for(auto x:r.texture_pool)put(record.key,x,4);
            record.key.append(reinterpret_cast<const char*>(&state),sizeof(state));
        }
        projection_timer.Stop();
        Timing::Measure(timings,Timing::Phase::CacheAppend,[&] { VideoCommon::CacheStore::Append(argv[4],20,record); });
    }
    const auto read=VideoCommon::CacheStore::Read(argv[4],20);
    std::cout<<"{\"recipes\":"<<recipes.size()<<",\"graphics\":"<<graphics<<",\"compute\":"<<compute
             <<",\"reused\":"<<report.reused<<",\"skipped\":"<<report.skipped<<",\"incompatible_contexts\":"<<report.incompatible_contexts<<",\"missing_resources\":"<<report.missing_resources<<",\"incompatible_resources\":"<<report.incompatible_resources
             <<",\"unique_local_records\":"<<read.size()<<",\"cache_bytes\":"<<(std::filesystem::exists(argv[4])?std::filesystem::file_size(argv[4]):0);
    std::cout<<",\"recipe_ids\":[";
    bool comma{};
    for(const auto& r:recipes){if(comma)std::cout<<',';comma=true;std::cout<<'"'<<r.identity<<'"';}
    for(const auto& id:report.reused_ids){if(comma)std::cout<<',';comma=true;std::cout<<'"'<<id<<'"';}
    std::cout<<']';
    if(timings) std::cout<<",\"timings\":"<<timings->Json();
    std::cout<<"}\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
