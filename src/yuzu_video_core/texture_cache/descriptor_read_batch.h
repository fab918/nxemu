// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace VideoCommon {

// Snapshot only a bounded, consecutive run for this visit. Never keep guest
// descriptor bytes across draws: guest memory may have changed in between.
template <typename Descriptor, typename View, typename Read, typename Visit>
void VisitDescriptorRuns(std::span<View> views, uint32_t limit, bool batch, Read&& read,
                         Visit&& visit) {
    if (!batch || views.size() < 16) {
        for (auto& view : views) {
            visit(view, static_cast<const Descriptor*>(nullptr));
        }
        return;
    }
    std::array<Descriptor, 128> descriptors;
    size_t cursor{};
    while (cursor < views.size()) {
        size_t count = 1;
        const uint32_t first = views[cursor].index;
        // Probe once per small group before searching for a long run. Sparse
        // bindings keep their scalar path without a run search for every view.
        if (views.size() - cursor < 16 ||
            static_cast<uint64_t>(first) + 15 > limit ||
            views[cursor + 15].index != static_cast<uint64_t>(first) + 15) {
            const size_t end = cursor + std::min<size_t>(16, views.size() - cursor);
            for (; cursor < end; ++cursor) {
                visit(views[cursor], static_cast<const Descriptor*>(nullptr));
            }
            continue;
        }
        if (batch && first <= limit) {
            while (count < descriptors.size() && count < views.size() - cursor &&
                   count <= static_cast<uint64_t>(limit) - first &&
                   views[cursor + count].index == static_cast<uint64_t>(first) + count) {
                ++count;
            }
        }
        if (count >= 16) {
            read(first, std::span{descriptors}.first(count));
            for (size_t index = 0; index < count; ++index) {
                visit(views[cursor + index], &descriptors[index]);
            }
        } else {
            for (size_t index = 0; index < count; ++index) {
                visit(views[cursor + index], static_cast<const Descriptor*>(nullptr));
            }
        }
        cursor += count;
    }
}
} // namespace VideoCommon
