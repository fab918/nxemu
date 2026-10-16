// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iterator>
#include <mutex>
#include <numeric>
#include <sstream>
#include <thread>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include "yuzu_common/fs/file.h"
#include "yuzu_common/fs/fs.h"
#include "yuzu_common/fs/path_util.h"
#include "yuzu_common/settings.h"
#include "yuzu_common/logging/log.h"
#include "os_settings.h"
#include "core/perf_stats.h"

using namespace std::chrono_literals;
using DoubleSecs = std::chrono::duration<double, std::chrono::seconds::period>;
using std::chrono::duration_cast;
using std::chrono::microseconds;

// Purposefully ignore the first five frames, as there's a significant amount of overhead in
// booting that we shouldn't account for
constexpr std::size_t IgnoreFrames = 5;

namespace Core {

PerfStats::PerfStats(u64 title_id_) : title_id(title_id_) {}

PerfStats::~PerfStats() {
    if (!Settings::values.record_frame_times || title_id == 0) {
        return;
    }

    const std::time_t t = std::time(nullptr);
    std::ostringstream stream;
    std::copy(perf_history.begin() + IgnoreFrames, perf_history.begin() + current_index,
              std::ostream_iterator<double>(stream, "\n"));

    const auto path = Common::FS::GetYuzuPath(Common::FS::YuzuPath::LogDir);
    // %F Date format expanded is "%Y-%m-%d"
    const auto filename = fmt::format("{:%F-%H-%M}_{:016X}.csv", *std::localtime(&t), title_id);
    const auto filepath = path / filename;

    if (Common::FS::CreateParentDir(filepath)) {
        Common::FS::IOFile file(filepath, Common::FS::FileAccessMode::Write,
                                Common::FS::FileType::TextFile);
        void(file.WriteString(stream.str()));
    }
}

void PerfStats::BeginSystemFrame() {
    std::scoped_lock lock{object_mutex};

    frame_begin = Clock::now();
}

void PerfStats::EndSystemFrame() {
    std::scoped_lock lock{object_mutex};

    auto frame_end = Clock::now();
    const auto frame_time = frame_end - frame_begin;
    if (current_index < perf_history.size()) {
        perf_history[current_index++] =
            std::chrono::duration<double, std::milli>(frame_time).count();
    }
    accumulated_frametime += frame_time;
    system_frames += 1;

    previous_frame_length = frame_end - previous_frame_end;
    previous_frame_end = frame_end;
}

void PerfStats::EndGameFrame() {
    game_frames.fetch_add(1, std::memory_order_relaxed);
    static const bool trace_frames = std::getenv("NXEMU_TRACE_FRAME_TIMES") != nullptr;
    if (!measure_game_frames.load(std::memory_order_relaxed) && !trace_frames) {
        return;
    }

    const Clock::rep now = Clock::now().time_since_epoch().count();
    const Clock::rep previous = last_game_frame.exchange(now, std::memory_order_relaxed);
    if (previous == 0) {
        return;
    }
    const Clock::rep length = now - previous;
    if (trace_frames) {
        const u64 index = game_frame_trace_written.fetch_add(1, std::memory_order_relaxed);
        auto& slot = game_frame_trace[index % game_frame_trace.size()];
        slot.length.store(length, std::memory_order_relaxed);
        slot.sequence.store(index + 1, std::memory_order_release);
    }
    Clock::rep longest = longest_game_frame.load(std::memory_order_relaxed);
    while (length > longest &&
           !longest_game_frame.compare_exchange_weak(longest, length, std::memory_order_relaxed)) {
    }
}

void PerfStats::MeasureGameFrames(bool measure) {
    if (measure_game_frames.exchange(measure, std::memory_order_relaxed) != measure) {
        last_game_frame.store(0, std::memory_order_relaxed);
        longest_game_frame.store(0, std::memory_order_relaxed);
    }
}

double PerfStats::GetMeanFrametime() const {
    std::scoped_lock lock{object_mutex};

    if (current_index <= IgnoreFrames) {
        return 0;
    }

    const double sum = std::accumulate(perf_history.begin() + IgnoreFrames,
                                       perf_history.begin() + current_index, 0.0);
    return sum / static_cast<double>(current_index - IgnoreFrames);
}

PerfStatsResults PerfStats::GetAndResetStats(microseconds current_system_time_us) {
    std::scoped_lock lock{object_mutex};

    const auto now = Clock::now();
    // Walltime elapsed since stats were reset
    const auto interval = duration_cast<DoubleSecs>(now - reset_point).count();

    const auto system_us_per_second = (current_system_time_us - reset_point_system_us) / interval;
    const auto current_frames = static_cast<double>(game_frames.load(std::memory_order_relaxed));
    const auto current_fps = current_frames / interval;
    // A game frame that has not ended yet counts for the time it has taken so far
    const Clock::rep last_frame = last_game_frame.load(std::memory_order_relaxed);
    const Clock::duration longest_frame{
        std::max(longest_game_frame.exchange(0, std::memory_order_relaxed),
                 last_frame != 0 ? now.time_since_epoch().count() - last_frame : Clock::rep{0})};
    const PerfStatsResults results{
        .system_fps = static_cast<double>(system_frames) / interval,
        .average_game_fps = (current_fps + previous_fps) / 2.0,
        .frametime = duration_cast<DoubleSecs>(accumulated_frametime).count() /
                     static_cast<double>(system_frames),
        .emulation_speed = system_us_per_second.count() / 1'000'000.0,
        .longest_game_frame = duration_cast<DoubleSecs>(longest_frame).count(),
    };

    static const bool trace = std::getenv("NXEMU_TRACE_PERFORMANCE") != nullptr;
    if (trace) {
        LOG_INFO(Core, "Performance: game_fps={:.3f} system_fps={:.3f} frame_ms={:.3f} longest_ms={:.3f} guest_us={}",
                 results.average_game_fps, results.system_fps, results.frametime * 1000.0,
                 results.longest_game_frame * 1000.0, current_system_time_us.count());
    }

    static const bool trace_frames = std::getenv("NXEMU_TRACE_FRAME_TIMES") != nullptr;
    if (trace_frames) {
        const u64 end = game_frame_trace_written.load(std::memory_order_relaxed);
        const u64 begin = std::max(game_frame_trace_read,
                                  end > game_frame_trace.size() ? end - game_frame_trace.size() : 0);
        std::vector<double> lengths;
        lengths.reserve(static_cast<size_t>(end - begin));
        u64 dropped = begin - game_frame_trace_read;
        for (u64 index = begin; index < end; ++index) {
            const auto& slot = game_frame_trace[index % game_frame_trace.size()];
            const u64 sequence = slot.sequence.load(std::memory_order_acquire);
            const Clock::duration length{slot.length.load(std::memory_order_relaxed)};
            if (sequence == index + 1 &&
                slot.sequence.load(std::memory_order_acquire) == sequence) {
                lengths.push_back(std::chrono::duration<double, std::milli>(length).count());
            } else {
                ++dropped;
            }
        }
        game_frame_trace_read = end;
        LOG_INFO(Core, "FrameTimes: guest_us={} dropped={} frame_ms=[{:.3f}]",
                 current_system_time_us.count(), dropped, fmt::join(lengths, ","));
    }

    // Reset counters
    reset_point = now;
    reset_point_system_us = current_system_time_us;
    accumulated_frametime = Clock::duration::zero();
    system_frames = 0;
    game_frames.store(0, std::memory_order_relaxed);
    previous_fps = current_fps;

    return results;
}

double PerfStats::GetLastFrameTimeScale() const {
    std::scoped_lock lock{object_mutex};

    constexpr double FRAME_LENGTH = 1.0 / 60;
    return duration_cast<DoubleSecs>(previous_frame_length).count() / FRAME_LENGTH;
}

void SpeedLimiter::DoSpeedLimiting(microseconds current_system_time_us) {
    if (osSettings.use_multi_core ||
        !osSettings.use_speed_limit) {
        return;
    }

    auto now = Clock::now();

    const double sleep_scale = osSettings.speed_limit / 100.0;

    // Max lag caused by slow frames. Shouldn't be more than the length of a frame at the current
    // speed percent or it will clamp too much and prevent this from properly limiting to that
    // percent. High values means it'll take longer after a slow frame to recover and start
    // limiting
    const microseconds max_lag_time_us = duration_cast<microseconds>(
        std::chrono::duration<double, std::chrono::microseconds::period>(25ms / sleep_scale));
    speed_limiting_delta_err += duration_cast<microseconds>(
        std::chrono::duration<double, std::chrono::microseconds::period>(
            (current_system_time_us - previous_system_time_us) / sleep_scale));
    speed_limiting_delta_err -= duration_cast<microseconds>(now - previous_walltime);
    speed_limiting_delta_err =
        std::clamp(speed_limiting_delta_err, -max_lag_time_us, max_lag_time_us);

    if (speed_limiting_delta_err > microseconds::zero()) {
        std::this_thread::sleep_for(speed_limiting_delta_err);
        auto now_after_sleep = Clock::now();
        speed_limiting_delta_err -= duration_cast<microseconds>(now_after_sleep - now);
        now = now_after_sleep;
    }

    previous_system_time_us = current_system_time_us;
    previous_walltime = now;
}

} // namespace Core
