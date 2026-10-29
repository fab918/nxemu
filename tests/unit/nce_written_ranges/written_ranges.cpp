// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>
#include "nxemu-os/core/nce_written_ranges.h"

namespace {
constexpr u64 PageSize = 0x1000;
using Page = std::array<u8, PageSize>;
using Ranges = std::vector<std::pair<u64, u64>>;

Ranges Written(const Page& current, const Page& snapshot) {
    Ranges ranges;
    Core::ForEachNceWrittenRange(current.data(), snapshot.data(), PageSize,
                                 [&](u64 offset, u64 size) { ranges.emplace_back(offset, size); });
    return ranges;
}

void Store(Page& page, u64 offset, u32 value) {
    std::memcpy(page.data() + offset, &value, sizeof(value));
}

u32 Load(const Page& page, u64 offset) {
    u32 value;
    std::memcpy(&value, page.data() + offset, sizeof(value));
    return value;
}

// The buffer cache drops the GPU writes a CPU write overlaps and uploads guest memory there.
void Invalidate(Page& host, std::vector<bool>& gpu_modified, const Page& guest, const Ranges& ranges) {
    for (const auto& [offset, size] : ranges) {
        for (u64 i = offset; i < offset + size; ++i) {
            gpu_modified[i] = false;
            host[i] = guest[i];
        }
    }
}
} // namespace

int main() {
    unsigned failures{};
    const auto check = [&](bool ok, const char* label) {
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
    };

    Page snapshot{};
    check(Written(snapshot, snapshot).empty(), "an unchanged page reports nothing");

    Page current = snapshot;
    Store(current, 0x104, 1);
    Ranges ranges = Written(current, snapshot);
    check(ranges.size() == 1 && ranges[0] == std::pair<u64, u64>{0x104, 4}, "one word is reported as itself");

    current = snapshot;
    Store(current, 0x3c, 1);
    Store(current, 0x40, 2);
    Store(current, 0x7c, 3);
    ranges = Written(current, snapshot);
    check(ranges.size() == 1 && ranges[0] == std::pair<u64, u64>{0x3c, 0x44}, "a run crossing blocks is trimmed at both ends");

    current = snapshot;
    Store(current, 0x0, 1);
    Store(current, 0x200, 2);
    ranges = Written(current, snapshot);
    check(ranges.size() == 2 && ranges[0] == std::pair<u64, u64>{0, 4} && ranges[1] == std::pair<u64, u64>{0x200, 4},
          "separate writes are reported separately");

    current.fill(0xff);
    ranges = Written(current, snapshot);
    check(ranges.size() == 1 && ranges[0] == std::pair<u64, u64>{0, PageSize}, "a rewritten page is one range");

    // Pokemon Legends Z-A: the GPU writes counters at the end of one storage buffer, in a page
    // that the CPU also fills from 0xaa0 with the next buffer. The counters are not downloaded.
    Page guest{};
    Page host = guest;
    std::vector<bool> gpu_modified(PageSize);
    for (u64 offset = 0x6c; offset < 0xa80; offset += 0x100) {
        Store(host, offset, 0xf9);
        for (u64 i = offset; i < offset + 4; ++i) gpu_modified[i] = true;
    }
    const Page fault_snapshot = guest;
    for (u64 offset = 0xaa0; offset < PageSize; offset += 4) Store(guest, offset, 0x1234);
    for (u64 offset = 0xaa0; offset < PageSize; offset += 4) Store(host, offset, 0x1234);

    Page old_host = host;
    std::vector<bool> old_modified = gpu_modified;
    Invalidate(old_host, old_modified, guest, {{0, PageSize}});
    check(Load(old_host, 0x6c) == 0, "reporting the whole page loses the GPU counters (the bug)");

    Invalidate(host, gpu_modified, guest, Written(guest, fault_snapshot));
    bool kept = true;
    for (u64 offset = 0x6c; offset < 0xa80; offset += 0x100) kept &= Load(host, offset) == 0xf9 && gpu_modified[offset];
    check(kept, "the GPU counters next to a CPU write are kept");
    check(Load(host, 0xaa0) == 0x1234 && Load(host, PageSize - 4) == 0x1234, "the CPU write reaches the host buffer");

    std::printf("NCE written ranges: %u failures\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
