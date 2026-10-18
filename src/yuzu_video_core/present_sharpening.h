// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "nxemu-module-spec/fsr_sharpness.h"
#include "nxemu-module-spec/video.h"

enum class PresentSharpening
{
    None,
    Cas,
    Rcas
};

// EASU-only explicitly disables RCAS. FSR uses its own strength rather than
// stacking CAS on top. Other filters share the independent CAS control.
constexpr PresentSharpening GetPresentSharpening(ScalingFilter requested, int cas, int fsr)
{
    if (requested == ScalingFilter::FsrEasu) return PresentSharpening::None;
    if (requested == ScalingFilter::Fsr)
    {
        return fsr < FsrSharpness::StoredMaximum ? PresentSharpening::Rcas
                                                 : PresentSharpening::None;
    }
    return cas > 0 ? PresentSharpening::Cas : PresentSharpening::None;
}
