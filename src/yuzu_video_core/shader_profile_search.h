// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>

namespace VideoCommon::ShaderProfile {
// Offline discovery only. This indexes prefixes, never pipeline combinations.
// Callers must compare the complete program before recording a resource locator.
class PrefixSearch {
public:
    static constexpr size_t Width = 16;
    using Prefix = std::array<unsigned char, Width>;
    explicit PrefixSearch(std::span<const Prefix> patterns) {
        if (patterns.empty()) throw std::invalid_argument("Empty shader prefix index");
        shifts.fill(Width - 1);
        for (const auto& pattern : patterns) {
            first_bytes[pattern[0]] = true;
            for (size_t i = 0; i + 1 < Width; ++i) {
                auto& shift = shifts[Pair(pattern.data() + i)];
                shift = std::min(shift, static_cast<unsigned char>(Width - 2 - i));
            }
            auto& bucket = buckets[Pair(pattern.data() + Width - 2)];
            if (std::find(bucket.begin(), bucket.end(), pattern) == bucket.end())
                bucket.push_back(pattern);
        }
    }

    // Positions are ascending and non-overlapping, like the original prefix regex.
    // Bounded batches let callers check full programs without retaining all hits.
    // Resume and data refer to the same input buffer; no bytes are read past it.
    size_t Scan(std::span<const unsigned char> data, size_t& resume,
                std::span<size_t> positions) const {
        size_t count = 0;
        if (positions.empty()) return count;
        while (resume <= data.size() && data.size() - resume >= Width) {
            // Containers contain large zero-filled areas. Skip a word only when
            // zero cannot start any pattern, preserving every possible start.
            if (!first_bytes[0] && data[resume] == 0) {
                std::uint64_t word;
                std::memcpy(&word, data.data() + resume, sizeof(word));
                if (word == 0) { resume += sizeof(word); continue; }
            }
            const auto pair = Pair(data.data() + resume + Width - 2);
            if (const auto shift = shifts[pair]) {
                resume += shift;
                continue;
            }
            const auto& bucket = buckets.at(pair);
            const bool found = first_bytes[data[resume]] && std::any_of(bucket.begin(), bucket.end(), [&](const Prefix& p) {
                return std::memcmp(data.data() + resume, p.data(), Width) == 0;
            });
            if (found) {
                positions[count++] = resume;
                resume += Width;
                if (count == positions.size()) return count;
            } else {
                ++resume;
            }
        }
        resume = data.size();
        return count;
    }

private:
    static unsigned Pair(const unsigned char* p) { return (unsigned(p[0]) << 8) | p[1]; }
    std::array<unsigned char, 65536> shifts{};
    std::array<bool, 256> first_bytes{};
    std::map<unsigned, std::vector<Prefix>> buckets;
};
} // namespace VideoCommon::ShaderProfile
