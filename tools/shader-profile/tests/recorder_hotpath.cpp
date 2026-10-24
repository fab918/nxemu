// SPDX-License-Identifier: GPL-2.0-or-later
#include "recorder_fixture.h"
#include <bit>
#include <cstdlib>
#include <new>

thread_local bool count_allocations{};
thread_local size_t allocations{};
void* operator new(size_t n) {
    if (count_allocations) ++allocations;
    if (auto* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}

struct Step {
    u32 point{},attribute{};
    GraphicsPipelineCacheKey key;
    Journal::Entry entry;
};
#ifdef _MSC_VER
__declspec(noinline)
#else
__attribute__((noinline))
#endif
u64 Replay(GuestPipelineRecorder* recorder, bool tracking, bool mostly_unchanged, bool untracked_writes, Maxwell& regs,
           Tegra::Engines::Maxwell3D::DirtyState& dirty, std::span<const Step> steps,
           size_t count, size_t variants, const DynamicFeatures& features) {
    constexpr size_t point_method=offsetof(Maxwell,point_size)/4;
    constexpr size_t attribute_method=offsetof(Maxwell,vertex_attrib_format)/4;
    for(size_t draw=0;draw<count;++draw){
        const auto& step=steps[(mostly_unchanged?draw/1000:draw)%variants];
        if(regs.reg_array[point_method]!=step.point){regs.reg_array[point_method]=step.point;if(tracking)dirty.MarkShaderProfileRegister(point_method);}
        if(regs.reg_array[attribute_method]!=step.attribute){regs.reg_array[attribute_method]=step.attribute;if(tracking)dirty.MarkShaderProfileRegister(attribute_method);}
        if(untracked_writes){regs.reg_array[Maxwell::NUM_REGS-1]=u32(draw);if(tracking)dirty.MarkShaderProfileRegister(Maxwell::NUM_REGS-1);}
        if(recorder)recorder->ObserveState(dirty,regs,Maxwell::PrimitiveTopology::Triangles,
            Tegra::Engines::Maxwell3D::EngineHint::None,&regs,step.key,features);
    }
    return regs.reg_array[point_method]+u64(regs.reg_array[attribute_method]);
}
int main(int argc,char** argv)try {
    Check(argc==2 || (argc==3 && std::string_view(argv[2])=="wide"));
    const size_t known_variants=argc==3?512:64;
    const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
#ifdef _WIN32
    _putenv_s("NXEMU_RECIPE_RECORD_TIMING","");_putenv_s("NXEMU_RECIPE_RECORD_REPORT","");
#endif
    const DynamicFeatures features{true,true,true,true,true,true,false};
    const auto topology=Maxwell::PrimitiveTopology::Triangles;
    const auto hint=Tegra::Engines::Maxwell3D::EngineHint::None;
    std::cout<<"{\"scope\":\"ObserveState_plus_enabled_register_tracking_vs_no_tracking_CPU_only\",\"per_draw_clocks\":false,\"scenarios\":[";
    u64 checksum{};
    bool comma{};
    for(const std::string scenario:{"unchanged","mostly_unchanged","known_transitions","prefix_only","untracked_writes","discovery"}){
        auto initial=RecorderRegisters();
        std::vector<Step> steps(512);
        const bool stable=scenario=="prefix_only" || scenario=="untracked_writes";
        for(size_t i=0;i<steps.size();++i){
            initial->point_size=stable?1.0f:float(known_variants==512?i%32+1:i+1);
            initial->vertex_attrib_format[0].offset.Assign(stable?0:u32(i/16%4*16));
            const auto guest=GuestPipelineState::Capture(*initial,topology,hint);
            auto& step=steps[i];step.point=std::bit_cast<u32>(initial->point_size);
            step.attribute=std::bit_cast<u32>(initial->vertex_attrib_format[0]);
            step.key.state=guest.Project(features);step.key.prefix[0]=scenario=="untracked_writes"?0:scenario=="prefix_only"?u32(i):u32(known_variants==512?i/8:i/8%3);
            step.entry={};step.entry[0]=63;
            const auto words=LegacyWords(guest);std::copy(words.begin(),words.end(),step.entry.begin()+1);
            std::copy(step.key.prefix.begin(),step.key.prefix.end(),step.entry.begin()+279);
        }
        const size_t count=scenario=="discovery"?512:2000000;
        for(unsigned mode:{0U,1U,2U}){
            const bool enabled=mode==2,tracking=mode!=0;
            std::vector<double> times,cpu_cycles,allocs,drains;
            for(size_t trial=0;trial<5;++trial){
                auto regs=RecorderRegisters();auto dirty=std::make_unique<Tegra::Engines::Maxwell3D::DirtyState>();
                Common::ThreadWorker worker(1,"hotpath bench");
                const auto path=root/(scenario+std::to_string(mode)+"-"+std::to_string(trial)+".nxr");
                Check(!std::filesystem::exists(path));
                if(enabled && scenario!="discovery"){
                    const size_t known=scenario=="unchanged" || scenario=="untracked_writes"?1:known_variants;
                    for(size_t i=0;i<known;++i)Journal::Append(path,steps[i].entry);
                }
                auto recorder=enabled?std::make_unique<GuestPipelineRecorder>(path,worker):nullptr;
                constexpr size_t point_method=offsetof(Maxwell,point_size)/4;
                constexpr size_t attribute_method=offsetof(Maxwell,vertex_attrib_format)/4;
                if(tracking)GuestPipelineState::Track(*dirty);
                Check(dirty->shader_profile_registers[Maxwell::NUM_REGS-1]==0);
                // Prime outside timing. Mode 0 omits both the recorder and marks;
                // mode 1 measures marks alone; mode 2 measures their total cost.
                if(recorder && scenario!="discovery"){
                    regs->reg_array[point_method]=steps[0].point;
                    regs->reg_array[attribute_method]=steps[0].attribute;
                    recorder->ObserveState(*dirty,*regs,topology,hint,regs.get(),steps[0].key,features);
                    worker.WaitForRequests();
                }
                allocations=0;count_allocations=true;
                const auto cpu_before=Runtime::ThreadCpu();const auto start=std::chrono::steady_clock::now();
                checksum+=Replay(recorder.get(),tracking,scenario=="mostly_unchanged",scenario=="untracked_writes",*regs,*dirty,steps,count,scenario=="unchanged" || scenario=="untracked_writes"?1:scenario=="discovery"?512:known_variants,features);
                const auto finish=std::chrono::steady_clock::now();const auto cpu_after=Runtime::ThreadCpu();
                count_allocations=false;
                const auto draw_allocations=allocations;
                worker.WaitForRequests();const auto drained=std::chrono::steady_clock::now();
                recorder.reset();
                if(enabled){
                    Check(!std::filesystem::exists(path.string()+".failed"));
                    const auto actual=Journal::Read(path);std::set<Journal::Entry> expected;
                    for(size_t i=0;i<(scenario=="unchanged" || scenario=="untracked_writes"?1:scenario=="discovery"?512:known_variants);++i)expected.insert(steps[i].entry);
                    Check(actual.entries==expected);
                }
                if(trial){
                    times.push_back(std::chrono::duration<double,std::nano>(finish-start).count()/count);
                    allocs.push_back(double(draw_allocations));
                    cpu_cycles.push_back(cpu_after.cycles_available && cpu_before.cycles_available?double(cpu_after.cycles-cpu_before.cycles)/count:0);
                    drains.push_back(std::chrono::duration<double,std::milli>(drained-finish).count());
                }
            }
            const auto median=[](auto values){std::sort(values.begin(),values.end());return (values[1]+values[2])/2;};
            if(comma)std::cout<<',';comma=true;
            std::cout<<"{\"workload\":\""<<scenario<<"\",\"tracking\":"<<(tracking?"true":"false")<<",\"recorder\":"<<(enabled?"true":"false")<<",\"observations\":"<<count
                <<",\"known_working_set\":"<<known_variants<<",\"median_ns_per_observation\":"<<median(times)<<",\"median_cycles_per_observation\":"<<median(cpu_cycles)
                <<",\"median_main_thread_allocations\":"<<median(allocs)<<",\"median_worker_drain_ms\":"<<median(drains)<<'}';
        }
    }
    std::cout<<"],\"recipe_sets_verified\":true,\"checksum\":"<<checksum<<"}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
