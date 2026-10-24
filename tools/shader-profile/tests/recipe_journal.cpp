// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/shader_profile_journal.h"
#include <iostream>

using namespace VideoCommon::ShaderProfile::Journal;
void Check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) try {
  Check(argc == 2, "Expected temporary journal path");
  const auto path = std::filesystem::u8path(argv[1]);
  std::filesystem::remove(path);
  Entry a{};
  a[0] = 63;
  a[1] = 1;
  Append(path, a);
  auto b = a;
  b[0] = 0; // A different source GPU must retain its projection inputs.
  Append(path, b);
  auto c = a;
  c[12] = 123; // Guest combination, even if the host key is shared.
  Append(path, c);
  auto saved = Read(path);
  Check(saved.entries.size() == 3 && !saved.incomplete_tail,
        "Recipe combination lost");
  auto batch_path=path;batch_path+=".batch";
  const std::array entries{a,b,c};WriteMetrics metrics;
  AppendBatch(batch_path,entries,&metrics);
  const auto read_all=[](const auto& p){std::ifstream in(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
  Check(read_all(path)==read_all(batch_path) && metrics.frames==3 && metrics.batches==1,
        "Batch changed checked frame bytes or counters");
  std::filesystem::remove(batch_path);
  AppendBatch(batch_path,{},nullptr);
  Check(!std::filesystem::exists(batch_path),"Empty batch created a journal");
  {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << "torn frame";
  }
  saved = Read(path);
  Check(saved.entries.size() == 3 && saved.incomplete_tail,
        "Crash lost committed entries");
  std::filesystem::resize_file(path, saved.valid_bytes);
  Check(!Read(path).incomplete_tail, "Tail recovery failed");
  {
    std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
    io.seekp(Magic.size() + 100);
    io.put('x');
  }
  bool rejected = false;
  try {
    Read(path);
  } catch (const std::exception &) {
    rejected = true;
  }
  Check(rejected, "Corrupted recipe was accepted");
  std::filesystem::remove(path);
  std::cout << "PASS: cross-GPU combinations, restart, torn append recovery, "
               "corruption rejection\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
