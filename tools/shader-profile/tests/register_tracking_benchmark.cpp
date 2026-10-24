// SPDX-License-Identifier: GPL-2.0-or-later
#include "recorder_fixture.h"
#include <random>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#ifdef _MSC_VER
__declspec(noinline)
#else
__attribute__((noinline))
#endif
u64 ReplayWrites(Maxwell& regs,Tegra::Engines::Maxwell3D::DirtyState& dirty,
                 const std::vector<u16>& methods,bool tracking,size_t count) {
    for(size_t i=0;i<count;++i) {
        const auto method=methods[i%methods.size()];
        const auto value=u32(i+1);
        if(regs.reg_array[method]==value)continue;
        regs.reg_array[method]=value;
        if(tracking)dirty.MarkShaderProfileRegister(method);
        // Prevent hoisting the accumulated mask out of this isolated loop.
        // Real engine writes interleave other dirty-state operations/calls.
#ifdef _MSC_VER
        _ReadWriteBarrier();
#else
        asm volatile("" : "+m"(dirty.shader_profile_changes) : : "memory");
#endif
    }
    return dirty.shader_profile_changes+regs.reg_array[methods.back()];
}
int main()try {
    auto dirty=std::make_unique<Tegra::Engines::Maxwell3D::DirtyState>();
    GuestPipelineState::Track(*dirty);
    std::vector<u16> tracked,untracked;
    for(size_t i=0;i<Maxwell::NUM_REGS;++i)
        (dirty->shader_profile_registers[i]?tracked:untracked).push_back(u16(i));
    Check(!tracked.empty() && !untracked.empty());
    std::mt19937 random(918);u64 checksum{};bool comma{};
    std::cout<<"{\"scope\":\"changed_register_writes_with_or_without_production_marker\",\"scenarios\":[";
    for(unsigned density:{0U,10U,100U}) {
        std::vector<u16> methods(4096);u64 expected{};
        for(auto& method:methods) {
            const auto& source=random()%100<density?tracked:untracked;
            method=source[random()%source.size()];expected|=dirty->shader_profile_registers[method];
        }
        for(bool enabled:{false,true}) {
            std::vector<double> samples;
            for(unsigned trial=0;trial<7;++trial) {
                auto regs=RecorderRegisters();dirty->shader_profile_changes=0;
                constexpr size_t count=2000000;
                const auto start=std::chrono::steady_clock::now();
                checksum+=ReplayWrites(*regs,*dirty,methods,enabled,count);
                const auto finish=std::chrono::steady_clock::now();
                Check(dirty->shader_profile_changes==(enabled?expected:0));
                if(trial)samples.push_back(std::chrono::duration<double,std::nano>(finish-start).count()/count);
            }
            std::sort(samples.begin(),samples.end());
            if(comma)std::cout<<',';comma=true;
            std::cout<<"{\"tracked_percent\":"<<density<<",\"tracking\":"<<(enabled?"true":"false")
                <<",\"writes\":2000000,\"median_ns_per_write\":"<<(samples[2]+samples[3])/2<<'}';
        }
    }
    std::cout<<"],\"all_masks_verified\":true,\"checksum\":"<<checksum<<"}\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
