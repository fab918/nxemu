"""Extend ONLY the existing diagnostic checkout with compute traces."""
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
folder=repo/'build/shader-profile-poc/master/src/yuzu_video_core/renderer_vulkan'
path=folder/'vk_pipeline_cache.cpp'; text=path.read_text()
assert 'compute-key-disk' not in text
anchor='    LOG_INFO(Render_Vulkan, "0x{:016x}", hash);\n\n    Shader::Maxwell::Flow::CFG cfg{env, pools.flow_block, env.StartAddress()};'
assert text.count(anchor)==1
text=text.replace(anchor,'    LOG_INFO(Render_Vulkan, "0x{:016x}", hash);\n'
    '    RebuildTrace::Save(hash, build_in_parallel ? "compute-key-live" : "compute-key-disk", 6, &key, sizeof(key));\n\n'
    '    Shader::Maxwell::Flow::CFG cfg{env, pools.flow_block, env.StartAddress()};')
anchor='    auto program{TranslateProgram(pools.inst, pools.block, env, cfg, host_info)};'
assert text.count(anchor)==1
text=text.replace(anchor,anchor+'\n    RebuildTrace::IR(hash, 6, program);')
anchor='    const std::vector<u32> code{EmitSPIRV(profile, program)};'
assert text.count(anchor)==1
text=text.replace(anchor,anchor+'\n    RebuildTrace::Save(hash, "compute-spirv", 6, code.data(), code.size()*sizeof(u32));')
anchor='&shader_notify, program.info, std::move(spv_module), staging_pool);'
assert text.count(anchor)==1
text=text.replace(anchor,'&shader_notify, program.info, std::move(spv_module), staging_pool,\n'
    '                                             hash, Common::BitCast<std::array<u8, 64>>(key));')
path.write_text(text)
path=folder/'vk_compute_pipeline.h'; text=path.read_text()
anchor='vk::ShaderModule spv_module, StagingBufferPool& staging_pool);'
assert text.count(anchor)==1
text=text.replace(anchor,'vk::ShaderModule spv_module, StagingBufferPool& staging_pool,\n'
                  '                             u64 trace_hash = 0, std::array<u8, 64> trace_key = {});')
path.write_text(text)
path=folder/'vk_compute_pipeline.cpp'; text=path.read_text()
text=text.replace('#include "yuzu_video_core/vulkan_common/vulkan_wrapper.h"',
                  '#include "yuzu_video_core/vulkan_common/vulkan_wrapper.h"\n#include "rebuild_trace_native.h"',1)
anchor='vk::ShaderModule spv_module_, StagingBufferPool& staging_pool_)'
assert text.count(anchor)==1
text=text.replace(anchor,'vk::ShaderModule spv_module_, StagingBufferPool& staging_pool_,\n'
                  '                                 u64 trace_hash, std::array<u8, 64> trace_key)')
text=text.replace('auto func{[this, &descriptor_pool, shader_notify, pipeline_statistics]',
                  'auto func{[this, &descriptor_pool, shader_notify, pipeline_statistics, trace_hash, trace_key]',1)
anchor='                *pipeline_cache);'
assert text.count(anchor)==1
text=text.replace(anchor,anchor+'\n            if (trace_hash) RebuildTrace::Save(trace_hash, "compute-native-success", 6, trace_key.data(), trace_key.size());')
path.write_text(text)
print(folder)
