// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "yuzu_common/common_types.h"

namespace Shader {

// Maxwell dithers alpha-to-coverage over a 2x2 pixel footprint. With a single sample the host
// can only keep or drop the whole pixel, so the fragment shader drops pixels in this pattern.
// Bit (level * 4 + (y & 1) * 2 + (x & 1)) keeps the pixel, level being alpha in eighths
// (pattern from Ryujinx): 0, 1, 1, 2, 2, 3, 3 and 4 pixels of the 2x2 footprint are kept.
constexpr u32 ALPHA_TO_COVERAGE_DITHER_MASK = 0xfbb99110U;

constexpr u32 AlphaToCoverageDitherLevel(float alpha) {
    // A NaN alpha is treated as zero, as the hardware saturate does
    const float saturated = alpha > 0.0f ? (alpha < 1.0f ? alpha : 1.0f) : 0.0f;
    const u32 level = static_cast<u32>(saturated * 8.0f);
    return level < 7 ? level : 7;
}

constexpr bool AlphaToCoverageDitherKeeps(float alpha, u32 x, u32 y) {
    const u32 shift = (AlphaToCoverageDitherLevel(alpha) << 2) | ((y & 1) << 1) | (x & 1);
    return ((ALPHA_TO_COVERAGE_DITHER_MASK >> shift) & 1) != 0;
}

} // namespace Shader
