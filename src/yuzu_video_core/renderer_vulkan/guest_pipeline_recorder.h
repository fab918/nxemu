// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yuzu_common/thread_worker.h"
#include "yuzu_video_core/engines/draw_manager.h"
#include "yuzu_video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "yuzu_video_core/shader_profile_journal.h"
#include "yuzu_video_core/shader_profile_runtime.h"
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace Vulkan
{
// Register writes only increment a revision. Canonical capture and deduplication
// happen on semantic transitions; all hashing and file I/O run on the cache worker.
class GuestPipelineRecorder
{
    using Engine = Tegra::Engines::Maxwell3D;
    using Entry = VideoCommon::ShaderProfile::Journal::Entry;

public:
    GuestPipelineRecorder(std::filesystem::path path_, Common::ThreadWorker & writer_) :
        path(std::move(path_)), writer(writer_)
    {
        auto saved = VideoCommon::ShaderProfile::Journal::Read(path);
        entries = std::move(saved.entries);
        loaded_recipes = entries.size();
        if (saved.incomplete_tail) std::filesystem::resize_file(path, saved.valid_bytes);
        if (!std::filesystem::exists(path))
        {
            std::ofstream out(path, std::ios::binary);
            out << VideoCommon::ShaderProfile::Journal::Magic;
            if (!out) throw std::runtime_error("Cannot initialize guest recipe journal");
        }
        timing = std::getenv("NXEMU_RECIPE_RECORD_TIMING") != nullptr;
        if (const char * output = std::getenv("NXEMU_RECIPE_RECORD_REPORT"); output && *output)
        {
            report_path = std::filesystem::u8path(output);
            timing = true;
        }
        if (timing) write_metrics = std::make_shared<VideoCommon::ShaderProfile::Journal::WriteMetrics>();
    }
    static void MarkFailed(const std::filesystem::path & target) noexcept
    {
        try
        {
            auto marker = target;
            marker += ".failed";
            std::ofstream out(marker, std::ios::binary | std::ios::trunc);
            out << "Incomplete recording; see renderer log. Clear the local cache to start again.\n";
        }
        catch (...)
        {
        }
    }
    void Observe(Engine & engine, const GraphicsPipelineCacheKey & key, const DynamicFeatures & features) noexcept
    {
        static_assert(offsetof(GraphicsPipelineCacheKey, state) == 104 * sizeof(u32));
        struct Sample
        {
            GuestPipelineRecorder & owner;
            std::chrono::steady_clock::time_point start{};
            explicit Sample(GuestPipelineRecorder & recorder) :
                owner(recorder)
            {
                if (owner.timing) start = std::chrono::steady_clock::now();
            }
            ~Sample()
            {
                if (owner.timing)
                {
                    ++owner.calls;
                    owner.elapsed_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
                }
            }
        } sample(*this);
        if (failed) return;
        try
        {
            const auto topology = engine.draw_manager->GetDrawState().topology;
            const auto hint = engine.engine_state;
            const bool untracked = engine.dirty.shader_profile_registers[offsetof(Engine::Regs, alpha_test_enabled) / 4] == 0;
            const bool changed = untracked || channel != &engine || revision != engine.dirty.shader_profile_revision ||
                                 topology != last_topology || hint != last_hint;
            if (!changed && std::memcmp(last_prefix.data(), &key, sizeof(last_prefix)) == 0) return;
            if (changed)
            {
                if (untracked) GuestPipelineState::Track(engine.dirty);
                guest = GuestPipelineState::Capture(engine.regs, topology, hint);
                channel = &engine;
                revision = engine.dirty.shader_profile_revision;
                last_topology = topology;
                last_hint = hint;
                ++captures;
            }
            std::memcpy(last_prefix.data(), &key, sizeof(last_prefix));
            // Reuse the same projection as normal rendering, including host dynamic states.
            if (guest.Project(features) != key.state)
                throw std::runtime_error("Automatic guest recipe projection mismatch");
            Entry entry{};
            entry[0] = (features.has_extended_dynamic_state ? 1U : 0U) |
                       (features.has_extended_dynamic_state_2 ? 2U : 0U) |
                       (features.has_extended_dynamic_state_2_extra ? 4U : 0U) |
                       (features.has_extended_dynamic_state_3_blend ? 8U : 0U) |
                       (features.has_extended_dynamic_state_3_enables ? 16U : 0U) |
                       (features.has_dynamic_vertex_input ? 32U : 0U) |
                       (features.has_vertex_stride_workaround ? 64U : 0U);
            const auto words = guest.Words();
            if (words.size() != 278) throw std::runtime_error("Unsupported automatic guest state ABI");
            std::copy(words.begin(), words.end(), entry.begin() + 1);
            std::copy(last_prefix.begin(), last_prefix.end(), entry.begin() + 279);
            if (entries.contains(entry)) return;
            if (entries.size() >= VideoCommon::ShaderProfile::Journal::MaxEntries)
                throw std::runtime_error("Automatic guest recipe journal is full");
            entries.insert(entry);
            writer.QueueWork([target = path, entry, metrics = write_metrics] {
                try
                {
                    const auto before = metrics ? VideoCommon::ShaderProfile::Runtime::ThreadCpu() : VideoCommon::ShaderProfile::Runtime::CpuSample{};
                    VideoCommon::ShaderProfile::Journal::Append(target, entry, metrics.get());
                    if (metrics)
                    {
                        const auto after = VideoCommon::ShaderProfile::Runtime::ThreadCpu();
                        if (before.time_available && after.time_available && after.ns >= before.ns)
                        {
                            metrics->cpu_ns += after.ns - before.ns;
                            ++metrics->cpu_samples;
                        }
                        if (before.cycles_available && after.cycles_available && after.cycles >= before.cycles)
                        {
                            metrics->cpu_cycles += after.cycles - before.cycles;
                            ++metrics->cycle_samples;
                        }
                    }
                }
                catch (const std::exception & e)
                {
                    MarkFailed(target);
                    LOG_ERROR(Render_Vulkan, "Guest recipe journal: {}", e.what());
                }
            });
        }
        catch (const std::exception & e)
        {
            failed = true;
            writer.QueueWork([target = path] { MarkFailed(target); });
            LOG_ERROR(Render_Vulkan, "Automatic guest recipe recorder stopped: {}", e.what());
        }
    }
    ~GuestPipelineRecorder()
    {
        LOG_INFO(Render_Vulkan, "Automatic guest recipes: total_unique={} loaded={} new={} captures={} failed={} measured_observations={} observer_wall_ns={}", entries.size(), loaded_recipes, entries.size()-loaded_recipes, captures, failed, calls, elapsed_ns);
        // All writer work is drained before this recorder is destroyed. Observer
        // time is elapsed steady-clock time, not per-thread CPU accounting.
        if (!report_path.empty() && write_metrics) try
            {
                const auto & m = *write_metrics;
                std::ofstream out(report_path, std::ios::binary | std::ios::trunc);
                out << "{\"schema\":2,\"observer_timing_enabled\":true,\"observer_scope\":\"Observe only; excludes register revision tracking\",\"unique_recipes\":" << entries.size()
                    << ",\"loaded_recipes\":" << loaded_recipes << ",\"new_recipes\":" << entries.size()-loaded_recipes << ",\"captures\":" << captures
                    << ",\"failed\":" << failed << ",\"observed_draws\":" << calls << ",\"observer_wall_ns\":" << elapsed_ns
                    << ",\"worker_frames\":" << m.frames << ",\"worker_frame_bytes\":" << m.bytes
                    << ",\"worker_encoding_ns\":" << m.encoding_ns << ",\"worker_io_ns\":" << m.io_ns
                    << ",\"worker_cpu_ns\":" << m.cpu_ns << ",\"worker_cpu_samples\":" << m.cpu_samples
                    << ",\"worker_cpu_cycles\":" << m.cpu_cycles << ",\"worker_cycle_samples\":" << m.cycle_samples << "}";
                out.flush();
                if (!out) LOG_WARNING(Render_Vulkan, "Cannot write guest recording cost report");
            }
            catch (const std::exception & e)
            {
                LOG_WARNING(Render_Vulkan, "Guest recording cost report: {}", e.what());
            }
    }

private:
    std::filesystem::path path;
    Common::ThreadWorker & writer;
    std::set<Entry> entries;
    std::array<u32, 104> last_prefix{};
    GuestPipelineState guest{};
    Engine * channel{};
    u64 revision{}, captures{}, calls{}, elapsed_ns{}, loaded_recipes{};
    Engine::Regs::PrimitiveTopology last_topology{};
    Engine::EngineHint last_hint{};
    bool failed{};
    bool timing{};
    std::filesystem::path report_path;
    std::shared_ptr<VideoCommon::ShaderProfile::Journal::WriteMetrics> write_metrics;
};
} // namespace Vulkan
