// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/pipeline_cache_store.h"
#include <chrono>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
using namespace VideoCommon;
void Put(std::string &bytes, size_t off, u64 value, size_t count) {
  for (size_t i = 0; i < count; ++i)
    bytes.at(off + i) = static_cast<char>(value >> (i * 8));
}
int main(int argc, char **argv) try {
  if (argc < 3)
    throw std::runtime_error(
        "usage: cache-store-benchmark generate|read path [MiB]");
  const std::filesystem::path path = argv[2];
  const auto started = std::chrono::steady_clock::now();
  size_t count{};
  if (std::string_view(argv[1]) == "generate") {
    if (argc != 4 || std::filesystem::exists(path))
      throw std::runtime_error("fresh output required");
    const auto target = std::stoull(argv[3]) * 1024 * 1024;
    constexpr size_t CodeSize = 256 * 1024;
    for (size_t i = 0; i < target / CodeSize; ++i) {
      std::string env(68 + CodeSize + 16, '\0');
      Put(env, 0, CodeSize, 8);
      Put(env, 64, 5, 4);
      Put(env, 68, i + 1,
          8); // Synthetic instructions, unique program per frame.
      CacheStore::Record r{{std::move(env)}, std::string(64, '\0')};
      Put(r.key, 0, i + 1, 8);
      CacheStore::Append(path, 20, r);
      ++count;
    }
  } else if (std::string_view(argv[1]) == "read") {
    const auto records = CacheStore::Read(path, 20);
    count = records.size();
  } else
    throw std::runtime_error("unknown mode");
  u64 peak{};
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS memory{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
    peak = memory.PeakWorkingSetSize;
#else
  rusage memory{};
  if (getrusage(RUSAGE_SELF, &memory) == 0) {
#ifdef __APPLE__
    peak = memory.ru_maxrss;
#else
    peak = memory.ru_maxrss * 1024ULL;
#endif
  }
#endif
  const auto seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  std::cout << "{\"records\":" << count
            << ",\"file_bytes\":" << std::filesystem::file_size(path)
            << ",\"seconds\":" << seconds << ",\"peak_resident_bytes\":" << peak
            << "}\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
