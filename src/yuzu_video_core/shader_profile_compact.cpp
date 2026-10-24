// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/shader_profile_compact.h"
#include "yuzu_video_core/shader_profile_codec.h"
#include "yuzu_video_core/shader_profile_export.h"
#include "yuzu_video_core/shader_profile_hash.h"
#include "yuzu_video_core/shader_profile_metrics.h"
#include "yuzu_video_core/shader_profile_resources.h"
#include "yuzu_video_core/shader_profile_validation.h"
#include "yuzu_video_core/shader_profile_context.h"
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
    Check(ValidResourceName(name), "Invalid compact resource name");
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
SharedShaderBytes Environment(const Env & env, std::shared_ptr<const std::string> program, Timing::Metrics * timings)
{
    const std::string_view code=*program;
    Check(env.fields[6] <= 6 && env.fields[2] == env.fields[3] && env.fields[4] >= env.fields[3] && u64{env.fields[4]} - env.fields[3] + 8 == code.size(), "Compact environment bounds");
    Writer out;
    out.Fixed(code.size(), 8);
    for (const auto & m : env.maps)
        out.Fixed(m.size(), 8);
    for (auto n : env.fields)
        out.Fixed(n, 4);
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
    return SharedShaderBytes::FromMetadata(std::move(out.bytes),std::move(program));
}
} // namespace

std::string PackCompact(const JsonValue & profile)
{
    Allowed(profile, {"format", "version", "title_id", "guest_state_version", "content_label", "shaders", "environments", "recipes", "unresolved_recipes", "source_files", "source_context"});
    Check(profile["shaders"].isObject() && profile["shaders"].size() <= 16384 && profile["environments"].isObject() && profile["environments"].size() <= 65536 && profile["recipes"].isArray() && profile["recipes"].size() <= 65536 && (profile["unresolved_recipes"].isNull() || (profile["unresolved_recipes"].isArray() && profile["unresolved_recipes"].size() <= 65536)), "Compact table limit");
    const auto title = profile["title_id"].asString();
    Check(title.size() == 16 && title.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos, "Compact title syntax");
    Check(profile["content_label"].isString() && profile["content_label"].asString().size() <= 4096, "Compact label limit");
    Check(Number(profile["guest_state_version"]) == 1, "Compact guest state version");
    Check(profile["version"].asUInt64() == 2 && profile["format"].asString() == "nxshader", "Compact encoder requires v2 metadata");
    Writer out;
    out.bytes = "NXSHBIN4";
    out.Fixed(std::stoull(profile["title_id"].asString(), nullptr, 16), 8);
    out.Text(profile["content_label"].asString());
    {
        Check((profile["source_files"].isNull() || profile["source_files"].isArray()) && profile["source_files"].size() <= 1024, "Invalid source file table");
        out.Var(profile["source_files"].size());
        for (const auto& file : profile["source_files"]) {
            Allowed(file, {"name", "size", "header"});
            out.Text(file["name"].asString()); out.Var(Number(file["size"])); out.Hash(file["header"].asString());
        }
        out.Text(profile["source_context"].asString());
    }
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
    std::map<std::string,std::string> locator_keys;
    for (const auto& name : shader_names) locator_keys.emplace(name,Canonical(profile["shaders"][name]["references"]));
    std::sort(shader_names.begin(), shader_names.end(), [&](const auto & a, const auto & b) {
        return std::tie(locator_keys.at(a),a) < std::tie(locator_keys.at(b),b);
    });
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
            Check(env[name].isArray(), "Invalid environment map");
            out.Var(env[name].size());
            for (const auto & pair : env[name])
            {
                Check(pair.isArray() && pair.size() == 2, "Invalid environment map entry");
                out.Var(Number(pair[0]));
                out.Var(Number(pair[1]));
            }
        }
        const auto stage = Number(env["stage"]);
        if (stage == 5)
        {
            Check(env["workgroup_size"].isArray() && env["workgroup_size"].size() == 3, "Invalid compute dimensions");
            for (const auto & n : env["workgroup_size"])
                out.Var(Number(n));
            out.Var(Number(env["shared_memory_size"]));
        }
        else
        {
            out.Hash(env["program_header_sha256"].asString());
            if (stage == 3)
            {
                Check(env["gp_passthrough_mask"].isArray() && env["gp_passthrough_mask"].size() == 8, "Invalid geometry mask");
                for (const auto & n : env["gp_passthrough_mask"])
                    out.Var(Number(n));
            }
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
            Allowed(r, {"kind", "guest_words", "shader_hashes", "cbuf_sizes", "texture_pool", "environments", "shader_hash", "shared_memory_size", "workgroup_size", "texture_table_size", "reason", "source_context"});
            Check(r["kind"].asString() == "compute" || r["kind"].asString() == "graphics", "Invalid recipe kind");
            const bool compute = r["kind"].asString() == "compute";
            const bool unresolved = std::string_view(field) == "unresolved_recipes";
            Check(!unresolved || (!compute && r["reason"].asString() == "no_serialized_environment_for_source_key"), "Invalid unresolved recipe");
            Check(unresolved || r["environments"].isArray(), "Invalid recipe environments");
            out.Var(compute ? 1 : 0);
            out.Text(r["source_context"].isNull() ? profile["source_context"].asString() : r["source_context"].asString());
            if (compute)
            {
                Check(r["workgroup_size"].isArray() && r["workgroup_size"].size() == 3 &&
                    r["cbuf_sizes"].isArray() && r["cbuf_sizes"].size() == 8, "Invalid compute recipe arrays");
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
                Check(r["shader_hashes"].isArray() && r["shader_hashes"].size() == 6 &&
                    r["cbuf_sizes"].isArray() && r["cbuf_sizes"].size() == 5 &&
                    r["texture_pool"].isArray() && r["texture_pool"].size() == 2, "Invalid graphics recipe arrays");
                out.Var(state_ids.at(Canonical(r["guest_words"])));
                for (const auto & n : r["shader_hashes"])
                    out.Fixed(Number(n), 8);
                for (const auto & stage : r["cbuf_sizes"])
                {
                    Check(stage.isArray() && stage.size() == 18, "Invalid constant buffer array");
                    for (const auto & n : stage)
                        out.Var(Number(n));
                }
                for (const auto & n : r["texture_pool"])
                    out.Var(Number(n));
            }
            out.Var(r["environments"].size());
            for (const auto & id : r["environments"])
                out.Var(env_ids.at(id.asString()));
        }
    }
    Check(out.bytes.size() <= 32 * 1024 * 1024, "Compact profile size limit");
    return Pack(out.bytes,3);
}

