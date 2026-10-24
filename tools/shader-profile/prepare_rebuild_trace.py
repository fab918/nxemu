"""Add diagnostics ONLY to the existing isolated POC checkout, never src/ here."""
from pathlib import Path

repo=Path(__file__).resolve().parents[2]
root=repo/'build/shader-profile-poc/master'
folder=root/'src/yuzu_video_core/renderer_vulkan'
header=r'''// Isolated POC instrumentation. Never included by the production checkout.
#pragma once
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <fmt/format.h>
namespace Vulkan::RebuildTrace {
inline std::atomic_uint64_t sequence{};
inline std::mutex output_mutex;
inline const char* Directory() {
    static const char* value=std::getenv("NXEMU_SHADER_REBUILD_TRACE");
    return value;
}
inline void Save(u64 key, const char* kind, unsigned stage, const void* data, size_t size) {
    if (!Directory()) return;
    std::scoped_lock lock(output_mutex);
    const auto id=sequence++;
    const auto name=fmt::format("{:016x}-{}-{}-{}.bin",key,kind,stage,id);
    std::ofstream file(std::filesystem::path(Directory())/name,std::ios::binary);
    file.write(static_cast<const char*>(data),size);
    if (!file) throw std::runtime_error("rebuild trace write failed");
}
inline void IR(u64 key, unsigned stage, const Shader::IR::Program& program) {
    if (!Directory()) return;
    auto text=Shader::IR::DumpProgram(program);
    text += "\nFLAGS\n";
    for (const auto* block : program.blocks) {
        for (const auto& inst : *block) text += fmt::format("{}\n",inst.template Flags<u32>());
    }
    Save(key,"ir",stage,text.data(),text.size());
}
}
'''
# Keep the IR-dependent helper out of the native pipeline translation unit.
native_header=header[:header.index('inline void IR(')]+'}\n'
(folder/'rebuild_trace_native.h').write_text(native_header)
(folder/'rebuild_trace.h').write_text(header)
cache=folder/'vk_pipeline_cache.cpp'
text=cache.read_text()
assert 'rebuild_trace.h' not in text, 'probe already installed'
text=text.replace('#include "video_settings.h"','#include "video_settings.h"\n#include "rebuild_trace.h"',1)
text=text.replace('    LOG_INFO(Render_Vulkan, "0x{:016x}", hash);',
                  '    LOG_INFO(Render_Vulkan, "0x{:016x}", hash);\n'
                  '    RebuildTrace::Save(hash, build_in_parallel ? "key-live" : "key-disk", 0, &key, key.Size());',1)
text=text.replace('        if (Settings::values.dump_shaders) {',
                  '        RebuildTrace::IR(hash, static_cast<unsigned>(index), programs[index]);\n'
                  '        if (Settings::values.dump_shaders) {',1)
anchor='        const std::vector<u32> code{EmitSPIRV(profile, runtime_info, program, binding)};'
assert text.count(anchor)==1
text=text.replace(anchor,anchor+'\n        RebuildTrace::Save(hash, "spirv", static_cast<unsigned>(index), code.data(), code.size()*sizeof(u32));')
cache.write_text(text)
native=folder/'vk_graphics_pipeline.cpp'
text=native.read_text()
assert 'rebuild_trace_native.h' not in text
text=text.replace('#include "nxemu-video/video_settings.h"',
                  '#include "nxemu-video/video_settings.h"\n#include "rebuild_trace_native.h"',1)
text=text.replace('            MakePipeline(render_pass);',
                  '            MakePipeline(render_pass);\n'
                  '            RebuildTrace::Save(key.Hash(), "native-success", 0, &key, key.Size());',1)
native.write_text(text)
print(root)
