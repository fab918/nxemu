// SPDX-License-Identifier: GPL-2.0-or-later
#include "descriptor_copy_cache.h"
#include <array>
#include <cstdlib>

static void Require(bool value) {
    if (!value)
        std::abort();
}
int main() {
    Vulkan::DescriptorCopyCache<unsigned, 2, 32> cache;
    std::array<std::byte, 16> a{}, b{};
    b[0] = std::byte{1};
    cache.Begin(1);
    Require(!cache.Find(3, a));
    cache.Store(3, a, {10, 7});
    auto source = cache.Find(3, a);
    Require(source && source->set == 10 && source->binding == 7);
    Require(!cache.Find(2, a));
    Require(!cache.Find(3, std::span{a}.first(8)));
    cache.Begin(1);
    Require(cache.Find(3, a).has_value());
    cache.Store(3, b, {11, 8});
    b[0] = std::byte{2};
    Require(!cache.Find(3, b)); // The cache owns its snapshot.
    cache.Store(3, b, {12, 9});
    Require(!cache.Find(3, a)); // Bounded FIFO eviction.
    std::array<std::byte, 33> too_large{};
    cache.Store(3, too_large, {13, 0});
    Require(!cache.Find(3, too_large));
    cache.Begin(2);
    Require(!cache.Find(3, b)); // Recycled handles from prior ticks cannot hit.
    cache.Store(3, b, {10, 0});
    Require(cache.Find(3, b)->binding == 0);
    for (unsigned i = 3; i < 10000; ++i) {
        cache.Begin(i);
        Require(!cache.Find(3, b));
        cache.Store(3, b, {10, i});
        Require(cache.Find(3, b)->binding == i);
    }
}
