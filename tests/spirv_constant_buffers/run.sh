#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build}"
mkdir -p "$BUILD/spirv_constant_buffers"
clang++ -std=c++20 -O1 -g -I"$ROOT/src" -I"$ROOT/external/boost" \
    -I"$ROOT/external/fmt/include" -I"$ROOT/external/sirit/include" \
    -I"$ROOT/external/sirit/externals/SPIRV-Headers/include" \
    "$ROOT/tests/spirv_constant_buffers/layout.cpp" "$ROOT/tests/spirv_constant_buffers/support_stubs.cpp" \
    "$BUILD/src/yuzu_shader_recompiler/libshader_recompiler.a" \
    "$BUILD/external/sirit/src/libsirit.a" "$BUILD/external/fmt/libfmt.a" \
    "$BUILD/src/common/libcommon.a" -o "$BUILD/spirv_constant_buffers/layout"
"$BUILD/spirv_constant_buffers/layout"
