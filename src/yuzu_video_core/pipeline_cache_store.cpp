// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/pipeline_cache_store.h"
#include "common/sha256.h"
#include <array>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace VideoCommon::CacheStore
{
namespace
{
constexpr size_t MaxFile = 256 * 1024 * 1024;
constexpr size_t MaxFrame = 4 * 1024 * 1024;
constexpr size_t EnvHeader = 68;
constexpr std::string_view Magic = "NXSCACHE";
void Check(bool ok)
{
    if (!ok) throw std::runtime_error("Invalid NXEmu local shader cache");
}
void Put(std::string & out, u64 value, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
        out.push_back(static_cast<char>(value >> (8 * i)));
}
u64 Get(std::string_view bytes, size_t offset, size_t size)
{
    Check(offset <= bytes.size() && size <= bytes.size() - offset);
    u64 value{};
    for (size_t i = 0; i < size; ++i)
        value |= u64{static_cast<unsigned char>(bytes[offset + i])} << (8 * i);
    return value;
}
std::string Digest(std::string_view bytes)
{
    SHA256 hash;
    hash.init();
    hash.update(reinterpret_cast<const unsigned char *>(bytes.data()), static_cast<unsigned int>(bytes.size()));
    std::string out(32, '\0');
    hash.final(reinterpret_cast<unsigned char *>(out.data()));
    return out;
}
struct Index
{
    size_t valid_bytes{};
    std::map<std::string, std::string> code, environments;
    std::map<std::string, size_t> code_sizes;
    std::set<std::string> pipelines;
    std::vector<Record> records;
};
std::string Header(u32 abi)
{
    std::string out(Magic);
    Put(out, 1, 4);
    Put(out, abi, 4);
    return out;
}
Index Parse(const std::filesystem::path & path, u32 abi, bool records, const ReadLimits& limits = {})
{
    Index index;
    if (!std::filesystem::exists(path)) return index;
    const auto size = std::filesystem::file_size(path);
    Check(size <= MaxFile);
    std::ifstream file(path, std::ios::binary);
    std::string header(16, '\0');
    file.read(header.data(), header.size());
    Check(bool(file));
    Check(header == Header(abi));
    size_t cursor = 16;
    index.valid_bytes = cursor;
    size_t table_bytes{}, record_bytes{};
    while (cursor < size)
    {
        // Read bounded frames rather than retaining an additional full-file copy.
        if (size - cursor < 37) break;
        std::string frame_header(37, '\0');
        file.read(frame_header.data(), frame_header.size());
        Check(bool(file));
        const auto type = static_cast<unsigned char>(frame_header[0]);
        const auto length = static_cast<size_t>(Get(frame_header, 1, 4));
        Check(length <= MaxFrame && type >= 1 && type <= 3);
        if (length > size - cursor - 37) break;
        const std::string id = frame_header.substr(5, 32);
        std::string data(length, '\0');
        file.read(data.data(), length);
        Check(bool(file));
        Check(Digest(std::string(1, static_cast<char>(type)) + data) == id);
        if (type == 1)
        {
            Check(!data.empty() && data.size() % 8 == 0 && data.size() <= 1024 * 1024);
            if (!index.code.contains(id))
            {
                Check(data.size() <= limits.index_bytes - table_bytes);
                table_bytes += data.size();
                index.code_sizes.emplace(id, data.size());
                index.code.emplace(id, records ? std::move(data) : std::string{});
            }
        }
        else if (type == 2)
        {
            Check(data.size() >= EnvHeader + 32);
            const auto found = index.code.find(data.substr(EnvHeader, 32));
            Check(found != index.code.end());
            Check(Get(data, 0, 8) == index.code_sizes.at(found->first));
            if (!index.environments.contains(id))
            {
                const auto expanded = index.code_sizes.at(found->first) + data.size() - 32;
                Check(expanded <= limits.index_bytes - table_bytes);
                table_bytes += expanded;
                auto env = records ? data.substr(0, EnvHeader) + found->second + data.substr(EnvHeader + 32) : std::string{};
                index.environments.emplace(id, std::move(env));
            }
        }
        else
        {
            const auto count = static_cast<size_t>(Get(data, 0, 4));
            Check(count >= 1 && count <= 6);
            const auto key_size = static_cast<size_t>(Get(data, 4 + 32 * count, 4));
            Check(key_size > 0 && key_size <= 65536 && data.size() == 8 + 32 * count + key_size);
            const bool fresh = index.pipelines.insert(id).second;
            Record record;
            for (size_t i = 0; i < count; ++i)
            {
                const auto found = index.environments.find(data.substr(4 + 32 * i, 32));
                Check(found != index.environments.end());
                if (records && fresh)
                {
                    Check(found->second.size() <= limits.record_bytes - record_bytes);
                    record_bytes += found->second.size();
                    record.environments.push_back(found->second);
                }
            }
            if (records && fresh)
            {
                record.key = data.substr(8 + 32 * count);
                index.records.push_back(std::move(record));
            }
        }
        Check(index.code.size() + index.environments.size() + index.pipelines.size() <= 200000);
        cursor += 37 + length;
        index.valid_bytes = cursor;
    }
    return index;
}
} // namespace
std::vector<Record> Read(const std::filesystem::path & path, u32 shader_abi, const ReadLimits& limits)
{
    return Parse(path, shader_abi, true, limits).records;
}
void Append(const std::filesystem::path & path, u32 shader_abi, const Record & record)
{
    static std::mutex mutex;
    struct Writer
    {
        Index index;
        size_t disk_size{};
        u32 abi{};
    };
    static std::filesystem::path writer_path;
    static Writer writer;
    std::scoped_lock lock(mutex);
    Check(!record.environments.empty() && record.environments.size() <= 6 &&
          !record.key.empty() && record.key.size() <= 65536);
    if (writer_path != path)
    {
        writer = Writer{};
        writer_path = path;
    }
    const auto size = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
    if (!writer.disk_size || writer.disk_size != size || writer.abi != shader_abi)
    {
        writer.index = Parse(path, shader_abi, false);
        writer.abi = shader_abi;
        if (writer.index.valid_bytes && writer.index.valid_bytes != size)
            std::filesystem::resize_file(path, writer.index.valid_bytes);
    }
    try
    {
        std::string pending;
        if (!writer.index.valid_bytes) pending = Header(shader_abi);
        const auto frame = [&](unsigned char type, const std::string & data, auto & table) {
            Check(data.size() <= MaxFrame);
            const auto id = Digest(std::string(1, static_cast<char>(type)) + data);
            if (!table.contains(id))
            {
                pending.push_back(static_cast<char>(type));
                Put(pending, data.size(), 4);
                pending += id;
                pending += data;
            }
            return id;
        };
        // Build all frames before touching the file; validate the complete environment layout first.
        std::string pipeline;
        Put(pipeline, record.environments.size(), 4);
        for (const auto & env : record.environments)
        {
            Check(env.size() >= EnvHeader);
            const auto code_size = static_cast<size_t>(Get(env, 0, 8));
            Check(code_size && code_size % 8 == 0 && code_size <= 1024 * 1024 && code_size <= env.size() - EnvHeader);
            const auto code = env.substr(EnvHeader, code_size);
            const auto code_id = frame(1, code, writer.index.code);
            // Include additions immediately in a temporary index to deduplicate within this record too.
            if (!writer.index.code.contains(code_id))
            {
                writer.index.code.emplace(code_id, std::string{});
                writer.index.code_sizes.emplace(code_id, code_size);
            }
            const auto metadata = env.substr(0, EnvHeader) + code_id + env.substr(EnvHeader + code_size);
            const auto env_id = frame(2, metadata, writer.index.environments);
            if (!writer.index.environments.contains(env_id))
            {
                writer.index.environments.emplace(env_id, std::string{});
            }
            pipeline += env_id;
        }
        Put(pipeline, record.key.size(), 4);
        pipeline += record.key;
        const auto pipeline_id = frame(3, pipeline, writer.index.pipelines);
        if (writer.index.valid_bytes + pending.size() > MaxFile)
            throw std::runtime_error("Local shader cache reached its 256 MiB limit; new entries were not written");
        std::ofstream out(path, std::ios::binary | std::ios::app);
        out.write(pending.data(), static_cast<std::streamsize>(pending.size()));
        out.flush();
        Check(bool(out));
        writer.index.pipelines.insert(pipeline_id);
        writer.index.valid_bytes += pending.size();
        writer.disk_size = writer.index.valid_bytes;
    }
    catch (...)
    {
        writer = Writer{};
        writer_path.clear();
        throw;
    }
}

std::vector<Record> ReadLegacyVulkan(const std::filesystem::path & path, u32 shader_abi)
{
    Check(shader_abi == 20 && std::filesystem::file_size(path) <= MaxFile);
    std::ifstream in(path, std::ios::binary);
    size_t consumed{};
    const auto take = [&](size_t count) {
        Check(count <= MaxFrame && count <= MaxFile - consumed);
        consumed += count;
        std::string bytes(count, '\0');
        in.read(bytes.data(), count);
        Check(bool(in));
        return bytes;
    };
    const auto header = take(12);
    Check(header.substr(0, 8) == "yuzucach" && Get(header, 8, 4) == shader_abi);
    std::vector<Record> result;
    size_t expanded{};
    while (in.peek() != std::char_traits<char>::eof())
    {
        const auto count = Get(take(4), 0, 4);
        Check(count >= 1 && count <= 6);
        Record record;
        bool compute{};
        for (size_t i = 0; i < count; ++i)
        {
            auto env = take(EnvHeader);
            const auto code_size = Get(env, 0, 8), stage = Get(env, 64, 4);
            Check(code_size && code_size % 8 == 0 && code_size <= 1024 * 1024 && stage <= 6);
            if (i == 0) compute = stage == 5;
            Check((stage == 5) == compute && (!compute || count == 1));
            size_t maps{};
            for (size_t m = 0; m < 4; ++m)
            {
                const auto entries = Get(env, 8 + m * 8, 8);
                Check(entries <= 65536);
                maps += static_cast<size_t>(entries) * (m < 2 ? 8 : 12);
            }
            const auto tail = maps + (compute ? 16 : 80) + (stage == 3 ? 32 : 0);
            Check(code_size + tail <= MaxFrame);
            env += take(static_cast<size_t>(code_size) + tail);
            Check(env.size() <= 256 * 1024 * 1024ULL - expanded);
            expanded += env.size();
            record.environments.push_back(std::move(env));
        }
        record.key = take(compute ? 64 : 1400);
        result.push_back(std::move(record));
        Check(result.size() <= 65536);
    }
    Check(!in.bad());
    return result;
}

bool MigrateLegacyVulkan(const std::filesystem::path & legacy,
                         const std::filesystem::path & destination, u32 shader_abi)
{
    if (!std::filesystem::exists(legacy)) return false;
    const auto stamp = [&] {
        return "legacy-vulkan-1:" + std::to_string(shader_abi) + ":" +
               std::to_string(std::filesystem::file_size(legacy)) + ":" +
               std::to_string(std::filesystem::last_write_time(legacy).time_since_epoch().count());
    };
    const auto source_stamp = stamp();
    auto marker = destination;
    marker += ".legacy-source";
    if (std::filesystem::exists(destination) && std::filesystem::exists(marker) && std::filesystem::file_size(marker) <= 256)
    {
        std::ifstream in(marker, std::ios::binary);
        const std::string saved((std::istreambuf_iterator<char>(in)), {});
        if (saved == source_stamp) return false;
    }
    auto records = Read(destination, shader_abi);
    auto old = ReadLegacyVulkan(legacy, shader_abi);
    for (auto & record : old)
        records.push_back(std::move(record));
    old.clear();
    const auto signature = [](const Record & record) {
        SHA256 hash;
        hash.init();
        const auto update = [&](std::string_view bytes) { hash.update(reinterpret_cast<const unsigned char *>(bytes.data()), static_cast<unsigned int>(bytes.size())); };
        std::string count;
        Put(count, record.environments.size(), 8);
        update(count);
        for (const auto & env : record.environments)
        {
            std::string length;
            Put(length, env.size(), 8);
            update(length);
            update(env);
        }
        std::string length;
        Put(length, record.key.size(), 8);
        update(length);
        update(record.key);
        std::string out(32, '\0');
        hash.final(reinterpret_cast<unsigned char *>(out.data()));
        return out;
    };
    std::set<std::string> expected;
    for (const auto & record : records)
        expected.insert(signature(record));
    if (records.empty()) return false;
    auto temporary = destination;
    temporary += ".migrating-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Check(!std::filesystem::exists(temporary));
    const auto replace = [](const auto & from, const auto & to) {
#ifdef _WIN32
        Check(MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0);
#else
        std::filesystem::rename(from, to);
#endif
    };
    try
    {
        for (const auto & record : records)
            Append(temporary, shader_abi, record);
        records.clear();
        records.shrink_to_fit();
        std::set<std::string> actual;
        for (const auto & record : Read(temporary, shader_abi))
            actual.insert(signature(record));
        Check(expected == actual && stamp() == source_stamp);
        replace(temporary, destination);
        auto marker_temp = marker;
        marker_temp += ".partial";
        std::ofstream out(marker_temp, std::ios::binary | std::ios::trunc);
        out << source_stamp;
        out.flush();
        Check(bool(out));
        out.close();
        replace(marker_temp, marker);
    }
    catch (...)
    {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
    return true;
}
} // namespace VideoCommon::CacheStore
