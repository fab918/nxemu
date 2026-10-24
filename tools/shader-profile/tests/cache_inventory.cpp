// SPDX-License-Identifier: GPL-2.0-or-later
// Offline diagnostic: inventories hashes and pipeline state, never shader instructions.
#include "yuzu_video_core/pipeline_cache_store.h"
#include "yuzu_video_core/shader_profile_hash.h"
#include "yuzu_video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "yuzu_video_core/renderer_vulkan/cbuf_size_dependencies.h"
#include "yuzu_common/cityhash.h"
#include <cstring>
#include <algorithm>
#include <iostream>
#include <unordered_map>
struct GraphicsKey {
    std::array<u64,6> hashes;
    std::array<std::array<u32,18>,5> sizes;
    std::array<u32,2> textures;
    Vulkan::FixedPipelineState state;
    size_t Size()const{return 416+state.Size();}
};
static_assert(sizeof(GraphicsKey)==1400 && offsetof(GraphicsKey,state)==416);
u64 Get(std::string_view s,size_t p,size_t n){if(p>s.size() || n>s.size()-p)throw std::runtime_error("environment bounds");u64 v{};for(size_t i=0;i<n;++i)v|=u64(static_cast<unsigned char>(s[p+i]))<<(8*i);return v;}
u32 Mask(std::string_view e){u32 m{};const auto code=Get(e,0,8);size_t p=68+code+Get(e,8,8)*8+Get(e,16,8)*8;for(size_t i=0;i<Get(e,24,8);++i,p+=12){auto k=Get(e,p,8);if(k==((u64(UINT32_MAX)<<32)|(UINT32_MAX-1)))m|=1U<<31;if(u32(k)==UINT32_MAX && (k>>32)<32)m|=1U<<(k>>32);}return m;}
std::string Hex(std::string_view s){const char* h="0123456789abcdef";std::string out;for(unsigned char c:s){out+=h[c>>4];out+=h[c&15];}return out;}
std::string SemanticEnvironment(std::string_view e) {
    size_t cursor=68+Get(e,0,8);if(cursor>e.size())throw std::runtime_error("code bounds");
    std::string canonical(e.substr(0,cursor));
    for(size_t map=0;map<4;++map){const size_t width=map<2?8:12;std::vector<std::string> rows;
        for(size_t i=0;i<Get(e,8+map*8,8);++i,cursor+=width){Get(e,cursor,width==8?8:4);if(cursor+width>e.size())throw std::runtime_error("map bounds");rows.emplace_back(e.substr(cursor,width));}
        std::sort(rows.begin(),rows.end());for(const auto& row:rows)canonical+=row;
    }
    canonical+=e.substr(cursor);return VideoCommon::ShaderProfile::ResourceSha256(canonical);
}
int main(int argc,char** argv)try {
    if(argc!=2)throw std::runtime_error("usage: cache-inventory cache.nxc");
    const auto records=VideoCommon::CacheStore::Read(argv[1],20);
    Vulkan::CbufSizeDependencies dependencies;
    for(const auto& r:records)if(r.key.size()==1400){GraphicsKey key;std::memcpy(&key,r.key.data(),sizeof(key));size_t i{};for(auto hash:key.hashes)if(hash)dependencies.Observe(hash,Mask(r.environments.at(i++).String()));}
    std::cout<<"{\"records\":[";bool comma{};
    for(const auto& r:records){if(r.key.size()!=1400)continue;GraphicsKey raw;std::memcpy(&raw,r.key.data(),sizeof(raw));auto key=raw;
        key.state.NormalizeInactiveFields();for(size_t stage=0;stage<5;++stage)Vulkan::CbufSizeDependencies::Normalize(key.sizes[stage],dependencies.Mask(key.hashes[stage+1])|(stage==0?dependencies.Mask(key.hashes[0]):0));
        bool texture{};for(auto hash:key.hashes)texture|=bool(dependencies.Mask(hash)&(1U<<31));if(!texture)key.textures={};
        if(comma)std::cout<<',';comma=true;
        std::cout<<"{\"hash\":\""<<Common::CityHash64(reinterpret_cast<const char*>(&key),key.Size())<<"\",\"raw_hash\":\""<<Common::CityHash64(reinterpret_cast<const char*>(&raw),raw.Size())<<"\",\"key\":\""<<Hex(std::string_view(reinterpret_cast<const char*>(&key),key.Size()))<<"\",\"raw_key\":\""<<Hex(r.key)<<"\",\"programs\":[";
        size_t env{};bool pc{};for(auto hash:key.hashes)if(hash){if(pc)std::cout<<',';pc=true;const auto e=r.environments.at(env++).String();const auto length=Get(e,0,8);const auto code=std::string_view(e).substr(68,length);
            std::cout<<"{\"hash\":\""<<hash<<"\",\"code_sha256\":\""<<VideoCommon::ShaderProfile::ResourceSha256(code)<<"\",\"environment_sha256\":\""<<VideoCommon::ShaderProfile::ResourceSha256(e)<<"\",\"size\":"<<length<<",\"code_trim8_sha256\":\""<<VideoCommon::ShaderProfile::ResourceSha256(code.substr(0,code.size()-8))<<"\",\"body_sha256\":\""<<VideoCommon::ShaderProfile::ResourceSha256(code.substr(80))<<"\",\"replacement_count\":"<<Get(e,32,8)<<",\"semantic_environment\":\""<<SemanticEnvironment(e)<<"\",\"mask\":"<<Mask(e)<<'}';}
        std::cout<<"]}";
    }std::cout<<"]}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