static std::vector<Recipe> ReadCompactImpl(std::string_view payload, const std::filesystem::path & root, u64 title_id, size_t * unresolved_count, Timing::Metrics * timings, const ResourceProgress & progress, bool metadata_only, ImportReport* report = nullptr, std::string_view target_context = {}, const std::set<std::string>* existing_recipes = nullptr, const std::map<std::string,std::shared_ptr<const std::string>>* existing_programs = nullptr, bool allow_cross_version = false)
{
    Timing::Timer schema_timer(timings, Timing::Phase::SchemaAndLocators);
    Reader in{payload};
    const auto magic = in.Take(8);
    Check(magic == "NXSHBIN4" && in.Fixed(8) == title_id, "Compact title/version mismatch");
    in.Text();
    struct SourceFile { std::string name; u64 size; std::string header; };
    std::vector<SourceFile> source_files;
    std::set<std::string> source_names;
    std::string source_context;
    if (magic == "NXSHBIN4") {
        const auto count = in.Count(1024);
        for (size_t i=0;i<count;++i) {
            SourceFile file{in.Text(), in.Var(), in.Hash()};
            Check(ValidResourceName(file.name) && source_names.insert(file.name).second, "Invalid source file name");
            source_files.push_back(std::move(file));
        }
        source_context = in.Text(64);
        Check(source_context.empty() || ValidContext(source_context), "Invalid profile source context");
    }
    const auto resource_count = in.Count(65536);
    std::vector<Resource> resources;
    resources.reserve(resource_count);
    size_t total_spans{};
    for (size_t i = 0; i < resource_count; ++i)
    {
        Resource r;
        r.kind = in.Word();
        Check(r.kind < 5, "Compact resource kind");
        if (r.kind < 3) { r.file = in.Text(); Check(ValidResourceName(r.file), "Invalid compact resource name"); }
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
            u64 span_bytes{};
            for (const auto& span : r.spans) {
                Check(ValidResourceName(span.file) && span.size <= r.size - span_bytes &&
                    span.offset <= UINT64_MAX - span.size, "Invalid compact span");
                span_bytes += span.size;
            }
            Check(span_bytes == r.size, "Incomplete compact spans");
        }
        Check(r.size <= MaxDecodedResource && r.decoded_size <= MaxDecodedResource, "Compact decoded limit");
        Check(r.offset <= UINT64_MAX - r.size, "Compact resource offset overflow");
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
        const auto& resource = resources[p.resource];
        Check(p.offset <= UINT64_MAX - p.size, "Compact program offset overflow");
        if (resource.kind != 0) Check(p.offset <= MaxDecodedResource && p.size <= MaxDecodedResource - p.offset, "Compact program resource bounds");
        if (resource.kind == 2 || resource.kind == 3) {
            const auto limit = resource.kind == 2 ? resource.decoded_size : resource.size;
            Check(p.offset <= limit && p.size <= limit - p.offset, "Compact program resource range");
        }
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
        Check(env.fields[2] == env.fields[3] && env.fields[4] >= env.fields[3] &&
            u64{env.fields[4]} - env.fields[3] + 8 == programs[env.program].size,
            "Compact environment bounds");
        Check(env.fields[6] == 5 || programs[env.program].size >= 80, "Compact graphics header size");
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
                const auto value=in.Word();
                Check(ValidEnvironmentMapValue(m,value),"Invalid compact environment query value");
                env.maps[m].emplace_back(key, value);
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
            r.source_context = in.Text(64);
            Check(r.source_context.empty() || ValidContext(r.source_context), "Invalid recipe source context");
            if (r.compute)
            {
                r.shader_hashes[0] = in.Fixed(8);
                Check(r.shader_hashes[0] != 0, "Empty compute program hash");
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
            if (!table) {
                if (r.compute) {
                    const auto& env = envs[p.ids.front()];
                    Check(env.workgroup == r.workgroup_size && env.shared == r.shared_memory_size,
                        "Compute recipe/environment dimensions mismatch");
                } else {
                    size_t env_index{};
                    constexpr std::array<u32,6> stages_by_slot{6,0,1,2,3,4};
                    for (size_t slot=0;slot<6;++slot) if (r.shader_hashes[slot])
                        Check(envs[p.ids[env_index++]].fields[6] == stages_by_slot[slot], "Graphics recipe stage order mismatch");
                }
                size_t env_index{};
                for (size_t slot=0;slot<6;++slot) if (r.shader_hashes[slot]) {
                    const auto& env = envs[p.ids.at(env_index++)];
                    for (const auto& [key,value] : env.maps[2]) {
                        const auto bank = key >> 32;
                        if (static_cast<u32>(key) == UINT32_MAX && bank < (r.compute ? 8U : 18U)) {
                            const auto size = r.compute ? r.compute_cbuf_sizes[bank] : r.cbuf_sizes[slot <= 1 ? 0 : slot-1][bank];
                            Check(value == size, "Recipe constant buffer size mismatch");
                        }
                        if (r.compute && key == UINT64_MAX) Check(value == r.texture_table_size, "Recipe texture table size mismatch");
                        if (!r.compute && key == UINT64_MAX-1) Check(value == r.texture_pool[0], "Recipe texture pool mismatch");
                        if (!r.compute && key == UINT64_MAX-2) Check(value == r.texture_pool[1], "Recipe sampler pool mismatch");
                    }
                }
            }
            if (!table) pending.push_back(std::move(p));
        }
    }
    // Identity contains semantic compilation inputs only, never table indices,
    // physical locators, compression, labels or the community revision's digest.
    std::vector<std::string> environment_identities;
    environment_identities.reserve(envs.size());
    for (const auto& env : envs) {
        Writer identity;
        const auto words = [&](const auto& values) { for (auto value : values) identity.Fixed(value,4); };
        identity.Hash(programs[env.program].hash); words(env.fields);
        for (auto map : env.maps) {
            std::sort(map.begin(),map.end()); identity.Var(map.size());
            for (const auto& [key,value] : map) { identity.Fixed(key,8); identity.Fixed(value,4); }
        }
        words(env.workgroup); identity.Fixed(env.shared,4); words(env.passthrough); identity.Text(env.header);
        environment_identities.push_back(std::move(identity.bytes));
    }
    for (auto& p : pending) {
        Writer identity;
        identity.Text("nxshader-recipe-1"); identity.Text(p.recipe.source_context);
        const auto words = [&](const auto& values) { for (auto value : values) identity.Fixed(value,4); };
        identity.Var(p.recipe.compute); words(p.recipe.guest_words);
        for (auto hash : p.recipe.shader_hashes) identity.Fixed(hash,8);
        for (const auto& sizes : p.recipe.cbuf_sizes) words(sizes);
        words(p.recipe.texture_pool); identity.Fixed(p.recipe.shared_memory_size,4);
        words(p.recipe.workgroup_size); words(p.recipe.compute_cbuf_sizes); identity.Fixed(p.recipe.texture_table_size,4);
        for (auto id : p.ids) identity.bytes += environment_identities[id];
        p.recipe.identity = ResourceSha256(identity.bytes);
    }
    Check(in.pos == payload.size(), "Trailing compact bytes");
    if (unresolved_count) *unresolved_count = unresolved;
    // Parsing and all table references finish before touching local resources.
    schema_timer.Stop();
    if(metadata_only) {
        std::vector<Recipe> metadata;metadata.reserve(pending.size());
        for(auto& p:pending)metadata.push_back(std::move(p.recipe));
        return metadata;
    }
    ImportReport outcome;
    // Reject only incompatible recipe contexts. A merged community revision can
    // contain recipes from multiple updates without forcing irrelevant reads.
    std::set<size_t> needed_programs;
    const auto compatible_context = [&](const Recipe& recipe) {
        return target_context.empty() ? recipe.source_context.empty() :
            recipe.source_context==target_context || (allow_cross_version && ValidContext(target_context) && ValidContext(recipe.source_context));
    };
    for (const auto& p : pending) {
        if (!compatible_context(p.recipe)) { ++outcome.skipped; ++outcome.incompatible_contexts; continue; }
        if (existing_recipes && existing_recipes->contains(p.recipe.identity)) {
            ++outcome.reused; outcome.reused_ids.push_back(p.recipe.identity); continue;
        }
        for (auto id : p.ids) needed_programs.insert(envs[id].program);
    }
    std::vector<std::shared_ptr<const std::string>> codes(programs.size());
    groups.clear();
    for (auto id : needed_programs) {
        if (existing_programs) {
            const auto found=existing_programs->find(programs[id].hash);
            if (found!=existing_programs->end() && found->second && found->second->size()==programs[id].size &&
                Hash(*found->second,timings)==programs[id].hash) codes[id]=found->second;
        }
        if (!codes[id]) groups[programs[id].resource].push_back(id);
    }
    if (needed_programs.empty()) {
        if (report) *report = outcome;
        if (outcome.reused) return {};
        throw std::runtime_error("No recipes match the installed base/update identity; export a profile from an identified capture");
    }
    // Resolve renamed source containers using bounded fingerprints, then still
    // verify every requested program/resource. No guessed shader is accepted.
    std::map<std::string,std::string> aliases;
    if (!source_files.empty() && !groups.empty()) {
        std::set<std::string> needed_files;
        for (const auto& [rid,requests] : groups) {
            const auto& resource=resources[rid];
            if (resource.kind<3) needed_files.insert(resource.file);
            for (const auto& span : resource.spans) needed_files.insert(span.file);
        }
        std::map<u64,std::vector<std::string>> candidates;
        size_t entries{};
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            Check(++entries <= 4096, "Local resource directory limit");
            std::error_code error;
            if (!entry.is_regular_file(error) || error) continue;
            const auto size=entry.file_size(error);
            if (error) continue;
            const auto name=entry.path().filename().u8string();
            candidates[size].emplace_back(reinterpret_cast<const char*>(name.data()),name.size());
        }
        for (auto& [size,names] : candidates) std::sort(names.begin(),names.end());
        std::map<std::string,std::string> fingerprints;
        for (const auto& source : source_files) {
            if (!needed_files.contains(source.name)) continue;
            Check(!progress || progress(0,groups.size()), "Shader profile operation cancelled");
            auto names=candidates[source.size];
            // Prefer the original logical name; a same-size, same-prefix file
            // elsewhere in the directory must not hide the exact local copy.
            const auto named=std::find(names.begin(),names.end(),source.name);
            if(named!=names.end()) std::rotate(names.begin(),named,named+1);
            for (const auto& name : names) {
                try {
                    auto found=fingerprints.find(name);
                    if(found==fingerprints.end())found=fingerprints.emplace(name,
                        Hash(Bytes(root,name,0,std::min<u64>(source.size,4096),timings),timings)).first;
                    if(found->second==source.header) { aliases[source.name]=name;break; }
                } catch (const std::runtime_error&) { /* Never follow an escaping link. */ }
            }
        }
        for (auto& resource : resources) {
            if (aliases.contains(resource.file)) resource.file = aliases.at(resource.file);
            for (auto& span : resource.spans) if (aliases.contains(span.file)) span.file = aliases.at(span.file);
        }
    }
    Timing::Timer resources_timer(timings, Timing::Phase::ResourceTotal);
    size_t completed_resources{};
    for (const auto& [rid, requests] : groups) {
        Check(!progress || progress(completed_resources++, groups.size()), "Shader profile operation cancelled");
        const auto& resource = resources[rid];
        bool missing{}, incompatible{};
        const auto unavailable = [&](const std::exception& error) {
            const auto* fs = dynamic_cast<const std::filesystem::filesystem_error*>(&error);
            if (fs && fs->code() == std::errc::no_such_file_or_directory) missing = true;
            else incompatible = true;
        };
        if (resource.kind == 0) {
            try {
                // Resolve and open a raw container once, then validate each
                // requested range independently so a truncated tail cannot
                // discard otherwise useful programs from the same file.
                const auto path=Local(root,resource.file);
                const auto length=std::filesystem::file_size(path);
                std::ifstream input(path,std::ios::binary);
                Check(bool(input),"Cannot open compact resource");
                for (auto id : requests) {
                    try {
                        const auto& p=programs[id];
                        Check(p.offset<=length && p.size<=length-p.offset,"Compact resource range");
                        std::string code(static_cast<size_t>(p.size),'\0');
                        {
                            Timing::Timer io(timings,Timing::Phase::ResourceIO);
                            input.clear(); input.seekg(static_cast<std::streamoff>(p.offset));
                            input.read(code.data(),code.size());
                            Check(bool(input),"Cannot read compact resource");
                            if(timings)timings->Add(Timing::Counter::ResourceReadBytes,p.size);
                        }
                        Check(Hash(code,timings)==p.hash,"Compact program fingerprint mismatch");
                        codes[id]=std::make_shared<const std::string>(std::move(code));
                    } catch (const std::runtime_error& error) { unavailable(error); }
                }
            } catch (const std::runtime_error& error) { unavailable(error); }
        } else {
            try {
                size_t prefix{};
                for (auto id : requests) prefix = std::max(prefix, static_cast<size_t>(programs[id].offset + programs[id].size));
                std::string raw;
                if (resource.kind >= 3) {
                    raw.reserve(resource.size);
                    for (const auto& span : resource.spans) raw += Bytes(root, span.file, span.offset, span.size, timings);
                } else raw = Bytes(root, resource.file, resource.offset, resource.size, timings);
                if (resource.kind >= 2) Check(Hash(raw, timings) == resource.hash, "Compact resource fingerprint mismatch");
                auto decoded = Timing::Measure(timings, Timing::Phase::ResourceDecompression, [&] {
                    return resource.kind == 1 || resource.kind == 4 ? DecodeYaz0(raw, prefix) :
                        resource.kind == 2 ? DecodeLz4(raw, resource.decoded_size) : std::move(raw);
                });
                if (timings) timings->Add(Timing::Counter::ResourceDecodedBytes, decoded.size());
                if (resource.kind == 2) Check(Hash(decoded, timings) == resource.segment_hash, "Compact NSO fingerprint mismatch");
                for (auto id : requests) {
                    const auto& p = programs[id];
                    if (p.offset > decoded.size() || p.size > decoded.size() - p.offset) { incompatible = true; continue; }
                    auto code = decoded.substr(p.offset, p.size);
                    if (Hash(code, timings) != p.hash) { incompatible = true; continue; }
                    codes[id] = std::make_shared<const std::string>(std::move(code));
                }
            } catch (const std::runtime_error& error) { unavailable(error); }
        }
        outcome.missing_resources += missing;
        outcome.incompatible_resources += incompatible;
    }
    resources_timer.Stop();
    Timing::Timer environment_timer(timings, Timing::Phase::EnvironmentAssembly);
    std::vector<SharedShaderBytes> assembled(envs.size());
    size_t env_budget{};
    for (size_t id=0;id<envs.size();++id)
    {
        const auto& env = envs[id];
        if (!codes[env.program]) continue;
        assembled[id] = Environment(env, codes[env.program], timings);
        env_budget += assembled[id].MetadataBytes();
        Check(env_budget <= 256 * 1024 * 1024, "Compact environment budget");
    }
    environment_timer.Stop();
    Timing::Timer recipe_timer(timings, Timing::Phase::RecipeAssembly);
    std::vector<Recipe> result;
    result.reserve(pending.size());
    std::set<std::pair<u32,u64>> verified_program_hashes;
    for (auto& p : pending) {
        if (!compatible_context(p.recipe) || (existing_recipes && existing_recipes->contains(p.recipe.identity))) continue;
        bool available = true;
        size_t env_index{};
        for (auto hash : p.recipe.shader_hashes) if (hash) {
            const auto id = p.ids.at(env_index++);
            const auto& code = codes[envs[id].program];
            if (!code) available = false;
            else if (verified_program_hashes.emplace(envs[id].program,hash).second)
                Check(MatchesProgramHash(*code, hash), "Recipe program hash mismatch");
        }
        Check(env_index == p.ids.size(), "Recipe program count mismatch");
        if (!available) { ++outcome.skipped; continue; }
        for (auto id : p.ids) {
            total_environment_bytes += sizeof(SharedShaderBytes);
            Check(total_environment_bytes <= 256 * 1024 * 1024, "Compact recipe memory budget");
            p.recipe.environments.push_back(assembled[id]);
        }
        p.recipe.requires_validation=p.recipe.source_context!=target_context;
        outcome.candidates+=p.recipe.requires_validation;
        result.push_back(std::move(p.recipe));
    }
    Check(!progress || progress(groups.size(),groups.size()), "Shader profile operation cancelled");
    outcome.accepted = result.size();
    if (report) *report = outcome;
    Check(!result.empty() || outcome.reused, "No compatible shader recipes: local resources are missing or incompatible");
    if (timings)
    {
        timings->Add(Timing::Counter::Programs, programs.size());
        timings->Add(Timing::Counter::Environments, envs.size());
        timings->Add(Timing::Counter::Recipes, result.size());
        timings->Add(Timing::Counter::ResourceGroups, groups.size());
    }
    return result;
}
std::vector<Recipe> ReadCompact(std::string_view payload,const std::filesystem::path& root,u64 title_id,
    size_t* unresolved_count,Timing::Metrics* timings,const ResourceProgress& progress, ImportReport* report, std::string_view target_context, const std::set<std::string>* existing_recipes, const std::map<std::string,std::shared_ptr<const std::string>>* existing_programs, bool allow_cross_version) {
    return ReadCompactImpl(payload,root,title_id,unresolved_count,timings,progress,false,report,target_context,existing_recipes,existing_programs,allow_cross_version);
}
ProfileInspection InspectCompact(std::string_view payload,u64 title_id) {
    ProfileInspection result;
    const auto recipes=ReadCompactImpl(payload,{},title_id,&result.unresolved,nullptr,{},true);
    for(const auto& recipe:recipes)if(recipe.compute)++result.compute;else ++result.graphics;
    return result;
}
ProfileInspection Inspect(const std::filesystem::path& path,u64 title_id) {
    const auto size=std::filesystem::file_size(path);
    Check(size<=33*1024*1024,"Profile size limit");
    std::ifstream file(path,std::ios::binary);std::string packed(static_cast<size_t>(size),'\0');
    Check(bool(file.read(packed.data(),static_cast<std::streamsize>(size))) && file.peek()==std::char_traits<char>::eof(),"Cannot read profile metadata");
    Check(packed.starts_with("NXSHADR2"),"Expected a compressed NXShader profile");
    auto result=InspectCompact(Unpack(packed),title_id);
    result.bytes=size;
    return result;
}
} // namespace VideoCommon::ShaderProfile
