// SPDX-License-Identifier: GPL-2.0-or-later
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>
#include "nxemu-video/video_settings.h"
#include "yuzu_shader_recompiler/alpha_to_coverage_dither.h"
#include "yuzu_shader_recompiler/backend/bindings.h"
#include "yuzu_shader_recompiler/backend/spirv/emit_spirv.h"
#include "yuzu_shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "yuzu_shader_recompiler/frontend/ir/ir_emitter.h"
#include "yuzu_shader_recompiler/frontend/ir/program.h"
#include "yuzu_shader_recompiler/object_pool.h"
#include "yuzu_shader_recompiler/profile.h"
#include "yuzu_shader_recompiler/runtime_info.h"

VideoSettings videoSettings{};

namespace IR = Shader::IR;

namespace {
unsigned Kept(float alpha, u32 x0 = 0, u32 y0 = 0) {
    unsigned kept{};
    for (u32 y = y0; y < y0 + 2; ++y) {
        for (u32 x = x0; x < x0 + 2; ++x) {
            kept += Shader::AlphaToCoverageDitherKeeps(alpha, x, y) ? 1 : 0;
        }
    }
    return kept;
}

struct Module {
    std::vector<u32> words;
    bool kill{};
    bool frag_coord{};
    bool mask{};
};

// Compile a fragment shader that writes the alpha of render target 0 through the production
// SPIR-V backend.
Module Compile(bool dither, bool writes_color, bool writes_alpha = true) {
    Shader::ObjectPool<IR::Inst> inst_pool{64};
    Shader::ObjectPool<IR::Block> block_pool{4};
    IR::Block* const block = block_pool.Create(inst_pool);
    IR::IREmitter ir{*block};
    ir.Prologue();
    if (writes_color) {
        for (u32 component = 0; component < (writes_alpha ? 4U : 3U); ++component) {
            ir.SetFragColor(0, component, ir.Imm32(0.5f));
        }
    }
    ir.Epilogue();
    IR::Program program{};
    program.stage = Shader::Stage::Fragment;
    program.blocks.push_back(block);
    program.post_order_blocks.push_back(block);
    program.info.stores_frag_color[0] = writes_color;
    program.info.stores_frag_alpha[0] = writes_color && writes_alpha;
    program.syntax_list.push_back({.data{.block = block}, .type = IR::AbstractSyntaxNode::Type::Block});
    program.syntax_list.push_back({.type = IR::AbstractSyntaxNode::Type::Return});
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    Shader::RuntimeInfo runtime{};
    runtime.alpha_to_coverage_dither = dither;
    Shader::Backend::Bindings bindings{};
    Module module;
    module.words = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    const auto& w = module.words;
    for (size_t i = 5; i < w.size();) {
        const u32 count = w[i] >> 16;
        if (count == 0) {
            break;
        }
        const auto op = static_cast<spv::Op>(w[i] & 0xffff);
        module.kill |= op == spv::Op::OpKill;
        module.mask |= op == spv::Op::OpConstant && count == 4 &&
                       w[i + 3] == Shader::ALPHA_TO_COVERAGE_DITHER_MASK;
        module.frag_coord |= op == spv::Op::OpDecorate && count == 4 &&
                             w[i + 2] == static_cast<u32>(spv::Decoration::BuiltIn) &&
                             w[i + 3] == static_cast<u32>(spv::BuiltIn::FragCoord);
        i += count;
    }
    return module;
}
} // namespace

int main(int argc, char** argv) {
    unsigned failures{};
    const auto check = [&](bool ok, const char* label) {
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
    };

    // Coverage of the 2x2 footprint for each eighth of alpha
    constexpr unsigned expected[8]{0, 1, 1, 2, 2, 3, 3, 4};
    bool levels = true;
    for (u32 level = 0; level < 8; ++level) {
        levels &= Kept((static_cast<float>(level) + 0.5f) / 8.0f) == expected[level];
    }
    check(levels, "each alpha eighth keeps 0, 1, 1, 2, 2, 3, 3 or 4 pixels of the footprint");
    check(Kept(0.0f) == 0 && Kept(1.0f) == 4, "alpha 0 drops and alpha 1 keeps the footprint");
    check(Kept(-1.0f) == 0 && Kept(2.0f) == 4, "alpha is saturated");
    check(Kept(std::numeric_limits<float>::quiet_NaN()) == 0, "a NaN alpha drops the footprint");

    bool nested = true;
    for (u32 level = 0; level < 7; ++level) {
        for (u32 xy = 0; xy < 4; ++xy) {
            const bool now = Shader::AlphaToCoverageDitherKeeps(level / 8.0f, xy & 1, xy >> 1);
            const bool next = Shader::AlphaToCoverageDitherKeeps((level + 1) / 8.0f, xy & 1, xy >> 1);
            nested &= !now || next;
        }
    }
    check(nested, "a pixel kept at one alpha stays kept at higher alpha (no flicker while fading)");
    check(Kept(0.3f, 0, 0) == Kept(0.3f, 6, 10), "the pattern repeats every 2 pixels");

    // A fading object covers a share of the screen close to its alpha, instead of all or nothing
    unsigned covered{};
    for (u32 y = 0; y < 64; ++y) {
        for (u32 x = 0; x < 64; ++x) {
            covered += Shader::AlphaToCoverageDitherKeeps(0.4f, x, y) ? 1 : 0;
        }
    }
    check(covered == 64 * 64 / 2, "alpha 0.4 covers half of the pixels instead of all or none");

    const Module off = Compile(false, true);
    const Module on = Compile(true, true);
    const Module no_color = Compile(true, false);
    check(!off.kill && !off.frag_coord, "without dither the shader has no discard");
    check(on.kill && on.frag_coord && on.mask, "with dither the shader reads FragCoord and discards");
    check(!no_color.kill, "a shader that does not write the color is left alone");
    check(!Compile(true, true, false).kill, "a shader that does not write alpha is left alone");

    if (argc > 1) {
        // Write the module for spirv-val
        if (FILE* file = std::fopen(argv[1], "wb")) {
            std::fwrite(on.words.data(), sizeof(u32), on.words.size(), file);
            std::fclose(file);
        }
    }
    std::printf("Alpha-to-coverage dither: %u failures\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
