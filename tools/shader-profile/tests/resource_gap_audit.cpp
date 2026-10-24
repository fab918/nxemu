// SPDX-License-Identifier: GPL-2.0-or-later
// Offline diagnostic only. Reuses the logical storage walkers, and emits no
// code.
#include "video_settings.h"
#include "yuzu_common/logging/log.h"
#include "yuzu_video_core/pipeline_cache_store.h"
#include "yuzu_video_core/shader_profile_discovery.cpp"
#include <iostream>
VideoSettings videoSettings{};
namespace Common::Log {
LogLevel g_classLevel[static_cast<uint8_t>(LogClass::Count)]{};
void FmtLogMessageImpl(LogClass, LogLevel, const char *, unsigned, const char *,
                       const char *, const fmt::format_args &) {}
} // namespace Common::Log
[[noreturn]] void assert_fail_impl() {
  throw std::runtime_error("renderer assertion");
}
[[noreturn]] void unreachable_impl() {
  throw std::runtime_error("renderer unreachable");
}

namespace VideoCommon::ShaderProfile {
int AuditGaps(int argc, char **argv) {
  Check(argc >= 6,
        "usage: resource-gap-audit cache root base update|- shader-sha...");
  std::map<std::string, std::string> targets;
  for (int i = 5; i < argc; ++i)
    targets.emplace(argv[i], "");
  for (const auto &r : CacheStore::Read(argv[1], 20))
    for (const auto &env : r.environments) {
      const auto code = std::string_view(env).substr(68, Get(env, 0, 8));
      const auto hash = ResourceSha256(code);
      if (targets.contains(hash))
        targets[hash] = code;
    }
  std::map<std::string, std::vector<std::pair<std::string, size_t>>> anchors;
  for (const auto &[h, c] : targets) {
    Check(c.size() >= 112, "Missing graphics target");
    anchors[c.substr(0, 16)].emplace_back(h, 0);
    anchors[c.substr(80, 16)].emplace_back(h, 80);
  }
  std::vector<PrefixSearch::Prefix> prefixes;
  for (const auto &[p, entries] : anchors) {
    PrefixSearch::Prefix a;
    std::memcpy(a.data(), p.data(), 16);
    prefixes.push_back(a);
  }
  PrefixSearch search(prefixes);
  struct Match {
    size_t differences{}, prefix{};
    JsonValue result;
  };
  std::map<std::string, std::map<std::string, Match>> best;
  size_t resources{}, candidates{};
  u64 decoded_bytes{};
  const auto scan = [&](const std::string &data, const std::string &id,
                        const JsonValue &spans) {
    ++resources;
    decoded_bytes += data.size();
    size_t resume{};
    std::array<size_t, 1> hits;
    while (resume < data.size()) {
      const auto n = search.Scan(
          {reinterpret_cast<const unsigned char *>(data.data()), data.size()},
          resume, hits);
      // Diagnostic completeness includes overlapping anchors, unlike the
      // legacy prefix-regex batching convention used by discovery.
      if (n)
        resume = hits[0] + 1;
      for (size_t i = 0; i < n; ++i)
        for (const auto &[h, bias] : anchors.at(data.substr(hits[i], 16))) {
          if (hits[i] < bias)
            continue;
          const auto pos = hits[i] - bias;
          const auto &code = targets.at(h);
          if (code.size() > data.size() - pos)
            continue;
          const auto candidate =
              std::string_view(data).substr(pos, code.size());
          size_t differences{}, prefix{};
          for (size_t j = 0; j < code.size(); ++j)
            differences += code[j] != candidate[j];
          while (prefix < code.size() && code[prefix] == candidate[prefix])
            ++prefix;
          ++candidates;
          auto &matches = best[h];
          if (matches.size() >= 4 &&
              differences > std::max_element(matches.begin(), matches.end(),
                                             [](const auto &a, const auto &b) {
                                               return a.second.differences <
                                                      b.second.differences;
                                             })
                                ->second.differences)
            continue;
          const auto digest = ResourceSha256(candidate);
          if (matches.contains(digest))
            continue;
          JsonValue m;
          m["resource"] = id;
          m["offset"] = Jnum(pos);
          m["size"] = Jnum(code.size());
          m["candidate_sha256"] = digest;
          m["physical_spans"] = spans;
          m["differing_offsets"] = Array();
          for (size_t j = 0; j < code.size(); ++j)
            if (code[j] != candidate[j])
              m["differing_offsets"].Append(Jnum(j));
          m["difference_count"] = Jnum(differences);
          m["matching_prefix"] = Jnum(prefix);
          matches.emplace(digest, Match{differences, prefix, std::move(m)});
          if (matches.size() > 4)
            matches.erase(std::max_element(matches.begin(), matches.end(),
                                           [](const auto &a, const auto &b) {
                                             return a.second.differences <
                                                    b.second.differences;
                                           }));
        }
    }
  };
  std::map<std::string, Store> physical;
  std::map<std::string, std::vector<Section>> sections;
  for (const std::string name : {argv[3], argv[4]})
    if (name != "-") {
      physical[name] = std::make_shared<Physical>(Local(argv[2], name), name);
      sections[name] = Sections(physical[name]);
    }
  const auto romfs = [&](Store storage, const std::string &label) {
    for (const auto &f : Romfs(storage)) {
      auto raw = storage->Read(f.offset, f.size);
      auto data = raw.starts_with("Yaz0") ? DecodeYaz0(raw) : raw;
      scan(data, label + ":" + f.name, storage->Spans(f.offset, f.size));
    }
  };
  for (const auto &[name, ss] : sections)
    for (const auto &section : ss) {
      if (Get(section.header, 0x108, 8))
        continue;
      auto layer = DataLayer(section);
      if (!layer)
        continue;
      if (section.header[2] == 0)
        romfs(layer, name + ":" + section.name);
      else if (section.header[2] == 1)
        for (const auto &f : Pfs(layer)) {
          if (f.size < 256)
            continue;
          auto header = layer->Read(f.offset, 256);
          if (!header.starts_with("NSO0"))
            continue;
          const auto flags = Get(header, 12, 4);
          Check(!(flags & 128), "Unsupported NSO");
          for (unsigned segment = 0; segment < 3; ++segment) {
            const auto source = Get(header, 16 + segment * 16, 4),
                       size = Get(header, 24 + segment * 16, 4);
            const bool compressed = flags & (1 << segment);
            const auto stored =
                compressed ? Get(header, 96 + segment * 4, 4) : size;
            Bounds(source, stored, f.size);
            auto raw = layer->Read(f.offset + source, stored);
            auto data = compressed ? DecodeLz4(raw, size) : raw;
            scan(data, name + ":" + f.name + ":" + std::to_string(segment),
                 layer->Spans(f.offset + source, stored));
          }
        }
    }
  if (std::string_view(argv[4]) != "-")
    for (const auto &patch : sections.at(argv[4]))
      if (patch.type == 0 && patch.header[2] == 0 &&
          Get(patch.header, 0x108, 8)) {
        const Section *base{};
        for (const auto &candidate : sections.at(argv[3]))
          if (candidate.type == patch.type && candidate.index == patch.index &&
              candidate.content_index == patch.content_index &&
              (candidate.title & ~u64{0x800}) == (patch.title & ~u64{0x800})) {
            Check(!base, "Ambiguous patch base");
            base = &candidate;
          }
        Check(base, "Missing patch base");
        romfs(Patched(*base, patch), "logical:" + patch.name);
      }
  JsonValue report;
  report["resources"] = Jnum(resources);
  report["decoded_bytes"] = Jnum(decoded_bytes);
  report["candidates"] = Jnum(candidates);
  report["shader_instructions_included"] = false;
  report["targets"] = JsonValue(JsonValueType::Object);
  for (const auto &[h, c] : targets) {
    report["targets"][h] = Array();
    for (const auto &[digest, m] : best[h])
      report["targets"][h].Append(m.result);
  }
  std::cout << Canonical(report) << '\n';
  return 0;
}
} // namespace VideoCommon::ShaderProfile
int main(int argc, char **argv) try {
  return VideoCommon::ShaderProfile::AuditGaps(argc, argv);
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
