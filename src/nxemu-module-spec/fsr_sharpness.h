#pragma once

#include <algorithm>

// Keep the saved RCAS attenuation (0..200) unchanged for existing configurations,
// game overrides and status overrides. Only the UI percentage is reversed.
namespace FsrSharpness {
constexpr int DisplayMaximum = 100;
constexpr int StoredMaximum = 200;

constexpr int ToDisplay(int stored) {
    return (StoredMaximum - std::clamp(stored, 0, StoredMaximum) + 1) / 2;
}

constexpr int ToStored(int display) {
    return StoredMaximum - 2 * std::clamp(display, 0, DisplayMaximum);
}
} // namespace FsrSharpness
