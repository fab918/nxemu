// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/renderer_vulkan/startup_pipeline_variants.h"
#include "yuzu_video_core/shader_profile_identity.h"
#include "yuzu_video_core/shader_profile_audit.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void Check(bool ok) {
  if (!ok)
    throw std::runtime_error("Startup variant safety regression");
}
int main() try {
  // Model texture answers, constant values and HLE replacements as separate
  // compiler inputs. Every conflicting order must quarantine the entire key.
  for (const auto &changed :
       {"texture-type", "constant-value", "hle-replacement"}) {
    for (bool reverse : {false, true}) {
      Vulkan::StartupPipelineVariants<int, std::string> variants;
      const auto equal = [](const auto &a, const auto &b) { return a == b; };
      variants.Observe(1, reverse ? changed : "original", equal);
      variants.Observe(1, reverse ? "original" : changed, equal);
      variants.Observe(1, "original", equal);
      variants.Observe(2, "safe", equal);
      variants.Observe(2, "safe", equal);
      size_t built{};
      const auto rejected =
          variants.ConsumeSafe([&](int key, std::string input) {
            Check(key == 2 && input == "safe");
            ++built;
          });
      Check(built == 1 && rejected == 1);
    }
  }
  // Equal generated code alone cannot prove equivalent binding metadata.
  VideoCommon::ShaderProfile::Audit::Capture first, second;
  first.spirv = second.spirv = {"same-code-hash"};
  first.interfaces.emplace_back(0, Shader::Info{});
  second.interfaces = first.interfaces;
  Check(first == second);
  second.interfaces[0].second.constant_buffer_used_sizes[0] = 16;
  Check(first != second);
  second = first;
  second.interfaces[0].second.loads.mask.set(1);
  Check(first != second);
  second = first;
  second.rasterizes_lines = true;
  Check(first != second);
  using VideoCommon::ShaderProfile::ImportIdentity;
  const auto stamp = ImportIdentity("profile", "base+update-v1", 1, 63, 20);
  Check(!stamp.empty());
  Check(stamp != ImportIdentity("profile", "base+update-v2", 1, 63, 20));
  Check(stamp != ImportIdentity("profile", "base+update-v1", 2, 63, 20));
  Check(stamp != ImportIdentity("profile", "base+update-v1", 1, 31, 20));
  Check(stamp != ImportIdentity("profile", "base+update-v1", 1, 63, 21));
  Check(stamp != ImportIdentity("other-profile", "base+update-v1", 1, 63, 20));
  Check(ImportIdentity("profile", "", 1, 63, 20).empty());
  std::cout << "PASS: ambiguous inputs never win by order; import identity "
               "isolates content, title, GPU, ABI and profile\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
