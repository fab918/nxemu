// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/shader_profile.h"
#include "yuzu_video_core/shader_profile_compact.h"
#include "yuzu_video_core/shader_profile_resources.h"
#include "yuzu_video_core/shader_profile_codec.h"
#include "yuzu_video_core/shader_profile_metrics.h"
#include "yuzu_video_core/shader_profile_hash.h"
#include "common/json.h"
#include "common/sha256.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <cstring>

namespace VideoCommon::ShaderProfile {
using Timing::Phase;
using Timing::Counter;
using Timing::Measure;
namespace {
constexpr size_t MaxResource = 512*1024*1024;
void Check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
u64 Number(const JsonValue& v) {
    // 64-bit values are decimal strings in v2: JSON readers must never round shader hashes.
    if (v.isString()) {
        const auto text=v.asString();
        Check(!text.empty() && text.size()<=20 && text.find_first_not_of("0123456789")==std::string::npos,
              "Invalid decimal profile integer");
        return std::stoull(text);
    }
    Check(v.Type() == JsonValueType::UnsignedInt ||
          (v.Type() == JsonValueType::Int && v.asInt64() >= 0), "Expected unsigned profile integer");
    return v.asUInt64();
}
u32 Word(const JsonValue& v) {
    const auto n=Number(v); Check(n <= UINT32_MAX,"Profile integer overflow"); return static_cast<u32>(n);
}
std::string Text(const JsonValue& v) { Check(v.isString(),"Expected profile string"); return v.asString(); }
void Fields(const JsonValue& v, std::initializer_list<const char*> allowed) {
    Check(v.isObject(),"Expected profile object");
    for (const auto& name : v.GetMemberNames()) {
        Check(std::find(allowed.begin(),allowed.end(),name) != allowed.end(),"Unknown profile field");
    }
}
void DigestName(const std::string& name) {
    Check(name.size()==64 && name.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid SHA-256");
}
std::string Hash(const std::string& bytes, Timing::Metrics* timings=nullptr) {
    return Measure(timings,Phase::ResourceVerification,[&] { return ResourceSha256(bytes); });
}
std::string Bytes(const std::filesystem::path& path, u64 offset, u64 size, size_t limit=MaxResource,
                  Timing::Metrics* timings=nullptr) {
    Timing::Timer timer(timings,Phase::ResourceIO);
    Check(size <= limit,"Profile resource too large");
    const auto total=std::filesystem::file_size(path);
    Check(offset <= total && size <= total-offset,"Profile resource bounds");
    std::ifstream file(path,std::ios::binary); file.seekg(static_cast<std::streamoff>(offset));
    std::string out(static_cast<size_t>(size),'\0');
    file.read(out.data(),static_cast<std::streamsize>(size)); Check(bool(file),"Cannot read profile resource");
    if(timings) timings->Add(Counter::ResourceReadBytes,size);
    return out;
}
std::filesystem::path LocalFile(const std::filesystem::path& root, const JsonValue& value) {
    const auto name=Text(value);
    Check(!name.empty() && name.find('\0')==std::string::npos,"Invalid resource name");
    // Only basenames are portable. Neither relative traversal nor platform-specific paths are accepted.
    Check(name.find_first_of("/\\:")==std::string::npos && name!="." && name!="..","Resource must be a basename");
    const auto file=std::filesystem::canonical(root/std::filesystem::u8path(name));
    Check(file.parent_path()==std::filesystem::canonical(root) && std::filesystem::is_regular_file(file),
          "Resource outside game directory"); return file;
}
void Put(std::string& out,u64 value,size_t size) {
    for(size_t i=0;i<size;++i) out.push_back(static_cast<char>(value>>(8*i)));
}
std::string Environment(const JsonValue& env,const std::string& code) {
    Fields(env,{"shader","local_memory_size","texture_bound","guest_start_address","guest_read_lowest",
        "guest_read_highest","viewport_transform_state","stage","texture_types","texture_pixel_formats",
        "cbuf_values","cbuf_replacements","workgroup_size","shared_memory_size","program_header_sha256",
        "gp_passthrough_mask"});
    const auto stage=Word(env["stage"]); Check(stage<=6,"Invalid shader stage");
    const auto low=Word(env["guest_read_lowest"]),high=Word(env["guest_read_highest"]);
    Check(high>=low && u64{high}-low+8==code.size(),"Environment instruction bounds");
    Check(Word(env["guest_start_address"])==low,"Unsupported non-contiguous shader environment");
    const std::array<const char*,4> maps{"texture_types","texture_pixel_formats","cbuf_values","cbuf_replacements"};
    std::string out; Put(out,code.size(),8);
    for(const auto name:maps) { Check(env[name].isArray() && env[name].size()<=65536,"Invalid environment map"); Put(out,env[name].size(),8); }
    for(const auto name:{"local_memory_size","texture_bound","guest_start_address","guest_read_lowest",
                         "guest_read_highest","viewport_transform_state","stage"}) Put(out,Word(env[name]),4);
    out+=code;
    for(size_t i=0;i<maps.size();++i) {
        std::set<u64> keys;
        for(const auto& entry:env[maps[i]]) {
            Check(entry.isArray() && entry.size()==2,"Invalid environment map entry");
            const auto key=Number(entry[0]); Check(keys.insert(key).second,"Duplicate environment map key");
            Check(i>=2 || key<=UINT32_MAX,"Environment map key overflow");
            Put(out,key,i<2?4:8); Put(out,Word(entry[1]),4);
        }
    }
    if(stage==5) {
        Check(env["workgroup_size"].isArray() && env["workgroup_size"].size()==3,"Invalid compute dimensions");
        for(const auto& n:env["workgroup_size"]) Put(out,Word(n),4);
        Put(out,Word(env["shared_memory_size"]),4);
    } else {
        Check(code.size()>=80 && Hash(code.substr(0,80))==Text(env["program_header_sha256"]),"Program header fingerprint mismatch");
        out+=code.substr(0,80);
        if(stage==3) {
            Check(env["gp_passthrough_mask"].isArray() && env["gp_passthrough_mask"].size()==8,"Invalid geometry mask");
            for(const auto& n:env["gp_passthrough_mask"]) Put(out,Word(n),4);
        }
    }
    return out;
}
template<size_t N> void Words(const JsonValue& value,std::array<u32,N>& out) {
    Check(value.isArray() && value.size()==N,"Invalid recipe array size");
    for(size_t i=0;i<N;++i) out[i]=Word(value[static_cast<uint32_t>(i)]);
}
} // namespace

std::vector<Recipe> Read(const std::filesystem::path& path,const std::filesystem::path& root,u64 title_id,
                         size_t* unresolved_count, Timing::Metrics* timings,const ResourceProgress& progress) {
    Timing::Timer total(timings,Phase::ReaderTotal);
    const auto packed=Measure(timings,Phase::ProfileIO,[&] {
        return Bytes(path,0,std::filesystem::file_size(path),33*1024*1024);
    });
    const auto text=Measure(timings,Phase::ProfileDecompression,[&] { return Unpack(packed); });
    if(timings) {
        timings->Add(Counter::ProfileBytes,packed.size());
        if(!text.starts_with("NXSHBIN3"))timings->Add(Counter::JsonBytes,text.size());
    }
    if(text.starts_with("NXSHBIN3")) return ReadCompact(text,root,title_id,unresolved_count,timings,progress);
    Timing::Timer json_timer(timings,Phase::JsonParse);
    unsigned depth{}; bool quoted{},escaped{};
    for(const auto c:text) {
        if(quoted) { if(escaped) escaped=false; else if(c=='\\') escaped=true; else if(c=='"') quoted=false; }
        else if(c=='"') quoted=true;
        else if(c=='{' || c=='[') { Check(++depth<=32,"Profile nesting limit"); }
        else if(c=='}' || c==']') { Check(depth!=0,"Invalid profile nesting"); --depth; }
    }
    JsonValue profile; Check(JsonReader().Parse(text.data(),text.data()+text.size(),profile),"Invalid nxshader JSON");
    json_timer.Stop();
    Timing::Timer schema_timer(timings,Phase::SchemaAndLocators);
    Fields(profile,{"format","version","title_id","guest_state_version","content_label","shaders","environments","recipes","unresolved_recipes"});
    Check(Text(profile["format"])=="nxshader" && Word(profile["version"])==2 &&
          Word(profile["guest_state_version"])==1,"Unsupported nxshader version");
    const auto title=Text(profile["title_id"]);
    Check(title.size()==16 && title.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos &&
          std::stoull(title,nullptr,16)==title_id,"nxshader title mismatch");
    Check(profile["shaders"].isObject() && profile["shaders"].size()<=16384 &&
          profile["environments"].isObject() && profile["environments"].size()<=65536 &&
          profile["recipes"].isArray() && profile["recipes"].size()<=65536,"Profile table limit");
    const JsonValue& unresolved=profile["unresolved_recipes"];
    Check(unresolved.isNull() || (unresolved.isArray() && unresolved.size()<=65536),"Unresolved recipe table limit");
    for(const auto& r:unresolved) {
        Fields(r,{"kind","guest_words","shader_hashes","cbuf_sizes","texture_pool","reason"});
        Check(Text(r["kind"])=="graphics" && r["guest_words"].isArray() && r["guest_words"].size()<=512 &&
              Text(r["reason"])=="no_serialized_environment_for_source_key","Invalid unresolved recipe");
        for(const auto& value:r["guest_words"]) Word(value);
        Check(r["shader_hashes"].isArray() && r["shader_hashes"].size()==6,"Invalid unresolved stage hashes");
        for(const auto& value:r["shader_hashes"]) Number(value);
        std::array<u32,18> sizes; std::array<u32,2> pool;
        Check(r["cbuf_sizes"].isArray() && r["cbuf_sizes"].size()==5,"Invalid unresolved cbuf sizes");
        for(const auto& stage:r["cbuf_sizes"]) Words(stage,sizes);
        Words(r["texture_pool"],pool);
    }
    if(unresolved_count) *unresolved_count=unresolved.size();
    std::map<std::string,std::string> codes;
    struct Request { std::string digest; JsonValue ref; size_t size; };
    std::map<std::string,std::vector<Request>> groups;
    for(const auto& digest:profile["shaders"].GetMemberNames()) {
        DigestName(digest); const auto& shader=profile["shaders"][digest];
        Fields(shader,{"size","references"});
        const auto size=Number(shader["size"]); Check(size && size%8==0 && size<=1024*1024,"Invalid shader size");
        Check(shader["references"].isArray() && !shader["references"].empty(),"Shader has no local locator");
        // The portable exporter emits one verified locator per shader. Alternative locators are not implicit fallbacks.
        Check(shader["references"].size()==1,"Expected one verified locator");
        const auto& ref=shader["references"][0];
        Fields(ref,{"encoding","file","offset","size","container_offset","container_size","decoded_size",
                    "container_sha256","segment_sha256","physical_spans","resource_size","resource_sha256"});
        Check(Number(ref["size"])==size,"Locator shader size mismatch");
        const auto encoding=Text(ref["encoding"]);
        std::string identity=encoding+":";
        if(encoding=="raw") identity+=Text(ref["file"])+":"+std::to_string(Number(ref["offset"]))+":"+std::to_string(size);
        else if(encoding=="yaz0" || encoding=="nso-lz4") identity+=Text(ref["file"])+":"+
            std::to_string(Number(ref["container_offset"]))+":"+std::to_string(Number(ref["container_size"]));
        else if(encoding=="bktr-raw" || encoding=="bktr-yaz0") identity+=Text(ref["resource_sha256"]);
        else throw std::runtime_error("Unsupported shader locator encoding");
        groups[identity].push_back({digest,ref,static_cast<size_t>(size)});
    }
    schema_timer.Stop();
    if(timings) {
        timings->Add(Counter::ResourceGroups,groups.size());
        timings->Add(Counter::Programs,profile["shaders"].size());
    }
    Timing::Timer resource_timer(timings,Phase::ResourceTotal);
    size_t total_code_bytes{};
    size_t completed_resources{};
    for(const auto& [identity,requests]:groups) {
        Check(!progress || progress(completed_resources++,groups.size()),"Shader profile operation cancelled");
        const auto& ref=requests.front().ref; const auto encoding=Text(ref["encoding"]); std::string raw,decoded;
        if(encoding=="raw") raw=Bytes(LocalFile(root,ref["file"]),Number(ref["offset"]),Number(ref["size"]),MaxResource,timings);
        else if(encoding=="bktr-raw" || encoding=="bktr-yaz0") {
            Check(ref["physical_spans"].isArray() && ref["physical_spans"].size()<=65536,"Invalid BKTR spans");
            const auto expected=Number(ref["resource_size"]); Check(expected<=MaxResource,"BKTR resource limit");
            for(const auto& span:ref["physical_spans"]) {
                Fields(span,{"file","offset","size"}); const auto size=Number(span["size"]);
                Check(size<=expected-raw.size(),"BKTR span bounds");
                raw+=Bytes(LocalFile(root,span["file"]),Number(span["offset"]),size,MaxResource,timings);
            }
            Check(raw.size()==expected,"Incomplete BKTR spans");
        } else raw=Bytes(LocalFile(root,ref["file"]),Number(ref["container_offset"]),Number(ref["container_size"]),MaxResource,timings);
        const auto raw_hash=Hash(raw,timings);
        if(encoding=="bktr-raw" || encoding=="bktr-yaz0") {
            for(const auto& request:requests)
                Check(raw_hash==Text(request.ref["resource_sha256"]),"BKTR resource fingerprint mismatch");
        }
        size_t prefix=MaxResource+1;
        if(encoding=="bktr-yaz0") {
            prefix=0;
            for(const auto& request:requests) {
                const auto offset=Number(request.ref["offset"]);
                Check(offset<=MaxResource && request.size<=MaxResource-offset,"Shader locator bounds");
                prefix=std::max(prefix,static_cast<size_t>(offset)+request.size);
            }
        }
        if(encoding=="yaz0" || encoding=="bktr-yaz0")
            decoded=Measure(timings,Phase::ResourceDecompression,[&] { return DecodeYaz0(raw,prefix); });
        else if(encoding=="nso-lz4")
            decoded=Measure(timings,Phase::ResourceDecompression,[&] { return DecodeLz4(raw,static_cast<size_t>(Number(ref["decoded_size"]))); });
        else decoded=raw;
        if(timings) timings->Add(Counter::ResourceDecodedBytes,decoded.size());
        const auto decoded_hash=encoding=="nso-lz4"?Hash(decoded,timings):std::string{};
        for(const auto& request:requests) {
            const auto& r=request.ref;
            if(encoding=="nso-lz4") Check(raw_hash==Text(r["container_sha256"]) &&
                decoded_hash==Text(r["segment_sha256"]),"NSO resource fingerprint mismatch");
            const auto offset=encoding=="raw"?0:Number(r["offset"]);
            Check(offset<=decoded.size() && request.size<=decoded.size()-offset,"Shader locator bounds");
            auto code=decoded.substr(static_cast<size_t>(offset),request.size);
            Check(Hash(code,timings)==request.digest,"Shader fingerprint mismatch");
            total_code_bytes+=code.size();Check(total_code_bytes<=128*1024*1024,"Profile shader memory limit");
            codes.emplace(request.digest,std::move(code));
        }
    }
    resource_timer.Stop();
    Timing::Timer environment_timer(timings,Phase::EnvironmentAssembly);
    std::map<std::string,std::string> environments;
    std::map<std::string,u32> stages;
    size_t environment_table_bytes{};
    for(const auto& id:profile["environments"].GetMemberNames()) {
        DigestName(id); const auto& env=profile["environments"][id]; const auto shader=Text(env["shader"]);
        Check(codes.contains(shader),"Unknown environment shader");
        auto bytes=Environment(env,codes.at(shader));
        environment_table_bytes+=bytes.size();
        Check(environment_table_bytes<=256*1024*1024,"Profile environment table memory limit");
        environments.emplace(id,std::move(bytes)); stages[id]=Word(env["stage"]);
    }
    environment_timer.Stop();
    if(timings) timings->Add(Counter::Environments,environments.size());
    Timing::Timer recipe_timer(timings,Phase::RecipeAssembly);
    std::vector<Recipe> result;
    const JsonValue& recipe_array=profile["recipes"];
    size_t total_environment_bytes{};
    for(const auto& r:recipe_array) {
        Fields(r,{"kind","guest_words","shader_hashes","cbuf_sizes","texture_pool","environments",
            "shader_hash","shared_memory_size","workgroup_size","texture_table_size"});
        Recipe recipe; const auto kind=Text(r["kind"]); Check(kind=="graphics" || kind=="compute","Invalid recipe kind");
        recipe.compute=kind=="compute";
        Check(r["environments"].isArray() && r["environments"].size()>=1 && r["environments"].size()<=6,"Invalid recipe stages");
        for(const auto& value:r["environments"]) {
            const auto id=Text(value); Check(environments.contains(id),"Unknown recipe environment");
            Check(recipe.compute == (stages.at(id)==5),"Recipe/environment stage mismatch");
            total_environment_bytes+=environments.at(id).size();
            Check(total_environment_bytes<=256*1024*1024,"Profile environment memory limit");
            recipe.environments.push_back(environments.at(id));
        }
        if(recipe.compute) {
            Check(recipe.environments.size()==1,"Compute stage count"); recipe.shader_hashes[0]=Number(r["shader_hash"]);
            recipe.shared_memory_size=Word(r["shared_memory_size"]); Words(r["workgroup_size"],recipe.workgroup_size);
            Words(r["cbuf_sizes"],recipe.compute_cbuf_sizes); recipe.texture_table_size=Word(r["texture_table_size"]);
        } else {
            Check(r["guest_words"].isArray() && r["guest_words"].size()<=512,"Invalid guest state");
            for(const auto& word:r["guest_words"]) recipe.guest_words.push_back(Word(word));
            Check(r["shader_hashes"].isArray() && r["shader_hashes"].size()==6,"Invalid stage hashes");
            size_t active{};
            for(size_t i=0;i<6;++i) { recipe.shader_hashes[i]=Number(r["shader_hashes"][static_cast<uint32_t>(i)]); active+=recipe.shader_hashes[i]!=0; }
            Check(active==recipe.environments.size(),"Active shader count mismatch");
            Check(r["cbuf_sizes"].isArray() && r["cbuf_sizes"].size()==5,"Invalid cbuf stages");
            for(size_t i=0;i<5;++i) Words(r["cbuf_sizes"][static_cast<uint32_t>(i)],recipe.cbuf_sizes[i]);
            Words(r["texture_pool"],recipe.texture_pool);
        }
        result.push_back(std::move(recipe));
    }
    recipe_timer.Stop();
    if(timings) timings->Add(Counter::Recipes,result.size());
    return result;
}
} // namespace VideoCommon::ShaderProfile
