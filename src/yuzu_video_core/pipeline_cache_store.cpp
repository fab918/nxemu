// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/pipeline_cache_store.h"
#include "yuzu_video_core/shader_profile_file_stamp.h"
#include "yuzu_video_core/shader_profile_context.h"
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
    const auto digest = ShaderProfile::ResourceDigest(bytes);
    return {reinterpret_cast<const char*>(digest.data()), digest.size()};
}
struct ContextIndex { std::set<std::string> ids; size_t valid_bytes{}; std::filesystem::file_time_type modified{}; };
std::filesystem::path ContextPath(const std::filesystem::path& path, std::string_view context) {
    Check(ShaderProfile::ValidContext(context));
    auto result=path; result += ".context-" + std::string(context); return result;
}
ContextIndex ParseContext(const std::filesystem::path& path, std::string_view context) {
    ContextIndex index;
    if (!std::filesystem::exists(path)) return index;
    const auto size=std::filesystem::file_size(path);
    index.modified=std::filesystem::last_write_time(path);
    Check(size>=8 && size<=8+65536ULL*64);
    std::ifstream in(path,std::ios::binary);
    std::string header(8,'\0'); Check(bool(in.read(header.data(),8)) && header=="NXCTX001");
    index.valid_bytes=8;
    std::string frame(64,'\0');
    while(in.read(frame.data(),64)) {
        const auto id=frame.substr(0,32);
        Check(Digest(std::string(context)+id)==frame.substr(32));
        index.ids.insert(id); index.valid_bytes+=64;
    }
    Check(in.eof());
    return index;
}
void AppendContext(const std::filesystem::path& cache, std::string_view context, const std::string& id) {
    const auto path=ContextPath(cache,context);
    static std::filesystem::path active;
    static ContextIndex index;
    const auto size=std::filesystem::exists(path)?std::filesystem::file_size(path):0;
    const auto modified=size ? std::filesystem::last_write_time(path) : std::filesystem::file_time_type{};
    if(active!=path || index.valid_bytes!=size || index.modified!=modified) { index=ParseContext(path,context); active=path; }
    if(index.valid_bytes && index.valid_bytes!=size) std::filesystem::resize_file(path,index.valid_bytes);
    if(index.ids.contains(id)) return;
    Check(index.ids.size()<65536);
    try {
        std::ofstream out(path,std::ios::binary|std::ios::app);
        if(!index.valid_bytes) out.write("NXCTX001",8);
        const auto frame=id+Digest(std::string(context)+id);
        out.write(frame.data(),frame.size());out.flush();out.close();Check(bool(out));
        index.valid_bytes=(index.valid_bytes?index.valid_bytes:8)+64;
        index.ids.insert(id);
        index.modified=std::filesystem::last_write_time(path);
    } catch(...) { active.clear();index={};throw; }
}
struct Index
{
    size_t valid_bytes{};
    std::map<std::string, std::shared_ptr<const std::string>> code;
    std::map<std::string, SharedShaderBytes> environments;
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
                index.code.emplace(id, records ? std::make_shared<const std::string>(std::move(data)) : nullptr);
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
                const auto expanded = data.size() - 32;
                Check(expanded <= limits.index_bytes - table_bytes);
                table_bytes += expanded;
                auto env = records ? SharedShaderBytes::FromStore(data,found->second) : SharedShaderBytes{};
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
                    Check(sizeof(SharedShaderBytes) <= limits.record_bytes - record_bytes);
                    record_bytes += sizeof(SharedShaderBytes);
                    record.environments.push_back(found->second);
                }
            }
            if (records && fresh)
            {
                Check(key_size <= limits.record_bytes - record_bytes);
                record_bytes += key_size;
                record.key = data.substr(8 + 32 * count);
                record.id = id;
                index.records.push_back(std::move(record));
            }
        }
        Check(index.code.size() + index.environments.size() + index.pipelines.size() <= 200000);
        cursor += 37 + length;
        index.valid_bytes = cursor;
    }
    return index;
}
std::mutex writer_mutex;
struct Writer
{
    Index index;
    size_t disk_size{};
    std::filesystem::file_time_type modified{};
    u32 abi{};
    // Weak ownership distinguishes reused addresses without retaining shader
    // bytes after an import. Memoization never trusts a mutable flat buffer.
    std::map<std::weak_ptr<const std::string>,std::string,std::owner_less<>> shared_code_ids;
};
std::filesystem::path writer_path;
Writer writer;

