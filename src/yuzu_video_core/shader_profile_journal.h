// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yuzu_common/common_types.h"
#include "yuzu_video_core/shader_profile_hash.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace VideoCommon::ShaderProfile::Journal
{
// Local metadata only. Each independently checked frame is committed by its trailing
// digest. A torn final frame can be discarded without losing preceding sessions.
using Entry = std::array<u32, 383>; // source feature bits, 278 guest + 104 key words
inline constexpr std::string_view Magic = "NXREC001";
inline constexpr size_t FrameBytes = 383 * 4 + 64;
inline constexpr size_t MaxEntries = 16384;
struct Contents
{
    std::set<Entry> entries;
    size_t valid_bytes{Magic.size()};
    bool incomplete_tail{};
};
inline std::string Encode(const Entry & entry)
{
    std::string bytes;
    bytes.reserve(FrameBytes);
    for (u32 word : entry)
        for (unsigned shift = 0; shift != 32; shift += 8)
            bytes.push_back(static_cast<char>(word >> shift));
    bytes += ResourceSha256(bytes);
    return bytes;
}
inline Contents Read(const std::filesystem::path & path)
{
    Contents result;
    if (!std::filesystem::exists(path)) return result;
    const auto size = std::filesystem::file_size(path);
    if (size > Magic.size() + MaxEntries * FrameBytes)
        throw std::runtime_error("Guest recipe journal exceeds limit");
    std::ifstream in(path, std::ios::binary);
    std::string magic(Magic.size(), '\0');
    if (!in.read(magic.data(), magic.size()) || magic != Magic)
        throw std::runtime_error("Invalid guest recipe journal header");
    std::string frame(FrameBytes, '\0');
    while (in.read(frame.data(), frame.size()))
    {
        if (ResourceSha256(std::string_view(frame).substr(0, 383 * 4)) != frame.substr(383 * 4))
            throw std::runtime_error("Corrupt guest recipe journal frame");
        Entry entry{};
        for (size_t i = 0; i < entry.size(); ++i)
            for (unsigned j = 0; j != 4; ++j)
                entry[i] |= u32(static_cast<unsigned char>(frame[i * 4 + j])) << (j * 8);
        if (entry[0] > 127 || entry[1] != 1)
            throw std::runtime_error("Unsupported guest recipe journal ABI");
        result.entries.insert(entry);
        result.valid_bytes += FrameBytes;
    }
    if (!in.eof()) throw std::runtime_error("Cannot read guest recipe journal");
    result.incomplete_tail = size != result.valid_bytes;
    return result;
}
struct WriteMetrics
{
    u64 frames{}, bytes{}, encoding_ns{}, io_ns{}, cpu_ns{}, cpu_samples{}, cpu_cycles{}, cycle_samples{};
};
// Called only by the existing cache serialization worker, never on a draw.
inline void Append(const std::filesystem::path & path, const Entry & entry, WriteMetrics * metrics = nullptr)
{
    const auto started = metrics ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const bool fresh = !std::filesystem::exists(path);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (fresh) out.write(Magic.data(), Magic.size());
    const auto encode_start = metrics ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto bytes = Encode(entry);
    const auto encoded = metrics ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    out.write(bytes.data(), bytes.size());
    out.flush();
    out.close();
    if (!out) throw std::runtime_error("Cannot append guest recipe journal");
    if (metrics)
    {
        const auto finished = std::chrono::steady_clock::now();
        const auto ns = [](auto duration) { return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(); };
        ++metrics->frames;
        metrics->bytes += bytes.size() + (fresh ? Magic.size() : 0);
        metrics->encoding_ns += ns(encoded - encode_start);
        metrics->io_ns += ns(encode_start - started) + ns(finished - encoded);
    }
}
} // namespace VideoCommon::ShaderProfile::Journal
