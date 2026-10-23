// SPDX-License-Identifier: GPL-2.0-or-later
// Offline projection adapter for the isolated reconstruction experiment.
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "video_settings.h"
#include "yuzu_common/logging/log.h"
#include "yuzu_video_core/renderer_vulkan/fixed_pipeline_state.h"

VideoSettings videoSettings{};
namespace Common::Log {
LogLevel g_classLevel[static_cast<uint8_t>(LogClass::Count)]{};
void FmtLogMessageImpl(LogClass, LogLevel, const char*, unsigned int, const char*,
                       const char*, const fmt::format_args&) {}
}
void assert_fail_impl() { throw std::runtime_error("renderer assertion"); }
[[noreturn]] void unreachable_impl() { throw std::runtime_error("renderer unreachable"); }

int main(int argc, char** argv) try {
    if (argc != 4) throw std::runtime_error("usage: recipe-projection input output feature-bits");
    std::ifstream input(argv[1], std::ios::binary);
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    input.exceptions(std::ios::badbit | std::ios::failbit);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    const unsigned bits = std::stoul(argv[3]);
    if (bits > 127) throw std::runtime_error("invalid feature bits");
    const Vulkan::DynamicFeatures features{bool(bits&1), bool(bits&2), bool(bits&4),
        bool(bits&8), bool(bits&16), bool(bits&32), bool(bits&64)};
    u32 count{};
    input.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (count > 16384) throw std::runtime_error("too many recipes");
    for (u32 i=0; i<count; ++i) {
        u32 size{};
        input.read(reinterpret_cast<char*>(&size), sizeof(size));
        if (size != Vulkan::GuestPipelineState{}.Words().size()) throw std::runtime_error("bad word count");
        std::vector<u32> words(size);
        input.read(reinterpret_cast<char*>(words.data()), size*sizeof(u32));
        const auto guest = Vulkan::GuestPipelineState::FromWords(words);
        if (!guest) throw std::runtime_error("invalid guest state");
        const auto state = guest->Project(features);
        const auto effective = static_cast<u32>(state.Size());
        output.write(reinterpret_cast<const char*>(&effective), sizeof(effective));
        output.write(reinterpret_cast<const char*>(&state), sizeof(state));
    }
    output.flush();
    std::cout << "{\"count\":" << count << ",\"state_bytes\":"
              << sizeof(Vulkan::FixedPipelineState) << ",\"feature_bits\":" << bits << "}\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
