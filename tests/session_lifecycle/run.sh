#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/session_lifecycle"
mkdir -p "$OUT"
clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
    -I"$ROOT/src" "$ROOT/tests/session_lifecycle/test_session_lifecycle.cpp" \
    "$ROOT/src/yuzu_video_core/host1x/syncpoint_manager.cpp" -o "$OUT/test_session_lifecycle"
"$OUT/test_session_lifecycle"
