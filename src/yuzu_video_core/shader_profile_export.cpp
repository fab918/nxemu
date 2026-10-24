// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/shader_profile_export.h"
#include "yuzu_video_core/shader_profile_context.h"
#include "yuzu_video_core/shader_profile_provenance.h"
#include "yuzu_video_core/pipeline_cache_store.h"
#include "yuzu_video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "yuzu_video_core/shader_profile_codec.h"
#include "yuzu_video_core/shader_profile_compact.h"
#include "yuzu_video_core/shader_profile_discovery.h"
#include "yuzu_video_core/shader_profile_hash.h"
#include "yuzu_video_core/shader_profile_journal.h"
#include "yuzu_video_core/shader_profile_resources.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <utility>

namespace VideoCommon::ShaderProfile
{
namespace
{
void Require(bool ok, const char * reason)
{
    if (!ok) throw std::runtime_error(reason);
}
using Object = std::map<std::string, JsonValue>;
JsonValue Array()
{
    return JsonValue(JsonValueType::Array);
}
JsonValue Num(u64 n)
{
    return n > UINT32_MAX ? JsonValue(std::to_string(n)) : JsonValue(static_cast<u32>(n));
}
u64 Number(const JsonValue & n)
{
    return n.isString() ? std::stoull(n.asString()) : n.asUInt64();
}
struct Cursor
{
    std::string_view bytes;
    size_t pos{};
    std::string Take(size_t n)
    {
        Require(pos <= bytes.size() && n <= bytes.size() - pos, "Truncated local shader cache");
        auto s = std::string(bytes.substr(pos, n));
        pos += n;
        return s;
    }
    u64 Get(size_t n)
    {
        const auto s = Take(n);
        u64 v{};
        for (size_t i = 0; i < n; ++i)
            v |= u64{static_cast<unsigned char>(s[i])} << (8 * i);
        return v;
    }
};
std::string Load(const std::filesystem::path & p, size_t limit = 256 * 1024 * 1024)
{
    const auto size = std::filesystem::file_size(p);
    Require(size <= limit, "Offline input size limit");
    std::string bytes(static_cast<size_t>(size), '\0');
    std::ifstream in(p, std::ios::binary);
    in.read(bytes.data(), static_cast<std::streamsize>(size));
    Require(bool(in), "Cannot read offline input");
    return bytes;
}
struct Pipeline
{
    std::string key;
    std::vector<std::string> environments;
    bool compute{};
};
struct Model
{
    JsonValue profile{JsonValueType::Object};
    std::map<std::string, std::string> codes;
    std::vector<Pipeline> pipelines;
};
const std::array<const char *, 7> MetaNames{"local_memory_size", "texture_bound", "guest_start_address", "guest_read_lowest", "guest_read_highest", "viewport_transform_state", "stage"};
const std::array<const char *, 4> MapNames{"texture_types", "texture_pixel_formats", "cbuf_values", "cbuf_replacements"};

std::string ReadEnvironment(Cursor & input, Model & model)
{
    const auto begin = input.pos;
    const auto size = input.Get(8);
    std::array<u64, 4> counts;
    for (auto & n : counts)
    {
        n = input.Get(8);
        Require(n <= 65536, "Environment map limit");
    }
    Require(size && size % 8 == 0 && size <= 1024 * 1024, "Environment program size");
    JsonValue env(JsonValueType::Object);
    for (auto name : MetaNames)
        env[name] = Num(input.Get(4));
    const auto stage = Number(env["stage"]);
    Require(stage <= 6, "Invalid environment stage");
    Require(Number(env["guest_read_highest"]) >= Number(env["guest_read_lowest"]) &&
                Number(env["guest_read_highest"]) - Number(env["guest_read_lowest"]) + 8 == size,
            "Inconsistent shader bounds");
    auto code = input.Take(static_cast<size_t>(size));
    const auto digest = ResourceSha256(code);
    for (size_t i = 0; i < 4; ++i)
    {
        env[MapNames[i]] = Array();
        std::set<u64> keys;
        for (u64 n = 0; n < counts[i]; ++n)
        {
            const auto key = input.Get(i < 2 ? 4 : 8);
            Require(keys.insert(key).second, "Duplicate environment entry");
            JsonValue pair = Array();
            pair.Append(Num(key));
            pair.Append(Num(input.Get(4)));
            env[MapNames[i]].Append(std::move(pair));
        }
    }
    if (stage == 5)
    {
        env["workgroup_size"] = Array();
        for (unsigned i = 0; i < 3; ++i)
            env["workgroup_size"].Append(Num(input.Get(4)));
        env["shared_memory_size"] = Num(input.Get(4));
    }
    else
    {
        const auto header = input.Take(80);
        Require(code.size() >= 80 && header == code.substr(0, 80), "Shader header/code mismatch");
        env["program_header_sha256"] = ResourceSha256(header);
        if (stage == 3)
        {
            env["gp_passthrough_mask"] = Array();
            for (unsigned i = 0; i < 8; ++i)
                env["gp_passthrough_mask"].Append(Num(input.Get(4)));
        }
    }
    Require(input.pos > begin, "Empty environment");
    env["shader"] = digest;
    const auto id = ResourceSha256(Canonical(env));
    model.profile["environments"][id] = env;
    model.profile["shaders"][digest]["size"] = Num(size);
    model.profile["shaders"][digest]["references"] = Array();
    model.codes.emplace(digest, std::move(code));
    return id;
}
Model ReadCache(const ExportOptions & options)
{
    Model model;
    model.profile["environments"] = JsonValue(JsonValueType::Object);
    model.profile["shaders"] = JsonValue(JsonValueType::Object);
    std::ifstream source(options.cache,std::ios::binary);
    std::string magic(8,'\0');source.read(magic.data(),magic.size());
    Require(bool(source),"Truncated local cache header");
    if (magic == "NXSCACHE")
    {
        const auto contextual = options.context.empty() ? std::set<std::string>{} : CacheStore::ReadContext(options.cache, options.context);
        for (const auto & record : CacheStore::Read(options.cache, 20))
        {
            if (!options.context.empty() && !contextual.contains(record.id)) continue;
            Pipeline p;
            p.key = record.key;
            for (const auto & env : record.environments)
            {
                const auto serialized=env.String();
                Cursor c{serialized};
                p.environments.push_back(ReadEnvironment(c, model));
                Require(c.pos == env.size(), "Trailing environment data");
            }
            p.compute = Number(model.profile["environments"][p.environments.front()]["stage"]) == 5;
            Require(p.key.size() == (p.compute ? 64 : 1400), "Unsupported local pipeline ABI");
            model.pipelines.push_back(std::move(p));
        }
    }
    else throw std::runtime_error("Expected an NXEmu local shader store");
    Require(model.pipelines.size() <= 65536, "Pipeline table limit");
    return model;
}
u64 At(const std::string & bytes, size_t pos, size_t count)
{
    Cursor c{bytes, pos};
    return c.Get(count);
}
void Put(std::string & bytes, size_t pos, u64 value, size_t count)
{
    Require(pos + count <= bytes.size(), "Pipeline key bounds");
    for (size_t i = 0; i < count; ++i)
        bytes[pos + i] = static_cast<char>(value >> (8 * i));
}
using Masks = std::map<u64, u32>;
std::string Normalize(std::string key, const Masks & dependencies)
{
    const auto mask = [&](u64 h) {const auto it=dependencies.find(h);return it==dependencies.end()?0:it->second; };
    auto state2 = At(key, 420, 4);
    if (!(state2 & (1 << 5))) Put(key, 420, state2 & ~u64(31 << 6), 4);
    bool pool = false;
    for (size_t stage = 0; stage < 5; ++stage)
    {
        const auto m = mask(At(key, 8 * (stage + 1), 8)) | (stage == 0 ? mask(At(key, 0, 8)) : 0);
        for (size_t bank = 0; bank < 18; ++bank)
            if (!(m & (1U << bank))) Put(key, 48 + (stage * 18 + bank) * 4, 0, 4);
    }
    for (size_t i = 0; i < 6; ++i)
        pool |= (mask(At(key, i * 8, 8)) & (1U << 31)) != 0;
    if (!pool) Put(key, 408, 0, 8);
    Vulkan::FixedPipelineState state;
    std::memcpy(&state, key.data() + 416, sizeof(state));
    return key.substr(0, 416 + state.Size());
}
JsonValue Assemble(Model & model, const ExportOptions & options, ExportReport & report)
{
    const auto recording = [&] {
        if (options.recording.extension() != ".nxr")
            return ParseMetadata(Load(options.recording, 32 * 1024 * 1024));
        auto failure_path = options.recording;
        failure_path += ".failed";
        Require(!std::filesystem::exists(failure_path), "Automatic guest recording failed; see renderer log");
        const auto journal = Journal::Read(options.recording);
        Require(!journal.incomplete_tail, "Incomplete recipe journal; start and stop the game to recover its final frame");
        JsonValue result(JsonValueType::Object);
        result["feature_bits"] = 0U;
        result["guest_state_word_count"] = 278U;
        result["recipes"] = Array();
        for (const auto & entry : journal.entries)
        {
            JsonValue r(JsonValueType::Object);
            r["source_feature_bits"] = entry[0];
            r["words"] = Array();
            for (size_t i = 1; i < entry.size(); ++i)
                r["words"].Append(entry[i]);
            result["recipes"].Append(std::move(r));
        }
        return result;
    }();
    Require(!recording["failed"].asBool() && Number(recording["projection_mismatches"]) == 0 && Number(recording["dropped_observations"]) == 0, "Incomplete guest recipe recording");
    Require(recording.isMember("feature_bits") && Number(recording["feature_bits"]) <= 127 && Number(recording["guest_state_word_count"]) == 278, "Unsupported guest recording ABI");
    const auto bits = Number(recording["feature_bits"]);
    Masks dependencies;
    for (const auto & p : model.pipelines)
        if (!p.compute)
        {
            size_t stage = 0;
            for (size_t i = 0; i < 6; ++i)
                if (const auto hash = At(p.key, i * 8, 8))
                {
                    Require(stage < p.environments.size(), "Pipeline environment/hash mismatch");
                    const auto & env = model.profile["environments"][p.environments[stage++]];
                    for (const auto & entry : env["cbuf_values"])
                    {
                        const auto key = Number(entry[0]), bank = key >> 32, offset = key & UINT32_MAX;
                        if (offset == UINT32_MAX && bank < 32) dependencies[hash] |= 1U << bank;
                        if (key == UINT64_MAX - 1) dependencies[hash] |= 1U << 31;
                    }
                }
            Require(stage == p.environments.size(), "Pipeline stage/hash mismatch");
        }
    std::map<std::string, std::vector<std::vector<std::string>>> source;
    JsonValue recipes = Array(), unresolved = Array();
    std::set<std::string> seen;
    for (const auto & p : model.pipelines)
    {
        bool located = true;
        for (const auto & id : p.environments)
            located &= !model.profile["shaders"][model.profile["environments"][id]["shader"].asString()]["references"].empty();
        if (!located)
        {
            Require(options.allow_partial, "Pipeline has unlocated shaders");
            continue;
        }
        if (p.compute)
        {
            Cursor key{p.key};
            JsonValue r(JsonValueType::Object);
            r["kind"] = "compute";
            r["shader_hash"] = Num(key.Get(8));
            r["shared_memory_size"] = Num(key.Get(4));
            for (const auto field : {"workgroup_size", "cbuf_sizes"})
            {
                r[field] = Array();
                for (unsigned i = 0; i < (std::string_view(field) == "workgroup_size" ? 3U : 8U); ++i)
                    r[field].Append(Num(key.Get(4)));
            }
            r["texture_table_size"] = Num(key.Get(4));
            r["environments"] = Array();
            for (const auto & id : p.environments)
                r["environments"].Append(id);
            if (seen.insert(Canonical(r)).second)
            {
                recipes.Append(std::move(r));
                ++report.compute;
            }
        }
        else
        {
            const auto key = Normalize(p.key, dependencies);
            auto & variants = source[key];
            if (std::find(variants.begin(), variants.end(), p.environments) == variants.end()) variants.push_back(p.environments);
        }
    }
    JsonValue graphics = Array();
    Require(recording["recipes"].isArray() && recording["recipes"].size() <= 65536, "Guest observation limit");
    for (const auto & observation : recording["recipes"])
    {
        const auto source_bits = observation.isMember("source_feature_bits") ? Number(observation["source_feature_bits"]) : bits;
        Require(source_bits <= 127, "Unsupported source feature bits");
        const Vulkan::DynamicFeatures observation_features{bool(source_bits & 1), bool(source_bits & 2), bool(source_bits & 4), bool(source_bits & 8), bool(source_bits & 16), bool(source_bits & 32), bool(source_bits & 64)};
        const auto & w = observation["words"];
        Require(w.isArray() && w.size() == 382, "Guest observation width");
        std::vector<u32> words;
        words.reserve(382);
        for (const auto & n : w)
        {
            Require(n.Type() == JsonValueType::UnsignedInt || (n.Type() == JsonValueType::Int && n.asInt64() >= 0), "Guest word type");
            Require(n.asUInt64() <= UINT32_MAX, "Guest word overflow");
            words.push_back(static_cast<u32>(n.asUInt64()));
        }
        auto guest = Vulkan::GuestPipelineState::FromWords(std::span(words.data(), 278));
        Require(guest.has_value(), "Invalid guest state");
        const auto projected = guest->Project(observation_features);
        std::string key(1400, '\0');
        for (size_t i = 0; i < 104; ++i)
            Put(key, i * 4, words[278 + i], 4);
        std::memcpy(key.data() + 416, &projected, sizeof(projected));
        key = Normalize(std::move(key), dependencies);
        JsonValue r(JsonValueType::Object);
        r["kind"] = "graphics";
        r["guest_words"] = Array();
        for (size_t i = 0; i < 278; ++i)
            r["guest_words"].Append(words[i]);
        r["shader_hashes"] = Array();
        for (size_t i = 0; i < 6; ++i)
            r["shader_hashes"].Append(Num(u64{words[278 + 2 * i]} | (u64{words[279 + 2 * i]} << 32)));
        r["cbuf_sizes"] = Array();
        for (size_t stage = 0; stage < 5; ++stage)
        {
            JsonValue sizes = Array();
            for (size_t bank = 0; bank < 18; ++bank)
                sizes.Append(Num(At(key, 48 + (stage * 18 + bank) * 4, 4)));
            r["cbuf_sizes"].Append(std::move(sizes));
        }
        r["texture_pool"] = Array();
        r["texture_pool"].Append(Num(At(key, 408, 4)));
        r["texture_pool"].Append(Num(At(key, 412, 4)));
        auto found = source.find(key);
        bool hle_alias = false;
        if (found == source.end())
        {
            // Runtime already permits a plain draw to share a replacement-free
            // HLE pipeline. Recover the same directed alias at export, keeping
            // every other normalized byte (programs, sizes, pool and state).
            Vulkan::FixedPipelineState state{};
            state.raw2 = static_cast<u32>(At(key, 420, 4));
            using Hint = Tegra::Engines::Maxwell3D::EngineHint;
            if (state.app_stage == Hint::None)
            {
                state.app_stage.Assign(Hint::OnHLEMacro);
                auto twin = key;
                Put(twin, 420, state.raw2, 4);
                found = source.find(twin);
                hle_alias = found != source.end();
            }
        }
        bool matched = false;
        if (found != source.end())
        {
            for (auto it = found->second.rbegin(); it != found->second.rend(); ++it)
            {
                // Never drop real HLE replacements, nor infer that a plain
                // capture proves a future HLE draw has no replacements.
                if (hle_alias && std::any_of(it->begin(), it->end(), [&](const auto& id) {
                        return !model.profile["environments"][id]["cbuf_replacements"].empty();
                    }))
                    continue;
                matched = true;
                r["environments"] = Array();
                for (const auto & id : *it)
                    r["environments"].Append(id);
                if (seen.insert(Canonical(r)).second)
                {
                    graphics.Append(r);
                    ++report.graphics;
                }
            }
        }
        if (!matched)
        {
            r["reason"] = "no_serialized_environment_for_source_key";
            unresolved.Append(std::move(r));
            ++report.unresolved;
        }
        else if (hle_alias)
            ++report.resolved_hle_aliases;
    }
    for (const auto & r : std::as_const(recipes))
        graphics.Append(r);
    JsonValue result(JsonValueType::Object);
    result["format"] = "nxshader";
    result["version"] = 2U;
    result["guest_state_version"] = 1U;
    char title[17];
    std::snprintf(title, sizeof(title), "%016llx", static_cast<unsigned long long>(options.title_id));
    result["title_id"] = title;
    result["content_label"] = options.label;
    result["source_files"] = model.profile["source_files"];
    result["source_context"] = options.context;
    result["shaders"] = JsonValue(JsonValueType::Object);
    result["environments"] = JsonValue(JsonValueType::Object);
    result["recipes"] = graphics;
    result["unresolved_recipes"] = unresolved;
    for (const auto & r : std::as_const(graphics))
        for (const auto & id : r["environments"])
        {
            const auto env = model.profile["environments"][id.asString()];
            result["environments"][id.asString()] = env;
            result["shaders"][env["shader"].asString()] = model.profile["shaders"][env["shader"].asString()];
        }
    Require(!graphics.empty(), "No reconstructible recipes");
    report.programs = result["shaders"].size();
    report.environments = result["environments"].size();
    return result;
}
} // namespace

std::string Canonical(const JsonValue & value)
{
    const auto styled = JsonStyledWriter().write(value);
    std::string result;
    result.reserve(styled.size());
    bool quoted = false, escaped = false;
    for (char c : styled)
    {
        if (quoted)
        {
            result += c;
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        }
        else if (c == '"')
        {
            quoted = true;
            result += c;
        }
        else if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            result += c;
    }
    return result;
}
JsonValue ParseMetadata(std::string_view text, size_t limit)
{
    Require(text.size() <= limit, "Metadata size limit");
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (char c : text)
    {
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        }
        else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[')
            Require(++depth <= 32, "Metadata depth limit");
        else if (c == '}' || c == ']')
        {
            Require(depth > 0, "Metadata structure");
            --depth;
        }
    }
    Require(!quoted && depth == 0, "Incomplete metadata");
    JsonValue value;
    Require(JsonReader().Parse(text.data(), text.data() + text.size(), value), "Invalid metadata JSON");
    return value;
}
void Publish(const std::filesystem::path & output, std::string_view bytes)
{
    Require(!std::filesystem::exists(output), "Output already exists");
    auto temporary = output;
    temporary += ".partial";
    Require(!std::filesystem::exists(temporary), "Output temporary already exists");
    try
    {
        std::ofstream out(temporary, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        Require(bool(out), "Cannot write profile");
        out.close();
        Require(!std::filesystem::exists(output), "Output already exists");
#ifdef _WIN32
        Require(MoveFileW(temporary.c_str(), output.c_str()) != 0, "Cannot publish profile (output may already exist)");
#else
        Require(link(temporary.c_str(), output.c_str()) == 0, "Cannot publish profile (output may already exist)");
        std::filesystem::remove(temporary);
#endif
    }
    catch (...)
    {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
}
ExportReport Export(const ExportOptions & requested)
{
    const auto started = std::chrono::steady_clock::now();
    auto options = requested;
    const auto progress = [&](const char * phase, u64 done, u64 total) {
        Require(!options.progress || options.progress(phase, done, total), "Shader profile operation cancelled");
    };
    progress("identify-content", 0, 0);
    if (options.automatic_sources) {
        ResolveRecordedSources(options);
        options.label = RecordedVersionLabel(RecordedProvenance(options.cache.parent_path(), options.context, options.title_id));
    }
    Require(options.title_id && !options.base.empty(), "Missing game identity");
    Require(!std::filesystem::exists(options.output), "Output already exists");
    progress("read-cache", 0, 0);
    auto model = ReadCache(options);
    ExportReport report;
    Discover(options, model.profile, model.codes, report);
    progress("assemble", 0, 0);
    const auto profile = Assemble(model, options, report);
    progress("compress", 0, 0);
    const auto bytes = PackCompact(profile);
    progress("write-profile", 0, bytes.size());
    Publish(options.output, bytes);
    report.bytes = bytes.size();
    report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return report;
}
} // namespace VideoCommon::ShaderProfile
