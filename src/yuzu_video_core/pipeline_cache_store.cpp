// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/pipeline_cache_store.h"
#include "common/sha256.h"
#include <array>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace VideoCommon::CacheStore {
namespace {
constexpr size_t MaxFile = 256 * 1024 * 1024;
constexpr size_t MaxFrame = 4 * 1024 * 1024;
constexpr size_t EnvHeader = 68;
constexpr std::string_view Magic = "NXSCACHE";
void Check(bool ok) { if (!ok) throw std::runtime_error("Invalid NXEmu local shader cache"); }
void Put(std::string& out, u64 value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) out.push_back(static_cast<char>(value >> (8*i)));
}
u64 Get(std::string_view bytes, size_t offset, size_t size) {
    Check(offset <= bytes.size() && size <= bytes.size()-offset);
    u64 value{};
    for (size_t i = 0; i < size; ++i)
        value |= u64{static_cast<unsigned char>(bytes[offset+i])} << (8*i);
    return value;
}
std::string Digest(std::string_view bytes) {
    SHA256 hash; hash.init();
    hash.update(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<unsigned int>(bytes.size()));
    std::string out(32, '\0'); hash.final(reinterpret_cast<unsigned char*>(out.data())); return out;
}
struct Index {
    size_t valid_bytes{};
    std::map<std::string, std::string> code, environments;
    std::set<std::string> pipelines;
    std::vector<Record> records;
};
std::string Header(u32 abi) {
    std::string out(Magic); Put(out, 1, 4); Put(out, abi, 4); return out;
}
Index Parse(const std::filesystem::path& path, u32 abi, bool records) {
    Index index;
    if (!std::filesystem::exists(path)) return index;
    const auto size = std::filesystem::file_size(path); Check(size <= MaxFile);
    std::ifstream file(path, std::ios::binary);
    std::string bytes(static_cast<size_t>(size), '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())); Check(bool(file));
    Check(bytes.size() >= 16 && bytes.substr(0,16) == Header(abi));
    size_t cursor = 16; index.valid_bytes = cursor;
    while (cursor < bytes.size()) {
        // A crash may interrupt only the last append. Preserve every complete prior frame.
        if (bytes.size()-cursor < 37) break;
        const auto type = static_cast<unsigned char>(bytes[cursor]);
        const auto length = static_cast<size_t>(Get(bytes,cursor+1,4));
        Check(length <= MaxFrame && type >= 1 && type <= 3);
        if (length > bytes.size()-cursor-37) break;
        const std::string id = bytes.substr(cursor+5,32);
        const std::string data = bytes.substr(cursor+37,length);
        Check(Digest(std::string(1, static_cast<char>(type))+data) == id);
        if (type == 1) {
            Check(!data.empty() && data.size()%8 == 0 && data.size() <= 1024*1024);
            index.code.emplace(id,data);
        } else if (type == 2) {
            Check(data.size() >= EnvHeader+32);
            const auto found = index.code.find(data.substr(EnvHeader,32)); Check(found != index.code.end());
            Check(Get(data,0,8) == found->second.size());
            auto env = data.substr(0,EnvHeader)+found->second+data.substr(EnvHeader+32);
            index.environments.emplace(id,std::move(env));
        } else {
            const auto count = static_cast<size_t>(Get(data,0,4)); Check(count >= 1 && count <= 6);
            const auto key_size = static_cast<size_t>(Get(data,4+32*count,4));
            Check(key_size > 0 && key_size <= 65536 && data.size() == 8+32*count+key_size);
            Record record;
            for (size_t i=0; i<count; ++i) {
                const auto found = index.environments.find(data.substr(4+32*i,32));
                Check(found != index.environments.end());
                if (records) record.environments.push_back(found->second);
            }
            if (records && index.pipelines.insert(id).second) {
                record.key = data.substr(8+32*count); index.records.push_back(std::move(record));
            } else index.pipelines.insert(id);
        }
        Check(index.code.size()+index.environments.size()+index.pipelines.size() <= 200000);
        cursor += 37+length; index.valid_bytes = cursor;
    }
    return index;
}
} // namespace
std::vector<Record> Read(const std::filesystem::path& path, u32 shader_abi) {
    return Parse(path,shader_abi,true).records;
}
void Append(const std::filesystem::path& path, u32 shader_abi, const Record& record) {
    static std::mutex mutex;
    struct Writer { Index index; size_t disk_size{}; u32 abi{}; };
    static std::map<std::filesystem::path, Writer> writers;
    std::scoped_lock lock(mutex);
    Check(!record.environments.empty() && record.environments.size() <= 6 &&
          !record.key.empty() && record.key.size() <= 65536);
    auto& writer = writers[path];
    const auto size = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
    if (!writer.disk_size || writer.disk_size != size || writer.abi != shader_abi) {
        writer.index = Parse(path,shader_abi,false); writer.abi = shader_abi;
        if (writer.index.valid_bytes && writer.index.valid_bytes != size)
            std::filesystem::resize_file(path,writer.index.valid_bytes);
    }
    try {
    std::string pending;
    if (!writer.index.valid_bytes) pending = Header(shader_abi);
    const auto frame = [&](unsigned char type, const std::string& data, auto& table) {
        Check(data.size() <= MaxFrame);
        const auto id = Digest(std::string(1,static_cast<char>(type))+data);
        if (!table.contains(id)) {
            pending.push_back(static_cast<char>(type)); Put(pending,data.size(),4);
            pending += id; pending += data;
        }
        return id;
    };
    // Build all frames before touching the file; validate the complete environment layout first.
    std::string pipeline; Put(pipeline,record.environments.size(),4);
    for (const auto& env : record.environments) {
        Check(env.size() >= EnvHeader);
        const auto code_size = static_cast<size_t>(Get(env,0,8));
        Check(code_size && code_size%8 == 0 && code_size <= 1024*1024 && code_size <= env.size()-EnvHeader);
        const auto code = env.substr(EnvHeader,code_size);
        const auto code_id = frame(1,code,writer.index.code);
        // Include additions immediately in a temporary index to deduplicate within this record too.
        if (!writer.index.code.contains(code_id)) {
            writer.index.code.emplace(code_id,std::string{});
        }
        const auto metadata = env.substr(0,EnvHeader)+code_id+env.substr(EnvHeader+code_size);
        const auto env_id = frame(2,metadata,writer.index.environments);
        if (!writer.index.environments.contains(env_id)) {
            writer.index.environments.emplace(env_id,std::string{});
        }
        pipeline += env_id;
    }
    Put(pipeline,record.key.size(),4); pipeline += record.key;
    const auto pipeline_id = frame(3,pipeline,writer.index.pipelines);
        Check(writer.index.valid_bytes+pending.size() <= MaxFile);
        std::ofstream out(path,std::ios::binary|std::ios::app);
        out.write(pending.data(),static_cast<std::streamsize>(pending.size())); out.flush(); Check(bool(out));
        writer.index.pipelines.insert(pipeline_id);
        writer.index.valid_bytes += pending.size(); writer.disk_size = writer.index.valid_bytes;
    } catch (...) { writers.erase(path); throw; }
}
} // namespace VideoCommon::CacheStore
