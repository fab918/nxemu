// TEMP: write tracking statistics for the G2 investigation. Not for commit.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace G2Stats {

enum Counter : int {
    ProtectCalls,     // RasterizerMarkRegionCached(caching = true)
    ProtectBytes,
    UnprotectCalls,   // RasterizerMarkRegionCached(caching = false)
    UnprotectBytes,
    SyncUploads,      // SynchronizeBuffer calls that uploaded
    SyncUploadBytes,
    UniformSyncs,     // classic uniform path calls
    UniformUploads,   // ... that uploaded
    UniformUploadBytes,
    UniformFast,      // streamed uniform path
    UniformFastBytes,
    UniformBig,       // classic path binding size > 16 KiB
    Epochs,
    Count,
};

inline std::atomic<unsigned long long> counters[Count]{};
inline thread_local bool in_uniform = false;

inline bool Enabled() {
    static const bool enabled = std::getenv("NXEMU_G2_STATS") != nullptr;
    return enabled;
}

inline void Add(Counter c, unsigned long long v = 1) {
    if (!Enabled()) return;
    counters[c].fetch_add(v, std::memory_order_relaxed);
    static std::mutex m;
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds(5)) return;
    std::scoped_lock lk{m};
    if (now - last < std::chrono::seconds(5)) return;
    const double s = std::chrono::duration<double>(now - last).count();
    last = now;
    unsigned long long v2[Count];
    for (int i = 0; i < Count; ++i) v2[i] = counters[i].exchange(0, std::memory_order_relaxed);
    if (FILE* f = std::fopen(std::getenv("NXEMU_G2_STATS"), "a")) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        std::fprintf(f,
                     "%lld,prot/s=%.0f,protKB/s=%.0f,unprot/s=%.0f,unprotKB/s=%.0f,syncUp/s=%.0f,syncUpKB/s=%.0f,"
                     "ubo/s=%.0f,uboUp/s=%.0f,uboUpKB/s=%.0f,uboFast/s=%.0f,uboFastKB/s=%.0f,uboBig/s=%.0f,epochs/s=%.0f\n",
                     static_cast<long long>(ms), v2[ProtectCalls] / s, v2[ProtectBytes] / s / 1024,
                     v2[UnprotectCalls] / s, v2[UnprotectBytes] / s / 1024, v2[SyncUploads] / s,
                     v2[SyncUploadBytes] / s / 1024, v2[UniformSyncs] / s, v2[UniformUploads] / s,
                     v2[UniformUploadBytes] / s / 1024, v2[UniformFast] / s, v2[UniformFastBytes] / s / 1024,
                     v2[UniformBig] / s, v2[Epochs] / s);
        std::fclose(f);
    }
}

} // namespace G2Stats
