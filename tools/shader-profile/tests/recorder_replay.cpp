#include "recorder_fixture.h"
int main(int argc,char** argv)try {
    Check(argc==2);
    for(unsigned bits:{0U,63U}){
        auto path=std::filesystem::path(argv[1]);path+=std::to_string(bits)+".nxr";
        Check(!std::filesystem::exists(path));
        auto regs=RecorderRegisters();
        auto dirty=std::make_unique<Tegra::Engines::Maxwell3D::DirtyState>();
        const DynamicFeatures features{bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8),bool(bits&16),bool(bits&32),bool(bits&64)};
        Common::ThreadWorker writer(1,"recorder test");
        std::set<Journal::Entry> expected;
        const auto replay=[&](GuestPipelineRecorder& recorder){
            for(size_t i=0;i<2400;++i){
                // Repeated transitions, a hidden vertex layout, prefix-only
                // changes and channel switches must all retain their recipes.
                const auto old_attribute=regs->vertex_attrib_format[0].offset.Value();
                const auto old_point=regs->point_size;
                regs->vertex_attrib_format[0].offset.Assign((i/3)%4*16);
                regs->point_size=float((i/7)%3+1);
                if(i%2)dirty->InvalidateShaderProfile();
                const auto hint=i%5?Tegra::Engines::Maxwell3D::EngineHint::None:Tegra::Engines::Maxwell3D::EngineHint::OnHLEMacro;
                const auto topology=Maxwell::PrimitiveTopology::Triangles;
                const auto guest=GuestPipelineState::Capture(*regs,topology,hint);
                GraphicsPipelineCacheKey key;key.prefix[0]=u32((i/11)%3);key.state=guest.Project(features);
                Journal::Entry entry{};entry[0]=bits;const auto words=LegacyWords(guest);
                std::copy(words.begin(),words.end(),entry.begin()+1);std::copy(key.prefix.begin(),key.prefix.end(),entry.begin()+279);
                expected.insert(entry);
                // A real changed register updates the revision, even on draws
                // whose resulting host key hides the changed attribute layout.
                if(regs->point_size!=old_point)dirty->MarkShaderProfileRegister(offsetof(Maxwell,point_size)/4);
                if(regs->vertex_attrib_format[0].offset.Value()!=old_attribute)
                    dirty->MarkShaderProfileRegister(offsetof(Maxwell,vertex_attrib_format)/4);
                const void* channel=i%13?static_cast<void*>(regs.get()):static_cast<void*>(dirty.get());
                recorder.ObserveState(*dirty,*regs,topology,hint,channel,key,features);
                if(i%60==0)recorder.Flush();
            }
            recorder.Flush();
            writer.WaitForRequests();
        };
        {GuestPipelineRecorder recorder(path,writer);replay(recorder);}
        Check(!std::filesystem::exists(path.string()+".failed"));
        Check(Journal::Read(path).entries==expected);
        const auto before=std::filesystem::file_size(path);
        {GuestPipelineRecorder recorder(path,writer);replay(recorder);}
        Check(std::filesystem::file_size(path)==before && Journal::Read(path).entries==expected);
        std::filesystem::remove(path);
    }
    // A partial batch is drained on destruction; an asynchronous write failure
    // must leave an incomplete marker rather than silently authorizing export.
    auto path=std::filesystem::path(argv[1]);path+="flush-failure.nxr";
    Check(!std::filesystem::exists(path));
    auto regs=RecorderRegisters();
    auto dirty=std::make_unique<Tegra::Engines::Maxwell3D::DirtyState>();
    const DynamicFeatures features{};
    const auto topology=Maxwell::PrimitiveTopology::Triangles;
    const auto hint=Tegra::Engines::Maxwell3D::EngineHint::None;
    GraphicsPipelineCacheKey key;key.state=GuestPipelineState::Capture(*regs,topology,hint).Project(features);
    Common::ThreadWorker writer(1,"flush test");
    {
        GuestPipelineRecorder recorder(path,writer);
        recorder.ObserveState(*dirty,*regs,topology,hint,regs.get(),key,features);
    }
    Check(Journal::Read(path).entries.size()==1);
    std::filesystem::remove(path);
    {
        GuestPipelineRecorder recorder(path,writer);
        std::filesystem::remove(path);
        std::filesystem::create_directory(path); // Deterministic write failure, no permissions assumptions.
        recorder.ObserveState(*dirty,*regs,topology,hint,regs.get(),key,features);
        recorder.Flush();writer.WaitForRequests();
        Check(std::filesystem::exists(path.string()+".failed"));
    }
    std::filesystem::remove(path);
    std::filesystem::remove(path.string()+".failed");
    std::cout<<"PASS: recorder matches legacy recipe sets and reuses existing journals\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
