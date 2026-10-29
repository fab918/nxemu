#pragma once

#include <cstring>
#include "yuzu_common/common_types.h"

namespace Core {

// Calls func(offset, size) for each run of 4-byte words that differ between a guest page and the
// snapshot taken before it became writable. The page is scanned in 64-byte blocks, each run is
// then trimmed to its first and last changed word.
template <typename Func>
void ForEachNceWrittenRange(const u8 * current, const u8 * snapshot, u64 page_size, Func && func)
{
    constexpr u64 granularity = 64;
    constexpr u64 word = 4;
    for (u64 offset = 0; offset < page_size;)
    {
        if (std::memcmp(current + offset, snapshot + offset, granularity) == 0)
        {
            offset += granularity;
            continue;
        }
        u64 begin = offset;
        do {
            offset += granularity;
        } while (offset < page_size && std::memcmp(current + offset, snapshot + offset, granularity) != 0);
        u64 end = offset;
        while (std::memcmp(current + begin, snapshot + begin, word) == 0) begin += word;
        while (std::memcmp(current + end - word, snapshot + end - word, word) == 0) end -= word;
        func(begin, end - begin);
    }
}

} // namespace Core