void SyncWriter(const std::filesystem::path& path,u32 shader_abi) {
    if (writer_path != path)
    {
        writer = Writer{};
        writer_path = path;
    }
    const auto size = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
    const auto modified = size ? std::filesystem::last_write_time(path) : std::filesystem::file_time_type{};
    if (!writer.disk_size || writer.disk_size != size || writer.modified != modified || writer.abi != shader_abi)
    {
        writer.index = Parse(path, shader_abi, false);
        writer.shared_code_ids.clear();
        writer.abi = shader_abi;
        if (writer.index.valid_bytes && writer.index.valid_bytes != size)
            std::filesystem::resize_file(path, writer.index.valid_bytes);
    }
    writer.disk_size=writer.index.valid_bytes;
    writer.modified=writer.disk_size?std::filesystem::last_write_time(path):std::filesystem::file_time_type{};
}
} // namespace
std::vector<Record> Read(const std::filesystem::path & path, u32 shader_abi, const ReadLimits& limits,
                         std::set<std::string>* record_ids)
{
    auto parsed=Parse(path, shader_abi, true, limits);
    if(record_ids)*record_ids=std::move(parsed.pipelines);
    return std::move(parsed.records);
}
void Append(const std::filesystem::path & path, u32 shader_abi, const Record & record,
            std::string* record_id, std::string_view context)
{
    std::scoped_lock lock(writer_mutex);
    Check(!record.environments.empty() && record.environments.size() <= 6 &&
          !record.key.empty() && record.key.size() <= 65536);
    SyncWriter(path,shader_abi);
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
            const auto code_size = static_cast<size_t>(Get(env.substr(0,8), 0, 8));
            Check(code_size && code_size % 8 == 0 && code_size <= 1024 * 1024 && code_size <= env.size() - EnvHeader);
            const auto program=env.ProgramStorage();
            Check(!program || program->size()==code_size);
            const std::weak_ptr<const std::string> owner(program);
            const auto known=program ? writer.shared_code_ids.find(owner) : writer.shared_code_ids.end();
            const auto code_id = known!=writer.shared_code_ids.end() ? known->second :
                frame(1, env.substr(EnvHeader, code_size), writer.index.code);
            if(program && known==writer.shared_code_ids.end()) {
                if(writer.shared_code_ids.size()>=16384)writer.shared_code_ids.clear();
                writer.shared_code_ids.emplace(owner,code_id);
            }
            // Include additions immediately in a temporary index to deduplicate within this record too.
            if (!writer.index.code.contains(code_id))
            {
                writer.index.code.emplace(code_id, nullptr);
                writer.index.code_sizes.emplace(code_id, code_size);
            }
            const auto metadata = env.substr(0, EnvHeader) + code_id + env.substr(EnvHeader + code_size);
            const auto env_id = frame(2, metadata, writer.index.environments);
            if (!writer.index.environments.contains(env_id))
            {
                writer.index.environments.emplace(env_id, SharedShaderBytes{});
            }
            pipeline += env_id;
        }
        Put(pipeline, record.key.size(), 4);
        pipeline += record.key;
        const auto pipeline_id = frame(3, pipeline, writer.index.pipelines);
        Check(writer.index.code.size()+writer.index.environments.size()+writer.index.pipelines.size()+1<=200000);
        if (writer.index.valid_bytes + pending.size() > MaxFile)
            throw std::runtime_error("Local shader cache reached its 256 MiB limit; new entries were not written");
        std::ofstream out(path, std::ios::binary | std::ios::app);
        out.write(pending.data(), static_cast<std::streamsize>(pending.size()));
        out.flush();
        Check(bool(out));
        writer.index.pipelines.insert(pipeline_id);
        writer.index.valid_bytes += pending.size();
        writer.disk_size = writer.index.valid_bytes;
        writer.modified = std::filesystem::last_write_time(path);
        if (!context.empty()) AppendContext(path,context,pipeline_id);
        if(record_id)*record_id=pipeline_id;
    }
    catch (...)
    {
        writer = Writer{};
        writer_path.clear();
        throw;
    }
}

bool AssociateContext(const std::filesystem::path& path,u32 shader_abi,
                      const std::string& record_id,std::string_view context) {
    std::scoped_lock lock(writer_mutex);
    Check(record_id.size()==32 && ShaderProfile::ValidContext(context));
    SyncWriter(path,shader_abi);
    if(!writer.index.pipelines.contains(record_id))return false;
    AppendContext(path,context,record_id);
    return true;
}

std::set<std::string> ReadContext(const std::filesystem::path& path, std::string_view context) {
    return ParseContext(ContextPath(path,context),context).ids;
}

} // namespace VideoCommon::CacheStore
