// SPDX-License-Identifier: GPL-2.0-or-later
#include "descriptor_read_batch.h"
#include "descriptor_table.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {
void Require(bool value) {
    if (!value)
        std::abort();
}
struct Descriptor {
    std::array<uint64_t, 4> words;
    auto operator<=>(const Descriptor&) const = default;
};
struct View {
    uint32_t index;
};
struct PagedMemory {
    static constexpr size_t PageSize = 4096;
    std::array<std::unique_ptr<std::byte[]>, 40> pages;
    mutable size_t reads{};
    PagedMemory() {
        for (auto& page : pages)
            page = std::make_unique<std::byte[]>(PageSize);
        for (uint32_t i = 0; i < 4096; ++i)
            Set(i, {i, i * 7u, i ^ 0xabcU, 0});
    }
    void Set(uint32_t index, Descriptor value) {
        const size_t offset = 64 + index * sizeof(Descriptor);
        std::memcpy(pages[offset / PageSize].get() + offset % PageSize, &value, sizeof(value));
    }
    void ReadBlockUnsafe(uint64_t address, void* output, size_t size) const {
        ++reads;
        auto* dst = static_cast<std::byte*>(output);
        while (size) {
            Require(address / PageSize < pages.size());
            const size_t amount = std::min<size_t>(size, PageSize - address % PageSize);
            std::memcpy(dst, pages[address / PageSize].get() + address % PageSize, amount);
            address += amount;
            dst += amount;
            size -= amount;
        }
    }
};
using Table = VideoCommon::DescriptorTable<Descriptor, PagedMemory>;
using Result = std::pair<Descriptor, bool>;
std::vector<Result> Visit(Table& table, std::span<View> views, bool batch) {
    std::vector<Result> out;
    out.reserve(views.size());
    VideoCommon::VisitDescriptorRuns<Descriptor>(
        views, table.Limit(), batch,
        [&](uint32_t first, std::span<Descriptor> descriptors) {
            table.ReadRange(first, descriptors);
        },
        [&](View& view, const Descriptor* snapshot) {
            out.push_back(view.index > table.Limit() ? Result{} : table.Read(view.index, snapshot));
        });
    return out;
}
} // namespace
int main(int argc, char**) {
    PagedMemory memory;
    Table reference{memory}, candidate{memory};
    Require(reference.Synchronize(64, 4095));
    Require(candidate.Synchronize(64, 4095));
    std::vector<View> views;
    for (uint32_t i = 0; i < 4096; i++)
        views.push_back({i});
    memory.reads = 0;
    auto expected = Visit(reference, views, false);
    Require(memory.reads == 4096);
    memory.reads = 0;
    Require(Visit(candidate, views, true) == expected);
    Require(memory.reads == 32);
    // A fresh visit must see edits, including at a page/chunk boundary.
    for (auto index : {0U, 125U, 127U, 128U, 4095U})
        memory.Set(index, {7, 8, 9, 10});
    Require(Visit(candidate, views, true) == Visit(reference, views, false));
    Require(Visit(candidate, views, true) == Visit(reference, views, false));
    reference.Invalidate();
    candidate.Invalidate();
    Require(Visit(candidate, views, true) == Visit(reference, views, false));
    std::mt19937 random{42};
    for (int trial = 0; trial < 200; trial++) {
        views.clear();
        for (uint32_t i = 0; i < 300; i++)
            views.push_back({random() % 4100});
        if (trial % 3 == 0)
            for (uint32_t i = 0; i < 128; i++)
                views.push_back({3968 + i});
        Require(Visit(candidate, views, true) == Visit(reference, views, false));
    }
    views = {{UINT32_MAX}, {0}, {0}, {4096}, {4095}};
    Require(Visit(candidate, views, true) == Visit(reference, views, false));
    Require(reference.Synchronize(64, 15));
    Require(candidate.Synchronize(64, 15));
    views.clear();
    for (uint32_t i = 0; i < 20; i++)
        views.push_back({i});
    Require(Visit(candidate, views, true) == Visit(reference, views, false));
    if (argc == 1)
        return 0;
    puts("pattern,batch,reads_per_visit,p50_us,p95_us,p99_us,checksum");
    for (bool dense : {true, false})
        for (bool batch : {false, true}) {
            Table table{memory};
            (void)table.Synchronize(64, 4095);
            views.clear();
            std::mt19937 fixture_random{43};
            for (uint32_t i = 0; i < 2048; i++)
                views.push_back({dense ? i : fixture_random() % 4096});
            std::vector<double> times;
            uint64_t checksum{};
            memory.reads = 0;
            for (int iteration = 0; iteration < 1000; iteration++) {
                auto begin = std::chrono::steady_clock::now();
                VideoCommon::VisitDescriptorRuns<Descriptor>(
                    std::span{views}, table.Limit(), batch,
                    [&](uint32_t first, std::span<Descriptor> descriptors) {
                        table.ReadRange(first, descriptors);
                    },
                    [&](View& view, const Descriptor* snapshot) {
                        auto value = table.Read(view.index, snapshot);
                        checksum += value.first.words[0] + value.second;
                    });
                times.push_back(std::chrono::duration<double, std::micro>(
                                    std::chrono::steady_clock::now() - begin)
                                    .count());
            }
            std::sort(times.begin(), times.end());
            printf("%s,%d,%.1f,%.3f,%.3f,%.3f,%llu\n", dense ? "dense" : "sparse", batch,
                   memory.reads / 1000.0, times[500], times[950], times[990],
                   static_cast<unsigned long long>(checksum));
        }
}
