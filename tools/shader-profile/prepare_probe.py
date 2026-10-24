"""Apply measurement-only probes to a fresh POC checkout, not the production renderer."""
from pathlib import Path
import sys

root = Path(sys.argv[1])


def edit(relative, old, new, count=1):
    path = root / relative
    text = path.read_text(encoding='utf-8')
    if text.count(old) < count:
        raise ValueError(f'missing source anchor: {relative}: {old[:70]}')
    path.write_text(text.replace(old, new, count), encoding='utf-8')


cache = 'src/yuzu_video_core/renderer_vulkan/vk_pipeline_cache.cpp'
edit(cache, '    if (title_id == 0) {',
     '    const auto poc_start = std::chrono::steady_clock::now();\n    if (title_id == 0) {')
edit(cache, '        size_t built{};', '        size_t built{};\n        size_t succeeded{};')
edit(cache, '            if (pipeline) {\n                compute_cache.emplace',
     '            if (pipeline) {\n                ++state.succeeded;\n                compute_cache.emplace')
edit(cache, '            if (pipeline) {\n                graphics_cache.emplace',
     '            if (pipeline) {\n                ++state.succeeded;\n                graphics_cache.emplace')
edit(cache, '    const auto load_graphics{', '    size_t poc_incompatible{};\n    const auto load_graphics{')
edit(cache, '                 !dynamic_features.has_dynamic_vertex_input)) {\n            return true;',
     '                 !dynamic_features.has_dynamic_vertex_input)) {\n            ++poc_incompatible;\n            return true;')
edit(cache, '    if (state.statistics) {\n        state.statistics->Report();', '''    LOG_INFO(Render_Vulkan, "NXSHADER_POC startup attempted={} succeeded={} incompatible={} graphics_keys={} compute_keys={} elapsed_ms={} host_features={},{},{},{},{},{},{}",
             state.total, state.succeeded, poc_incompatible, graphics_cache.size(), compute_cache.size(),
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - poc_start).count(),
             dynamic_features.has_extended_dynamic_state, dynamic_features.has_extended_dynamic_state_2,
             dynamic_features.has_extended_dynamic_state_2_extra, dynamic_features.has_extended_dynamic_state_3_blend,
             dynamic_features.has_extended_dynamic_state_3_enables, dynamic_features.has_dynamic_vertex_input,
             dynamic_features.has_vertex_stride_workaround);
    if (state.statistics) {
        state.statistics->Report();''')

perf = 'src/nxemu-os/core/perf_stats.cpp'
edit(perf, 'PerfStats::PerfStats(u64 title_id_) : title_id(title_id_) {}',
     'PerfStats::PerfStats(u64 title_id_) : title_id(title_id_) { Settings::values.record_frame_times = true; }')
edit(perf, '''    std::copy(perf_history.begin() + IgnoreFrames, perf_history.begin() + current_index,
              std::ostream_iterator<double>(stream, "\\n"));''', '''    stream << "active_ms,interval_ms\\n";
    for (size_t i = IgnoreFrames; i < current_index; ++i) {
        stream << perf_history[i] << ',' << poc_intervals[i] << '\\n';
    }''')
edit(perf, '    accumulated_frametime += frame_time;', '''    if (current_index > 0 && current_index <= poc_intervals.size()) {
        poc_intervals[current_index - 1] = std::chrono::duration<double, std::milli>(frame_end - previous_frame_end).count();
    }
    accumulated_frametime += frame_time;''')
edit('src/nxemu-os/core/perf_stats.h', '    std::array<double, 216000> perf_history{};',
     '    std::array<double, 216000> perf_history{};\n    std::array<double, 216000> poc_intervals{};')
