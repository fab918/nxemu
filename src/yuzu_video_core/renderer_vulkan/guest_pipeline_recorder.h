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
#include <unordered_set>

namespace Vulkan
{
// Register writes only increment a revision. Canonical capture and deduplication
// happen on semantic transitions. Exact dedup uses a local non-cryptographic
// hash; resource SHA-256 and file I/O run on the cache worker.
class GuestPipelineRecorder
{
    using Engine = Tegra::Engines::Maxwell3D;
    using Entry = VideoCommon::ShaderProfile::Journal::Entry;
    struct StageSample {
        u64* total{};
        std::chrono::steady_clock::time_point start{};
        StageSample(bool enabled, u64& sum) : total(enabled ? &sum : nullptr) {
            if (total) start = std::chrono::steady_clock::now();
        }
        ~StageSample() {
            if (total) *total += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()-start).count();
        }
    };

public:
    GuestPipelineRecorder(std::filesystem::path path_, Common::ThreadWorker & writer_) :
        path(std::move(path_)), writer(writer_)
    {
        auto saved = VideoCommon::ShaderProfile::Journal::Read(path);
        entries.reserve(VideoCommon::ShaderProfile::Journal::MaxEntries);
        entries.insert(saved.entries.begin(), saved.entries.end());
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
        ObserveState(engine.dirty, engine.regs, engine.draw_manager->GetDrawState().topology,
                     engine.engine_state, &engine, key, features);
    }
    // Explicit engine inputs allow deterministic replay tests without constructing
    // an emulated GPU. The live path above supplies exactly these same values.
    void ObserveState(Engine::DirtyState& dirty, const Engine::Regs& regs,
                      Engine::Regs::PrimitiveTopology topology, Engine::EngineHint hint,
                      const void* source, const GraphicsPipelineCacheKey& key,
                      const DynamicFeatures& features) noexcept
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
            const bool untracked = dirty.shader_profile_registers[offsetof(Engine::Regs, alpha_test_enabled) / 4] == 0;
            const bool changed = untracked || channel != source || revision != dirty.shader_profile_revision ||
                                 topology != last_topology || hint != last_hint;
            if (!changed && std::memcmp(last_prefix.data(), &key, sizeof(last_prefix)) == 0) return;
            if (changed)
            {
                if (untracked) GuestPipelineState::Track(dirty);
                const auto next = [&] {
                    StageSample measured(timing, capture_wall_ns);
                    return GuestPipelineState::Capture(regs, topology, hint);
                }();
                const bool same_guest = channel &&
                    next.enabled_vertex_streams == guest.enabled_vertex_streams &&
                    std::memcmp(&next.complete, &guest.complete, sizeof(next.complete)) == 0;
                channel = source;
                revision = dirty.shader_profile_revision;
                last_topology = topology;
                last_hint = hint;
                ++captures;
                if (same_guest && std::memcmp(last_prefix.data(), &key, sizeof(last_prefix)) == 0) {
                    if (timing) ++unchanged_captures;
                    return;
                }
                if (!same_guest) {
                    StageSample measured(timing, canonical_wall_ns);
                    guest = next;
                    guest.WriteWords(std::span<u32, GuestPipelineState::WordCount>(pending_entry.data()+1, GuestPipelineState::WordCount));
                    projected = guest.Project(features);
                    if (timing) ++canonical_updates;
                }
            }
            std::memcpy(last_prefix.data(), &key, sizeof(last_prefix));
            // Reuse the same projection as normal rendering, including host dynamic states.
            if (projected != key.state)
                throw std::runtime_error("Automatic guest recipe projection mismatch");
            pending_entry[0] = (features.has_extended_dynamic_state ? 1U : 0U) |
                       (features.has_extended_dynamic_state_2 ? 2U : 0U) |
                       (features.has_extended_dynamic_state_2_extra ? 4U : 0U) |
                       (features.has_extended_dynamic_state_3_blend ? 8U : 0U) |
                       (features.has_extended_dynamic_state_3_enables ? 16U : 0U) |
                       (features.has_dynamic_vertex_input ? 32U : 0U) |
                       (features.has_vertex_stride_workaround ? 64U : 0U);
            static_assert(GuestPipelineState::WordCount == 278);
            std::copy(last_prefix.begin(), last_prefix.end(), pending_entry.begin() + 279);
            const bool duplicate = [&] {
                StageSample measured(timing, dedup_wall_ns);
                return entries.contains(pending_entry);
            }();
            if (duplicate) { if (timing) ++duplicate_entries; return; }
            if (entries.size() >= VideoCommon::ShaderProfile::Journal::MaxEntries)
                throw std::runtime_error("Automatic guest recipe journal is full");
            {
                StageSample measured(timing, dedup_wall_ns);
                entries.insert(pending_entry);
            }
            const auto entry = pending_entry;
            StageSample measured(timing, enqueue_wall_ns);
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
                    << ",\"unchanged_captures\":" << unchanged_captures << ",\"canonical_updates\":" << canonical_updates << ",\"duplicate_entries\":" << duplicate_entries
                    << ",\"capture_wall_ns\":" << capture_wall_ns << ",\"canonical_wall_ns\":" << canonical_wall_ns
                    << ",\"dedup_wall_ns\":" << dedup_wall_ns << ",\"enqueue_wall_ns\":" << enqueue_wall_ns
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
    std::unordered_set<Entry, VideoCommon::ShaderProfile::Journal::EntryHash> entries;
    Entry pending_entry{};
    std::array<u32, 104> last_prefix{};
    GuestPipelineState guest{};
    FixedPipelineState projected{};
    const void* channel{};
    u64 revision{}, captures{}, calls{}, elapsed_ns{}, loaded_recipes{};
    u64 unchanged_captures{}, canonical_updates{}, duplicate_entries{};
    u64 capture_wall_ns{}, canonical_wall_ns{}, dedup_wall_ns{}, enqueue_wall_ns{};
    Engine::Regs::PrimitiveTopology last_topology{};
    Engine::EngineHint last_hint{};
    bool failed{};
    bool timing{};
    std::filesystem::path report_path;
    std::shared_ptr<VideoCommon::ShaderProfile::Journal::WriteMetrics> write_metrics;
};
} // namespace Vulkan
