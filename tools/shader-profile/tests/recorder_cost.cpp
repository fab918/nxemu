// SPDX-License-Identifier: GPL-2.0-or-later
// CPU-only recording microbenchmark. No game execution or FPS inference.
#include "yuzu_video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "yuzu_video_core/shader_profile_journal.h"
#include "yuzu_common/logging/log.h"
#include "video_settings.h"
#include <chrono>
#include <iostream>
#include <unordered_set>
#include <algorithm>

VideoSettings videoSettings{};
namespace Common::Log {
LogLevel g_classLevel[static_cast<uint8_t>(LogClass::Count)]{};
void FmtLogMessageImpl(LogClass,LogLevel,const char*,unsigned,const char*,const char*,const fmt::format_args&) {}
}
[[noreturn]] void assert_fail_impl(){throw std::runtime_error("renderer assertion");}
[[noreturn]] void unreachable_impl(){throw std::runtime_error("renderer unreachable");}
using namespace Vulkan;
using namespace VideoCommon::ShaderProfile;
#include "legacy_guest_words.inc"

int main(int argc,char** argv)try {
    if(argc!=2)throw std::runtime_error("usage: recorder-cost guest-recipes.nxr");
    const auto corpus=Journal::Read(argv[1]);
    if(corpus.entries.empty() || corpus.incomplete_tail)throw std::runtime_error("incomplete/empty corpus");
    std::vector<Journal::Entry> entries(corpus.entries.begin(),corpus.entries.end());
    std::vector<GuestPipelineState> states;
    for(const auto& entry:entries){
        const auto state=GuestPipelineState::FromWords({entry.data()+1,GuestPipelineState::WordCount});
        if(!state)throw std::runtime_error("invalid corpus guest state");
        const auto legacy=LegacyWords(*state);
        std::array<u32,GuestPipelineState::WordCount> fixed;
        state->WriteWords(fixed);
        if(!std::equal(fixed.begin(),fixed.end(),legacy.begin(),legacy.end()) ||
           !std::equal(fixed.begin(),fixed.end(),entry.begin()+1))throw std::runtime_error("wire mismatch");
        states.push_back(*state);
    }
    constexpr size_t Repeat=100;
    const size_t observations=entries.size()*Repeat;
    size_t checksum{};
    const auto bench=[&](bool optimized){
        std::vector<double> samples;
        for(int trial=0;trial<7;++trial){
            std::set<Journal::Entry> ordered;
            std::unordered_set<Journal::Entry,Journal::EntryHash> hashed;
            hashed.reserve(Journal::MaxEntries);
            const auto start=std::chrono::steady_clock::now();
            for(size_t n=0;n<observations;++n){
                const auto i=n%entries.size();
                auto entry=entries[i];
                if(optimized)states[i].WriteWords(std::span<u32,GuestPipelineState::WordCount>(entry.data()+1,GuestPipelineState::WordCount));
                else {const auto words=LegacyWords(states[i]);std::copy(words.begin(),words.end(),entry.begin()+1);}
                if(optimized){if(!hashed.contains(entry))hashed.insert(entry);}
                else {if(!ordered.contains(entry))ordered.insert(entry);}
            }
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(trial)samples.push_back(ms);
            if(optimized){std::set<Journal::Entry> actual(hashed.begin(),hashed.end());if(actual!=corpus.entries)throw std::runtime_error("dedup lost combination");checksum+=hashed.size();}
            else {if(ordered!=corpus.entries)throw std::runtime_error("legacy dedup mismatch");checksum+=ordered.size();}
        }
        std::sort(samples.begin(),samples.end());return (samples[2]+samples[3])/2;
    };
    const auto legacy_ms=bench(false),optimized_ms=bench(true);
    std::cout<<"{\"scope\":\"serialization_and_exact_dedup_only\",\"recipes\":"<<entries.size()<<",\"observations\":"<<observations
             <<",\"legacy_median_ms\":"<<legacy_ms<<",\"optimized_median_ms\":"<<optimized_ms
             <<",\"wire_and_recipe_set_identical\":true,\"checksum\":"<<checksum<<"}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
