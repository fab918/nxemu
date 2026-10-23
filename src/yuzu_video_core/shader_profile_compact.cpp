// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/shader_profile_compact.h"
#include "yuzu_video_core/shader_profile_codec.h"
#include "yuzu_video_core/shader_profile_export.h"
#include "yuzu_video_core/shader_profile_hash.h"
#include "yuzu_video_core/shader_profile_metrics.h"
#include "yuzu_video_core/shader_profile_resources.h"
#include <fstream>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace VideoCommon::ShaderProfile
{
namespace
{
void Check(bool b, const char * reason)
{
    if (!b) throw std::runtime_error(reason);
}
void Allowed(const JsonValue & value, std::initializer_list<const char *> fields)
{
    Check(value.isObject(), "Expected compact metadata object");
    for (const auto & field : value.GetMemberNames())
        Check(std::find(fields.begin(), fields.end(), field) != fields.end(), "Unknown compact metadata field");
}
std::string Hash(std::string_view bytes, Timing::Metrics * timings)
{
    return Timing::Measure(timings, Timing::Phase::ResourceVerification, [&] { return ResourceSha256(bytes); });
}
u64 Number(const JsonValue & n)
{
    if (n.isString())
    {
        const auto s = n.asString();
        Check(!s.empty() && s.size() <= 20 && s.find_first_not_of("0123456789") == std::string::npos, "Invalid compact integer");
        return std::stoull(s);
    }
    Check(n.Type() == JsonValueType::UnsignedInt || (n.Type() == JsonValueType::Int && n.asInt64() >= 0), "Expected compact integer");
    return n.asUInt64();
}
struct Writer
{
    std::string bytes;
    void Fixed(u64 n, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
            bytes += static_cast<char>(n >> (i * 8));
    }
    void Var(u64 n)
    {
        while (n >= 128)
        {
            bytes += static_cast<char>((n & 127) | 128);
            n >>= 7;
        }
        bytes += static_cast<char>(n);
    }
    void Text(std::string_view s)
    {
        Var(s.size());
        bytes += s;
    }
    void Hash(const std::string & h)
    {
        Check(h.size() == 64 && h.find_first_not_of("0123456789abcdef") == std::string::npos, "Invalid compact SHA-256");
        for (size_t i = 0; i < 64; i += 2)
        {
            const auto hex = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
            bytes += static_cast<char>((hex(h[i]) << 4) | hex(h[i + 1]));
        }
    }
};
struct Reader
{
    std::string_view data;
    size_t pos{};
    std::string_view Take(size_t n)
    {
        Check(pos <= data.size() && n <= data.size() - pos, "Truncated compact table");
        const auto result = data.substr(pos, n);
        pos += n;
        return result;
    }
    u64 Fixed(size_t n)
    {
        const auto b = Take(n);
        u64 v{};
        for (size_t i = 0; i < n; ++i)
            v |= u64{static_cast<unsigned char>(b[i])} << (i * 8);
        return v;
    }
    u64 Var()
    {
        u64 value{};
        for (unsigned i = 0; i < 10; ++i)
        {
            const auto b = Fixed(1);
            Check(i < 9 || b <= 1, "Compact varint overflow");
            value |= (b & 127) << (7 * i);
            if (b < 128)
            {
                Check(i == 0 || b != 0, "Noncanonical compact varint");
                return value;
            }
        }
        throw std::runtime_error("Compact varint overflow");
    }
    u32 Word()
    {
        const auto n = Var();
        Check(n <= UINT32_MAX, "Compact word overflow");
        return static_cast<u32>(n);
    }
    size_t Count(size_t maximum)
    {
        const auto n = Var();
        Check(n <= maximum && n <= data.size() - pos, "Compact table limit");
        return static_cast<size_t>(n);
    }
    std::string Text(size_t maximum = 4096)
    {
        const auto n = Count(maximum);
        return std::string(Take(n));
    }
    std::string Hash()
    {
        const auto b = Take(32);
        constexpr char hex[] = "0123456789abcdef";
        std::string h;
        h.reserve(64);
        for (unsigned char c : b)
        {
            h += hex[c >> 4];
            h += hex[c & 15];
        }
        return h;
    }
};
const std::array<const char *, 7> Fields{"local_memory_size", "texture_bound", "guest_start_address", "guest_read_lowest", "guest_read_highest", "viewport_transform_state", "stage"};
const std::array<const char *, 4> Maps{"texture_types", "texture_pixel_formats", "cbuf_values", "cbuf_replacements"};
const std::array<const char *, 5> Encodings{"raw", "yaz0", "nso-lz4", "bktr-raw", "bktr-yaz0"};
struct Span
{
    std::string file;
    u64 offset{}, size{};
};
struct Resource
{
    unsigned kind{};
    std::string file, hash, segment_hash;
    u64 offset{}, size{}, decoded_size{};
    std::vector<Span> spans;
};
struct Program
{
    std::string hash;
    u32 resource{};
    u64 size{}, offset{};
};
struct Env
{
    u32 program{};
    std::array<u32, 7> fields{};
    std::array<std::vector<std::pair<u64, u32>>, 4> maps;
    std::array<u32, 3> workgroup{};
    u32 shared{};
    std::array<u32, 8> passthrough{};
    std::string header;
};
std::filesystem::path Local(const std::filesystem::path & root, const std::string & name)
{
    Check(!name.empty() && name != "." && name != ".." && name.find_first_of("/\\:\0", 0, 4) == std::string::npos, "Invalid compact resource name");
    const auto path = std::filesystem::canonical(root / std::filesystem::u8path(name));
    Check(path.parent_path() == std::filesystem::canonical(root), "Compact resource escapes root");
    return path;
}
std::string Bytes(const std::filesystem::path & root, const std::string & name, u64 off, u64 size, Timing::Metrics * timings)
{
    Timing::Timer timer(timings, Timing::Phase::ResourceIO);
    const auto path = Local(root, name);
    const auto length = std::filesystem::file_size(path);
    Check(size <= MaxDecodedResource && off <= length && size <= length - off, "Compact resource range");
    std::string bytes(static_cast<size_t>(size), '\0');
    std::ifstream in(path, std::ios::binary);
    in.seekg(static_cast<std::streamoff>(off));
    in.read(bytes.data(), size);
    Check(bool(in), "Cannot read compact resource");
    if (timings) timings->Add(Timing::Counter::ResourceReadBytes, size);
    return bytes;
}
std::string Environment(const Env & env, const Program & program, const std::string & code, Timing::Metrics * timings)
{
    Check(env.fields[6] <= 6 && env.fields[2] == env.fields[3] && env.fields[4] >= env.fields[3] && u64{env.fields[4]} - env.fields[3] + 8 == code.size(), "Compact environment bounds");
    Writer out;
    out.Fixed(code.size(), 8);
    for (const auto & m : env.maps)
        out.Fixed(m.size(), 8);
    for (auto n : env.fields)
        out.Fixed(n, 4);
    out.bytes += code;
    for (size_t i = 0; i < 4; ++i)
        for (const auto & [key, value] : env.maps[i])
        {
            out.Fixed(key, i < 2 ? 4 : 8);
            out.Fixed(value, 4);
        }
    if (env.fields[6] == 5)
    {
        for (auto n : env.workgroup)
            out.Fixed(n, 4);
        out.Fixed(env.shared, 4);
    }
    else
    {
        Check(code.size() >= 80 && Hash(std::string_view(code).substr(0, 80), timings) == env.header, "Compact shader header fingerprint");
        out.bytes += code.substr(0, 80);
        if (env.fields[6] == 3)
            for (auto n : env.passthrough)
                out.Fixed(n, 4);
    }
    return out.bytes;
}
} // namespace

std::string PackCompact(const JsonValue & profile)
{
    Allowed(profile, {"format", "version", "title_id", "guest_state_version", "content_label", "shaders", "environments", "recipes", "unresolved_recipes"});
    Check(profile["shaders"].isObject() && profile["shaders"].size() <= 16384 && profile["environments"].isObject() && profile["environments"].size() <= 65536 && profile["recipes"].isArray() && profile["recipes"].size() <= 65536 && (profile["unresolved_recipes"].isNull() || (profile["unresolved_recipes"].isArray() && profile["unresolved_recipes"].size() <= 65536)), "Compact table limit");
    const auto title = profile["title_id"].asString();
    Check(title.size() == 16 && title.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos, "Compact title syntax");
    Check(profile["content_label"].isString() && profile["content_label"].asString().size() <= 4096, "Compact label limit");
    Check(Number(profile["guest_state_version"]) == 1, "Compact guest state version");
    Check(profile["version"].asUInt64() == 2 && profile["format"].asString() == "nxshader", "Compact encoder requires v2 metadata");
    Writer out;
    out.bytes = "NXSHBIN3";
    out.Fixed(std::stoull(profile["title_id"].asString(), nullptr, 16), 8);
    out.Text(profile["content_label"].asString());
    std::vector<JsonValue> resources;
    std::map<std::string, size_t> resource_ids, program_ids, env_ids, state_ids;
    struct Ref
    {
        size_t resource;
        u64 offset, size;
        std::string hash;
    };
    std::vector<Ref> programs;
    auto shader_names = profile["shaders"].GetMemberNames();
    std::sort(shader_names.begin(), shader_names.end(), [&](const auto & a, const auto & b) { return std::pair(Canonical(profile["shaders"][a]["references"]), a) < std::pair(Canonical(profile["shaders"][b]["references"]), b); });
    for (const auto & h : shader_names)
    {
        const auto & shader = profile["shaders"][h];
        Allowed(shader, {"size", "references"});
        Check(shader["references"].size() == 1, "Compact requires one locator");
        auto descriptor = shader["references"][0];
        Allowed(descriptor, {"encoding", "file", "offset", "size", "container_offset", "container_size", "decoded_size", "container_sha256", "segment_sha256", "physical_spans", "resource_size", "resource_sha256"});
        const auto offset = Number(descriptor["offset"]), size = Number(descriptor["size"]);
        Check(size == Number(shader["size"]), "Compact locator size");
        descriptor.removeMember("offset");
        descriptor.removeMember("size");
        const auto key = Canonical(descriptor);
        auto [it, inserted] = resource_ids.emplace(key, resources.size());
        if (inserted) resources.push_back(std::move(descriptor));
        program_ids[h] = programs.size();
        programs.push_back({it->second, offset, size, h});
    }
    out.Var(resources.size());
    for (const auto & resource : resources)
    {
        const auto encoding = resource["encoding"].asString();
        const auto it = std::find(Encodings.begin(), Encodings.end(), encoding);
        Check(it != Encodings.end(), "Unknown compact resource encoding");
        const auto kind = it - Encodings.begin();
        out.Var(kind);
        if (kind < 3) out.Text(resource["file"].asString());
        if (kind == 1 || kind == 2)
        {
            out.Var(Number(resource["container_offset"]));
            out.Var(Number(resource["container_size"]));
        }
        if (kind == 2)
        {
            out.Var(Number(resource["decoded_size"]));
            out.Hash(resource["container_sha256"].asString());
            out.Hash(resource["segment_sha256"].asString());
        }
        if (kind >= 3)
        {
            out.Var(Number(resource["resource_size"]));
            out.Hash(resource["resource_sha256"].asString());
            out.Var(resource["physical_spans"].size());
            for (const auto & span : resource["physical_spans"])
            {
                out.Text(span["file"].asString());
                out.Var(Number(span["offset"]));
                out.Var(Number(span["size"]));
            }
        }
    }
    out.Var(programs.size());
    for (const auto & p : programs)
    {
        out.Hash(p.hash);
        out.Var(p.size);
        out.Var(p.resource);
        out.Var(p.offset);
    }
    auto names = profile["environments"].GetMemberNames();
    std::sort(names.begin(), names.end(), [&](const auto & a, const auto & b) {const auto& x=profile["environments"][a];const auto& y=profile["environments"][b];return std::tuple(program_ids.at(x["shader"].asString()),Number(x["guest_start_address"]),a)<std::tuple(program_ids.at(y["shader"].asString()),Number(y["guest_start_address"]),b); });
    out.Var(names.size());
    for (const auto & id : names)
    {
        env_ids[id] = env_ids.size();
        const auto & env = profile["environments"][id];
        Allowed(env, {"shader", "local_memory_size", "texture_bound", "guest_start_address", "guest_read_lowest", "guest_read_highest", "viewport_transform_state", "stage", "texture_types", "texture_pixel_formats", "cbuf_values", "cbuf_replacements", "workgroup_size", "shared_memory_size", "program_header_sha256", "gp_passthrough_mask"});
        out.Var(program_ids.at(env["shader"].asString()));
        for (auto name : Fields)
            out.Var(Number(env[name]));
        for (const auto name : Maps)
        {
            out.Var(env[name].size());
            for (const auto & pair : env[name])
            {
                out.Var(Number(pair[0]));
                out.Var(Number(pair[1]));
            }
        }
        const auto stage = Number(env["stage"]);
        if (stage == 5)
        {
            for (const auto & n : env["workgroup_size"])
                out.Var(Number(n));
            out.Var(Number(env["shared_memory_size"]));
        }
        else
        {
            out.Hash(env["program_header_sha256"].asString());
            if (stage == 3)
                for (const auto & n : env["gp_passthrough_mask"])
                    out.Var(Number(n));
        }
    }
    // Guest states are deduplicated independently of program combinations.
    std::vector<std::vector<u32>> states;
    for (const auto field : {"recipes", "unresolved_recipes"})
        for (const auto & recipe : profile[field])
            if (recipe["kind"].asString() == "graphics")
            {
                const auto key = Canonical(recipe["guest_words"]);
                if (!state_ids.contains(key))
                {
                    std::vector<u32> w;
                    for (const auto & n : recipe["guest_words"])
                    {
                        Check(Number(n) <= UINT32_MAX, "State word overflow");
                        w.push_back(static_cast<u32>(Number(n)));
                    }
                    Check(w.size() == 278, "Compact guest state ABI");
                    state_ids[key] = states.size();
                    states.push_back(std::move(w));
                }
            }
    std::array<u32, 278> base{};
    for (size_t word = 0; word < 278; ++word)
    {
        std::map<u32, size_t> counts;
        size_t best{};
        for (const auto & state : states)
            if (const auto count = ++counts[state[word]]; count > best)
            {
                best = count;
                base[word] = state[word];
            }
    }
    for (auto n : base)
        out.Var(n);
    out.Var(states.size());
    for (const auto & state : states)
    {
        size_t count{};
        for (size_t i = 0; i < 278; ++i)
            count += state[i] != base[i];
        out.Var(count);
        for (size_t i = 0; i < 278; ++i)
            if (state[i] != base[i])
            {
                out.Var(i);
                out.Var(state[i]);
            }
    }
    for (const auto field : {"recipes", "unresolved_recipes"})
    {
        out.Var(profile[field].size());
        for (const auto & r : profile[field])
        {
            const bool compute = r["kind"].asString() == "compute";
            out.Var(compute ? 1 : 0);
            if (compute)
            {
                out.Fixed(Number(r["shader_hash"]), 8);
                out.Var(Number(r["shared_memory_size"]));
                for (const auto & n : r["workgroup_size"])
                    out.Var(Number(n));
                for (const auto & n : r["cbuf_sizes"])
                    out.Var(Number(n));
                out.Var(Number(r["texture_table_size"]));
            }
            else
            {
                out.Var(state_ids.at(Canonical(r["guest_words"])));
                for (const auto & n : r["shader_hashes"])
                    out.Fixed(Number(n), 8);
                for (const auto & stage : r["cbuf_sizes"])
                    for (const auto & n : stage)
                        out.Var(Number(n));
                for (const auto & n : r["texture_pool"])
                    out.Var(Number(n));
            }
            out.Var(r["environments"].size());
            for (const auto & id : r["environments"])
                out.Var(env_ids.at(id.asString()));
        }
    }
    Check(out.bytes.size() <= 32 * 1024 * 1024, "Compact profile size limit");
    return Pack(out.bytes);
}

std::vector<Recipe> ReadCompact(std::string_view payload, const std::filesystem::path & root, u64 title_id, size_t * unresolved_count, Timing::Metrics * timings, const ResourceProgress & progress)
{
    Timing::Timer schema_timer(timings, Timing::Phase::SchemaAndLocators);
    Reader in{payload};
    Check(in.Take(8) == "NXSHBIN3" && in.Fixed(8) == title_id, "Compact title/version mismatch");
    in.Text();
    const auto resource_count = in.Count(65536);
    std::vector<Resource> resources;
    resources.reserve(resource_count);
    size_t total_spans{};
    for (size_t i = 0; i < resource_count; ++i)
    {
        Resource r;
        r.kind = in.Word();
        Check(r.kind < 5, "Compact resource kind");
        if (r.kind < 3) r.file = in.Text();
        if (r.kind == 1 || r.kind == 2)
        {
            r.offset = in.Var();
            r.size = in.Var();
        }
        if (r.kind == 2)
        {
            r.decoded_size = in.Var();
            r.hash = in.Hash();
            r.segment_hash = in.Hash();
        }
        if (r.kind >= 3)
        {
            r.size = in.Var();
            r.hash = in.Hash();
            const auto count = in.Count(65536);
            total_spans += count;
            Check(total_spans <= 1000000, "Compact span budget");
            for (size_t n = 0; n < count; ++n)
                r.spans.push_back({in.Text(), in.Var(), in.Var()});
        }
        Check(r.size <= MaxDecodedResource && r.decoded_size <= MaxDecodedResource, "Compact decoded limit");
        resources.push_back(std::move(r));
    }
    const auto program_count = in.Count(16384);
    std::vector<Program> programs;
    programs.reserve(program_count);
    std::set<std::string> hashes;
    std::map<size_t, std::vector<size_t>> groups;
    size_t code_budget{};
    for (size_t i = 0; i < program_count; ++i)
    {
        Program p;
        p.hash = in.Hash();
        p.size = in.Var();
        p.resource = in.Word();
        p.offset = in.Var();
        Check(p.size && p.size % 8 == 0 && p.size <= 1024 * 1024 && p.resource < resources.size() && hashes.insert(p.hash).second, "Compact program bounds");
        code_budget += p.size;
        Check(code_budget <= 128 * 1024 * 1024, "Compact code budget");
        groups[p.resource].push_back(i);
        programs.push_back(std::move(p));
    }
    const auto env_count = in.Count(65536);
    std::vector<Env> envs;
    envs.reserve(env_count);
    size_t map_budget{};
    for (size_t i = 0; i < env_count; ++i)
    {
        Env env;
        env.program = in.Word();
        Check(env.program < programs.size(), "Compact environment program");
        for (auto & n : env.fields)
            n = in.Word();
        Check(env.fields[6] <= 6, "Compact stage");
        for (size_t m = 0; m < 4; ++m)
        {
            const auto count = in.Count(65536);
            map_budget += count;
            Check(map_budget <= 2000000, "Compact map budget");
            std::set<u64> keys;
            for (size_t n = 0; n < count; ++n)
            {
                const auto key = in.Var();
                Check((m >= 2 || key <= UINT32_MAX) && keys.insert(key).second, "Compact environment map key");
                env.maps[m].emplace_back(key, in.Word());
            }
        }
        if (env.fields[6] == 5)
        {
            for (auto & n : env.workgroup)
                n = in.Word();
            env.shared = in.Word();
        }
        else
        {
            env.header = in.Hash();
            if (env.fields[6] == 3)
                for (auto & n : env.passthrough)
                    n = in.Word();
        }
        envs.push_back(std::move(env));
    }
    std::array<u32, 278> base;
    for (auto & n : base)
        n = in.Word();
    const auto state_count = in.Count(65536);
    std::vector<std::vector<u32>> states;
    states.reserve(state_count);
    Check(state_count * 278ULL * 4 <= 128 * 1024 * 1024, "Compact state budget");
    for (size_t i = 0; i < state_count; ++i)
    {
        std::vector<u32> state(base.begin(), base.end());
        const auto count = in.Count(278);
        size_t previous{};
        for (size_t n = 0; n < count; ++n)
        {
            const auto word = in.Word();
            Check(word < 278 && (n == 0 || word > previous), "Compact patch index");
            state[word] = in.Word();
            previous = word;
        }
        states.push_back(std::move(state));
    }
    struct Pending
    {
        Recipe recipe;
        std::vector<u32> ids;
    };
    std::vector<Pending> pending;
    size_t total_environment_bytes{}, unresolved{};
    for (unsigned table = 0; table < 2; ++table)
    {
        const auto count = in.Count(65536);
        if (table) unresolved = count;
        for (size_t i = 0; i < count; ++i)
        {
            Pending p;
            const auto kind = in.Word();
            Check(kind <= 1 && (!table || kind == 0), "Compact recipe kind");
            auto & r = p.recipe;
            r.compute = kind == 1;
            if (r.compute)
            {
                r.shader_hashes[0] = in.Fixed(8);
                r.shared_memory_size = in.Word();
                for (auto & n : r.workgroup_size)
                    n = in.Word();
                for (auto & n : r.compute_cbuf_sizes)
                    n = in.Word();
                r.texture_table_size = in.Word();
            }
            else
            {
                const auto state = in.Word();
                Check(state < states.size(), "Compact state reference");
                r.guest_words = states[state];
                for (auto & n : r.shader_hashes)
                    n = in.Fixed(8);
                for (auto & stage : r.cbuf_sizes)
                    for (auto & n : stage)
                        n = in.Word();
                for (auto & n : r.texture_pool)
                    n = in.Word();
            }
            const auto stages = in.Count(6);
            Check(table ? stages == 0 : (r.compute ? stages == 1 : stages >= 1), "Compact recipe stages");
            size_t active{};
            for (auto h : r.shader_hashes)
                active += h != 0;
            Check(table || r.compute || active == stages, "Compact active stages");
            for (size_t j = 0; j < stages; ++j)
            {
                const auto id = in.Word();
                Check(id < envs.size() && r.compute == (envs[id].fields[6] == 5), "Compact environment reference");
                p.ids.push_back(id);
            }
            if (!table) pending.push_back(std::move(p));
        }
    }
    Check(in.pos == payload.size(), "Trailing compact bytes");
    if (unresolved_count) *unresolved_count = unresolved;
    // Parsing and all table references finish before touching local resources.
    schema_timer.Stop();
    Timing::Timer resources_timer(timings, Timing::Phase::ResourceTotal);
    std::vector<std::string> codes(programs.size());
    size_t completed_resources{};
    for (const auto & [rid, requests] : groups)
    {
        Check(!progress || progress(completed_resources++, groups.size()), "Shader profile operation cancelled");
        const auto & resource = resources[rid];
        if (resource.kind == 0)
        {
            for (auto id : requests)
            {
                const auto & p = programs[id];
                codes[id] = Bytes(root, resource.file, p.offset, p.size, timings);
                Check(Hash(codes[id], timings) == p.hash, "Compact program fingerprint mismatch");
            }
            continue;
        }
        std::string raw;
        size_t prefix{};
        for (auto id : requests)
        {
            const auto & p = programs[id];
            Check(p.offset <= MaxDecodedResource && p.size <= MaxDecodedResource - p.offset, "Compact program resource bounds");
            prefix = std::max(prefix, static_cast<size_t>(p.offset + p.size));
        }
        if (resource.kind >= 3)
        {
            raw.reserve(resource.size);
            for (const auto & span : resource.spans)
            {
                Check(span.size <= resource.size - raw.size(), "Compact span size");
                raw += Bytes(root, span.file, span.offset, span.size, timings);
            }
            Check(raw.size() == resource.size, "Incomplete compact spans");
        }
        else
            raw = Bytes(root, resource.file, resource.offset, resource.size, timings);
        if (resource.kind >= 2) Check(Hash(raw, timings) == resource.hash, "Compact resource fingerprint mismatch");
        auto decoded = Timing::Measure(timings, Timing::Phase::ResourceDecompression, [&] { return resource.kind == 1 || resource.kind == 4 ? DecodeYaz0(raw, resource.kind == 4 ? prefix : MaxDecodedResource + 1) : resource.kind == 2 ? DecodeLz4(raw, resource.decoded_size)
                                                                                                                                                                                                                                         : std::move(raw); });
        if (timings) timings->Add(Timing::Counter::ResourceDecodedBytes, decoded.size());
        if (resource.kind == 2) Check(Hash(decoded, timings) == resource.segment_hash, "Compact NSO fingerprint mismatch");
        for (auto id : requests)
        {
            const auto & p = programs[id];
            Check(p.offset <= decoded.size() && p.size <= decoded.size() - p.offset, "Compact shader range");
            codes[id] = decoded.substr(p.offset, p.size);
            Check(Hash(codes[id], timings) == p.hash, "Compact program fingerprint mismatch");
        }
    }
    resources_timer.Stop();
    Timing::Timer environment_timer(timings, Timing::Phase::EnvironmentAssembly);
    std::vector<std::string> assembled;
    assembled.reserve(envs.size());
    size_t env_budget{};
    for (const auto & env : envs)
    {
        assembled.push_back(Environment(env, programs[env.program], codes[env.program], timings));
        env_budget += assembled.back().size();
        Check(env_budget <= 256 * 1024 * 1024, "Compact environment budget");
    }
    environment_timer.Stop();
    Timing::Timer recipe_timer(timings, Timing::Phase::RecipeAssembly);
    std::vector<Recipe> result;
    result.reserve(pending.size());
    for (auto & p : pending)
    {
        for (auto id : p.ids)
        {
            total_environment_bytes += assembled[id].size();
            Check(total_environment_bytes <= 256 * 1024 * 1024, "Compact recipe memory budget");
            p.recipe.environments.push_back(assembled[id]);
        }
        result.push_back(std::move(p.recipe));
    }
    if (timings)
    {
        timings->Add(Timing::Counter::Programs, programs.size());
        timings->Add(Timing::Counter::Environments, envs.size());
        timings->Add(Timing::Counter::Recipes, result.size());
        timings->Add(Timing::Counter::ResourceGroups, groups.size());
    }
    return result;
}
} // namespace VideoCommon::ShaderProfile
