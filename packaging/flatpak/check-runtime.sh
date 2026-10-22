#!/bin/sh
set -eu
test "${FLATPAK_ID:-}" = com.nxemu.NxEmu
test -x /app/bin/nxemu
test -s /app/lib/nxemu/nxemu.lang
test -s /app/lib/nxemu/ui/main_window.rml
for binary in /app/lib/nxemu/nxemu \
    /app/lib/nxemu/modules/loader/libnxemu-loader.so \
    /app/lib/nxemu/modules/cpu/libnxemu-cpu.so \
    /app/lib/nxemu/modules/video/libnxemu-video.so \
    /app/lib/nxemu/modules/operating_system/libnxemu-os.so; do
    test -s "$binary"
    dependencies=$(ldd "$binary")
    printf '%s\n' "$dependencies"
    case "$dependencies" in
        *'not found'*) echo "Missing dependency: $binary" >&2; exit 1 ;;
    esac
done
echo 'Flatpak resources, modules and runtime dependencies are present.'
