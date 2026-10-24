// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace Vulkan::Diagnostic {

// Append-only IDs: tools/vulkan-diagnostic/decode_trace.py decodes this format.
enum class Event : std::uint32_t {
    Attach = 1, DisplayCreate, DisplayDestroy, TimelineCreate, TimelineDestroy,
    TimelineWaitBegin, TimelineWaitEnd, SubmitBegin, SubmitEnd,
    QueueLockBegin, QueueLockEnd, InterfaceWaitBegin, InterfaceWaitEnd,
    InterfaceSubmitBegin, InterfaceSubmitEnd, AcquireBegin, AcquireEnd,
    PresentSubmitBegin, PresentSubmitEnd, PresentBegin, PresentEnd,
    FrameWaitBegin, FrameWaitEnd, OwnPresentWaitBegin, OwnPresentWaitEnd,
    SwapchainCreate, RendererCreate, RendererDestroy, CompositeBegin, CompositeEnd,
    DeviceIdleBegin, DeviceIdleEnd, ResourceWaitBegin, ResourceWaitEnd,
};

template <typename T>
std::uint64_t Id(T value) noexcept {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<std::uintptr_t>(value);
    } else {
        return static_cast<std::uint64_t>(value);
    }
}

// Enabled only by NXEMU_VK_TRACE=1. No disk IO, GPU queries, extra synchronization
// commands, or waits. Shared memory survives video DLL reloads while the launcher
// holds its mapping open. Writers drop a record rather than wait on contention.
void Record(Event event, std::uint64_t a = 0, std::uint64_t b = 0,
            std::uint64_t c = 0, std::uint64_t d = 0, std::uint64_t e = 0,
            std::uint64_t f = 0, std::uint64_t g = 0, std::uint64_t h = 0) noexcept;


} // namespace Vulkan::Diagnostic
