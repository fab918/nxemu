// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include "yuzu_video_core/shader_profile.h"
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
    if(argc!=6)throw std::runtime_error("usage: nxshader-reader profile game-root title-id output.nxc target-feature-bits");
    const auto bits=std::stoul(argv[5]);if(bits>127)throw std::runtime_error("invalid features");
    const Vulkan::DynamicFeatures features{bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8),bool(bits&16),bool(bits&32),bool(bits&64)};
    if(std::filesystem::exists(argv[4]))throw std::runtime_error("output already exists");
    namespace Timing=VideoCommon::ShaderProfile::Timing;
    Timing::Metrics measurements;
    const char* option=std::getenv("NXEMU_NXSHADER_TIMING");
    auto* timings=option && *option && std::string_view(option)!="0" ? &measurements : nullptr;
    const auto recipes=VideoCommon::ShaderProfile::Read(argv[1],argv[2],std::stoull(argv[3],nullptr,16),nullptr,timings);
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
             <<",\"unique_local_records\":"<<read.size()<<",\"cache_bytes\":"<<std::filesystem::file_size(argv[4]);
    if(timings) std::cout<<",\"timings\":"<<timings->Json();
    std::cout<<"}\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
