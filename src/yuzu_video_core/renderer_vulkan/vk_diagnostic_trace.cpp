// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/renderer_vulkan/vk_diagnostic_trace.h"

#ifdef _WIN32
#include <atomic>
#include <cstdio>
#include <new>
#include <windows.h>

namespace Vulkan::Diagnostic {
namespace {
constexpr std::uint32_t Capacity = 65536;
constexpr std::uint64_t Magic = 0x313052544b56584eULL; // NXVKTR01, little endian
struct alignas(64) Header {
    std::uint64_t magic;
    std::uint32_t version;
    std::uint32_t record_size;
    std::uint32_t capacity;
    std::uint32_t header_size;
    std::uint64_t frequency;
    std::atomic<std::uint64_t> next;
    std::atomic<std::uint64_t> dropped;
    std::uint64_t reserved[2];
};
struct alignas(64) Entry {
    std::atomic<std::uint64_t> sequence;
    std::atomic<std::uint32_t> writing;
    std::uint32_t event;
    std::uint64_t timestamp;
    std::uint32_t thread;
    std::uint32_t reserved;
    std::uint64_t args[12];
};
static_assert(sizeof(Header) == 64 && sizeof(Entry) == 128);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
constexpr std::size_t MappingSize = sizeof(Header) + Capacity * sizeof(Entry);

struct Trace {
    HANDLE mapping{};
    Header* header{};
    Trace() noexcept {
        wchar_t enabled[4]{};
        if (GetEnvironmentVariableW(L"NXEMU_VK_TRACE", enabled, 4) != 1 || enabled[0] != L'1') {
            return;
        }
        wchar_t name[80]{};
        swprintf_s(name, L"Local\\NXEmuVkTrace-%lu", GetCurrentProcessId());
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                     static_cast<DWORD>(MappingSize), name);
        if (!mapping) {
            return;
        }
        const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
        header = static_cast<Header*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, MappingSize));
        if (!header) {
            return;
        }
        if (!existed) {
            // The mapping is zero-filled; initialize atomics and touch pages once.
            new (header) Header{};
            auto* entries = reinterpret_cast<Entry*>(header + 1);
            for (std::size_t i = 0; i < Capacity; ++i) {
                new (&entries[i]) Entry{};
            }
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            header->version = 1;
            header->record_size = sizeof(Entry);
            header->capacity = Capacity;
            header->header_size = sizeof(Header);
            header->frequency = frequency.QuadPart;
            // Publish only after initialization; an external reader may open early.
            InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&header->magic), Magic);
        } else if (header->magic != Magic || header->version != 1 ||
                   header->record_size != sizeof(Entry) || header->capacity != Capacity) {
            UnmapViewOfFile(header);
            header = nullptr;
        }
    }
    ~Trace() {
        if (header) UnmapViewOfFile(header);
        if (mapping) CloseHandle(mapping);
    }
};
} // namespace

void Record(Event event, std::uint64_t a, std::uint64_t b, std::uint64_t c,
            std::uint64_t d, std::uint64_t e, std::uint64_t f, std::uint64_t g,
            std::uint64_t h) noexcept {
    static Trace trace;
    Header* const header = trace.header;
    if (!header) return;
    const auto sequence = header->next.fetch_add(1, std::memory_order_relaxed);
    auto& entry = reinterpret_cast<Entry*>(header + 1)[sequence % Capacity];
    if (entry.writing.exchange(1, std::memory_order_acquire)) {
        header->dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // A delayed writer must not replace a newer lap of the ring.
    if (entry.sequence.load(std::memory_order_relaxed) > sequence + 1) {
        header->dropped.fetch_add(1, std::memory_order_relaxed);
        entry.writing.store(0, std::memory_order_release);
        return;
    }
    entry.sequence.store(0, std::memory_order_relaxed);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    entry.event = static_cast<std::uint32_t>(event);
    entry.timestamp = now.QuadPart;
    entry.thread = GetCurrentThreadId();
    entry.args[0] = a; entry.args[1] = b; entry.args[2] = c; entry.args[3] = d;
    entry.args[4] = e; entry.args[5] = f; entry.args[6] = g; entry.args[7] = h;
    entry.sequence.store(sequence + 1, std::memory_order_release);
    entry.writing.store(0, std::memory_order_release);
}
} // namespace Vulkan::Diagnostic
#else
namespace Vulkan::Diagnostic {
void Record(Event, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
            std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept {}
}
#endif
