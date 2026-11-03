// SPDX-License-Identifier: GPL-2.0-or-later
#include "nxemu-video/video_settings.h"
#include "yuzu_shader_recompiler/environment.h"
#include "yuzu_shader_recompiler/frontend/ir/ir_emitter.h"
#include "yuzu_shader_recompiler/frontend/ir/program.h"
#include "yuzu_shader_recompiler/host_translate_info.h"
#include "yuzu_shader_recompiler/ir_opt/passes.h"
#include <cstdio>
#include <cstdlib>
using namespace Shader;
VideoSettings videoSettings{};
class TestEnvironment final : public Shader::Environment {
  public:
    TestEnvironment() { stage = Stage::Fragment; }
    u32 size = 16384;
    u64 ReadInstruction(u32) override { return 0; }
    u32 ReadCbufValue(u32, u32 offset) override {
        if (offset > size || size - offset < 4)
            std::abort();
        return 0;
    }
    u32 ReadCbufSize(u32) override { return size; }
    TextureType ReadTextureType(u32) override { return TextureType::Color2D; }
    TexturePixelFormat ReadTexturePixelFormat(u32) override { return {}; }
    bool IsTexturePixelFormatInteger(u32) override { return false; }
    u32 ReadViewportTransformState() override { return 0; }
    u32 TextureBoundBuffer() const override { return 2; }
    u32 LocalMemorySize() const override { return 0; }
    u32 SharedMemorySize() const override { return 0; }
    std::array<u32, 3> WorkgroupSize() const override { return {1, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    std::optional<ReplaceConstant> GetReplaceConstBuffer(u32, u32) override { return {}; }
    void Dump(u64, u64) override {}
};

int main() {
    for (bool varying : {false, true})
        for (bool swapped : {false, true}) {
            ObjectPool<IR::Inst> pool;
            IR::Block block{pool};
            IR::IREmitter ir{block};
            ir.Prologue();
            auto index =
                varying ? ir.GetReg(IR::Reg::R0) : ir.GetCbuf(ir.Imm32(0), ir.Imm32(13312));
            auto offset = ir.IAdd(ir.ShiftLeftLogical(index, ir.Imm32(2)), ir.Imm32(316));
            auto primary = ir.BitwiseAnd(ir.GetCbuf(ir.Imm32(3), offset), ir.Imm32(0xfffff));
            auto secondary = IR::U32{
                ir.ShiftLeftLogical(ir.GetCbuf(ir.Imm32(0), ir.Imm32(13568)), ir.Imm32(20))};
            auto handle =
                swapped ? ir.BitwiseOr(secondary, primary) : ir.BitwiseOr(primary, secondary);
            IR::TextureInstInfo flags{};
            flags.type.Assign(TextureType::Color2D);
            auto fetch = ir.ImageFetch(handle, ir.CompositeConstruct(ir.Imm32(0), ir.Imm32(0)), {},
                                       ir.Imm32(0), {}, flags);
            for (u32 c = 0; c < 4; c++)
                ir.SetFragColor(0, c, IR::F32{ir.CompositeExtract(fetch, c)});
            ir.Epilogue();
            IR::Program program{};
            program.stage = Stage::Fragment;
            program.blocks = {&block};
            program.post_order_blocks = {&block};
            TestEnvironment env;
            env.size = 16900;
            HostTranslateInfo host{};
            host.support_separate_sampler = true;
            host.max_bindless_descriptors_per_stage = 32768;
            Optimization::TexturePass(env, program, host);
            if (program.info.texture_descriptors.size() != 1)
                return 1;
            const auto& d = program.info.texture_descriptors.front();
            bool pass = d.has_secondary && d.cbuf_index == 3 && d.cbuf_offset == 316 &&
                        d.secondary_cbuf_index == 0 && d.secondary_cbuf_offset == 13568 &&
                        d.mask == 0xfffff;
            if (varying)
                pass &= !d.has_index_cbuf && d.count > 1;
            else
                pass &= d.has_index_cbuf && d.count == 1 && d.index_cbuf_index == 0 &&
                        d.index_cbuf_offset == 13312 && d.index_shift == 2;
            printf("varying=%d swapped=%d descriptors=%u uniform=%d %s\n", varying, swapped,
                   d.count, d.has_index_cbuf, pass ? "PASS" : "FAIL");
            if (!pass)
                return 1;
        }
}
