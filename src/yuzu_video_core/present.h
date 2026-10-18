// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cmath>

#include "yuzu_common/math_util.h"
#include "yuzu_common/settings.h"
#include "nxemu-video/video_settings.h"
#include "nxemu-module-spec/fsr_sharpness.h"
#include "yuzu_video_core/fsr_diagnostics.h"

static inline ScalingFilter GetScalingFilter() {
    if (videoSettings.cas_sharpening_slider > 0) {
        switch (videoSettings.scaling_filter) {
        case ScalingFilter::NearestNeighbor: return ScalingFilter::CasNearest;
        case ScalingFilter::Bilinear: return ScalingFilter::CasBilinear;
        case ScalingFilter::CatmullRom: return ScalingFilter::CasCatmullRom;
        case ScalingFilter::CatmullRomFast: return ScalingFilter::CasCatmullRomFast;
        default: break;
        }
    }
    if (VideoCore::LegacyFsr() && videoSettings.scaling_filter == ScalingFilter::FsrEasu) {
        return ScalingFilter::Fsr;
    }
    // A true sharpening-off setting must not execute the RCAS shader.
    if (!VideoCore::LegacyFsr() && videoSettings.scaling_filter == ScalingFilter::Fsr &&
        videoSettings.fsr_sharpening_slider >= FsrSharpness::StoredMaximum) {
        return ScalingFilter::FsrEasu;
    }
    return videoSettings.scaling_filter;
}

static inline AntiAliasing GetAntiAliasing() {
    return videoSettings.anti_aliasing;
}

static inline ScalingFilter GetScalingFilterForAppletCapture() {
    return ScalingFilter::Bilinear;
}

static inline AntiAliasing GetAntiAliasingForAppletCapture() {
    return AntiAliasing::None;
}

struct PresentFilters {
    ScalingFilter (*get_scaling_filter)();
    AntiAliasing (*get_anti_aliasing)();
    bool is_applet_capture{};
};

constexpr PresentFilters PresentFiltersForDisplay{
    .get_scaling_filter = &GetScalingFilter,
    .get_anti_aliasing = &GetAntiAliasing,
};

constexpr PresentFilters PresentFiltersForAppletCapture{
    .get_scaling_filter = &GetScalingFilterForAppletCapture,
    .get_anti_aliasing = &GetAntiAliasingForAppletCapture,
    .is_applet_capture = true,
};

// RCAS can sample the source directly only when its texel coordinates match
// the full output one-to-one. Full flips are safe: RCAS is symmetric and the
// presentation vertices perform the flip. Partial crops still require EASU.
static inline bool CanUseNativeRcas(u32 input_width, u32 input_height,
                                    const Common::Rectangle<f32>& crop,
                                    u32 output_width, u32 output_height) {
    constexpr f32 epsilon = 0.000001f;
    const bool full_x = (std::abs(crop.left) < epsilon &&
                         std::abs(crop.right - 1.0f) < epsilon) ||
                        (std::abs(crop.right) < epsilon &&
                         std::abs(crop.left - 1.0f) < epsilon);
    const bool full_y = (std::abs(crop.top) < epsilon &&
                         std::abs(crop.bottom - 1.0f) < epsilon) ||
                        (std::abs(crop.bottom) < epsilon &&
                         std::abs(crop.top - 1.0f) < epsilon);
    return input_width != 0 && input_height != 0 && input_width == output_width &&
           input_height == output_height && full_x && full_y;
}
