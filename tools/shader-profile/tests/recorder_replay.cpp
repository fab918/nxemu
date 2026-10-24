// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "yuzu_common/logging/log.h"
#include "video_settings.h"
namespace Vulkan {
struct GraphicsPipelineCacheKey {
    std::array<u32,104> prefix{};
    FixedPipelineState state{};
};
}
#include "yuzu_video_core/renderer_vulkan/guest_pipeline_recorder.h"
#include <iostream>
VideoSettings videoSettings{};
namespace Common { void SetCurrentThreadName(const char*) {} }
namespace Common::Log {
LogLevel g_classLevel[static_cast<uint8_t>(LogClass::Count)]{};
void FmtLogMessageImpl(LogClass,LogLevel,const char*,unsigned,const char*,const char*,const fmt::format_args&) {}
}
[[noreturn]] void assert_fail_impl(){throw std::runtime_error("renderer assertion");}
[[noreturn]] void unreachable_impl(){throw std::runtime_error("renderer unreachable");}
using namespace Vulkan;
using namespace VideoCommon::ShaderProfile;
#include "legacy_guest_words.inc"
void Check(bool v){if(!v)throw std::runtime_error("recorder replay changed recipe coverage");}
int main(int argc,char** argv)try {
    Check(argc==2);
    for(unsigned bits:{0U,63U}){
        auto path=std::filesystem::path(argv[1]);path+=std::to_string(bits)+".nxr";
        Check(!std::filesystem::exists(path));
        auto regs=std::make_unique<Maxwell>();
        auto dirty=std::make_unique<Tegra::Engines::Maxwell3D::DirtyState>();
        regs->gl_front_face=Maxwell::FrontFace::CounterClockWise;regs->gl_cull_face=Maxwell::CullFace::Back;
        regs->polygon_mode_front=Maxwell::PolygonMode::Fill;regs->patch_vertices=3;
        regs->depth_test_func=Maxwell::ComparisonOp::Less_GL;regs->alpha_test_func=Maxwell::ComparisonOp::Always_GL;
        regs->stencil_front_op.func=regs->stencil_back_op.func=Maxwell::ComparisonOp::Always_GL;
        regs->logic_op.op=Maxwell::LogicOp::Op::Copy;regs->rasterize_enable=1;
        regs->blend.color_op=regs->blend.alpha_op=Maxwell::Blend::Equation::Add_GL;
        regs->blend.color_source=regs->blend.alpha_source=Maxwell::Blend::Factor::One_GL;
        regs->blend.color_dest=regs->blend.alpha_dest=Maxwell::Blend::Factor::Zero_GL;
        for(auto& a:regs->vertex_attrib_format){a.constant.Assign(1);a.type.Assign(Maxwell::VertexAttribute::Type::Float);a.size.Assign(Maxwell::VertexAttribute::Size::Size_R32_G32_B32_A32);}
        regs->vertex_attrib_format[0].constant.Assign(0);
        const DynamicFeatures features{bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8),bool(bits&16),bool(bits&32),bool(bits&64)};
        Common::ThreadWorker writer(1,"recorder test");
        std::set<Journal::Entry> expected;
        const auto replay=[&](GuestPipelineRecorder& recorder){
            for(size_t i=0;i<2400;++i){
                // Repeated transitions, a hidden vertex layout, prefix-only
                // changes and channel switches must all retain their recipes.
                regs->vertex_attrib_format[0].offset.Assign((i/3)%4*16);
                regs->point_size=float((i/7)%3+1);
                if(i%2)++dirty->shader_profile_revision;
                const auto hint=i%5?Tegra::Engines::Maxwell3D::EngineHint::None:Tegra::Engines::Maxwell3D::EngineHint::OnHLEMacro;
                const auto topology=Maxwell::PrimitiveTopology::Triangles;
                const auto guest=GuestPipelineState::Capture(*regs,topology,hint);
                GraphicsPipelineCacheKey key;key.prefix[0]=u32((i/11)%3);key.state=guest.Project(features);
                Journal::Entry entry{};entry[0]=bits;const auto words=LegacyWords(guest);
                std::copy(words.begin(),words.end(),entry.begin()+1);std::copy(key.prefix.begin(),key.prefix.end(),entry.begin()+279);
                expected.insert(entry);
                // A real changed register updates the revision, even on draws
                // whose resulting host key hides the changed attribute layout.
                ++dirty->shader_profile_revision;
                const void* channel=i%13?static_cast<void*>(regs.get()):static_cast<void*>(dirty.get());
                recorder.ObserveState(*dirty,*regs,topology,hint,channel,key,features);
            }
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
    std::cout<<"PASS: recorder matches legacy recipe sets and reuses existing journals\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
