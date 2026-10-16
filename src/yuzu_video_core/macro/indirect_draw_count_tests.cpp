// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>
#include <limits>
#include "indirect_draw_count.h"

// Protect the correctness boundaries of the CPU-known count optimization:
// stale counts and nonzero guest draw IDs must retain the GPU count path.
static_assert(!Tegra::KnownMultiDrawCount(true, 0, 100, 2));
static_assert(!Tegra::KnownMultiDrawCount(true, 0, 100, 0));
static_assert(!Tegra::KnownMultiDrawCount(false, 1, 100, 2));
static_assert(Tegra::KnownMultiDrawCount(false, 0, 100, 2) == 2);
static_assert(Tegra::KnownMultiDrawCount(false, 0, 100, 200) == 100);
static_assert(Tegra::KnownMultiDrawCount(false, 0, 100, 0) == 0);
static_assert(Tegra::KnownMultiDrawCount(false, 0, 0, 100) == 0);
static_assert(Tegra::KnownMultiDrawCount(false, 0, std::numeric_limits<std::uint32_t>::max(),
                                       std::numeric_limits<std::uint32_t>::max()) ==
              std::numeric_limits<std::uint32_t>::max());

int main() {}
