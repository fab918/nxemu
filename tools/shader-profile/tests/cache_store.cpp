// SPDX-License-Identifier: GPL-2.0-or-later
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "yuzu_video_core/pipeline_cache_store.h"
#include "yuzu_video_core/shader_profile_import_status.h"
using namespace VideoCommon;
void require(bool value) { if(!value) throw std::runtime_error("cache store regression"); }
int main(int argc,char** argv) try {
    if(argc!=2) return 2;
    const std::filesystem::path path=argv[1];
    require(!std::filesystem::exists(path));
    std::string environment(68,'\0'); environment[0]=16; environment+=std::string(16,'A'); environment+="metadata";
    CacheStore::Record first{{environment},std::string(64,'K')};
    std::string first_id,second_id;
    CacheStore::Append(path,20,first,&first_id);
    const auto size=std::filesystem::file_size(path);
    CacheStore::Append(path,20,first); require(std::filesystem::file_size(path)==size);
    auto second=first; second.key[0]='L';second.environments[0].back()='B';
    CacheStore::Append(path,20,second,&second_id);
    std::set<std::string> present_ids;
    CacheStore::Read(path,20,{},&present_ids);
    JsonValue receipt;
    receipt["schema"]=1U;receipt["stamp"]="identity";receipt["selection"]="selected";
    receipt["record_count"]=2U;receipt["record_ids"]=JsonValue(JsonValueType::Array);
    receipt["record_ids"].Append(ShaderProfile::LocalRecordHex(first_id));
    receipt["record_ids"].Append(ShaderProfile::LocalRecordHex(second_id));
    require(ShaderProfile::IntegratedRecordsPresent(receipt,"identity","selected",present_ids));
    require(!ShaderProfile::IntegratedRecordsPresent(receipt,"other","selected",present_ids));
    require(!ShaderProfile::IntegratedRecordsPresent(receipt,"identity","new-selection",present_ids));
    // A cleanly truncated cache remains valid and nonempty, but is not complete.
    std::filesystem::resize_file(path,size);
    require(CacheStore::Read(path,20,{},&present_ids).size()==1);
    require(!ShaderProfile::IntegratedRecordsPresent(receipt,"identity","selected",present_ids));
    CacheStore::Append(path,20,second);
    CacheStore::Read(path,20,{},&present_ids);
    require(ShaderProfile::IntegratedRecordsPresent(receipt,"identity","selected",present_ids));
    auto invalid_receipt=receipt;invalid_receipt["record_count"]="2";
    require(!ShaderProfile::IntegratedRecordsPresent(invalid_receipt,"identity","selected",present_ids));
    invalid_receipt=receipt;invalid_receipt["record_ids"][1]=ShaderProfile::LocalRecordHex(first_id);
    require(!ShaderProfile::IntegratedRecordsPresent(invalid_receipt,"identity","selected",present_ids));
    require(!ShaderProfile::ShouldReconstructProfile(false,false,true,"selected","selected"));
    require(!ShaderProfile::ShouldReconstructProfile(false,false,false,"selected","selected"));
    require(ShaderProfile::ShouldReconstructProfile(false,false,false,"new-selection","selected"));
    require(ShaderProfile::ShouldReconstructProfile(false,true,false,"selected","selected"));
    require(ShaderProfile::ShouldReconstructProfile(true,false,false,"selected","selected"));
    require(!ShaderProfile::ShouldReconstructProfile(true,false,true,"selected","selected"));
    require(!ShaderProfile::ShouldReconstructProfile(true,true,true,"selected","selected"));
    // Failure state is local, bounded, strictly validated and never permanent.
    const auto failed_id=std::string(64,'a');
    ShaderProfile::NativeWarmupFailures failures("gpu-driver-renderer-cache-A");
    require(!failures.Defer(failed_id));
    failures.Complete(failed_id,false);
    auto persisted=failures.Save();
    ShaderProfile::NativeWarmupFailures next("gpu-driver-renderer-cache-A");
    next.Load(persisted);require(next.Defer(failed_id));
    persisted=next.Save();next.Load(persisted);require(!next.Defer(failed_id));
    next.Complete(failed_id,false);
    require(next.Defer(failed_id));require(next.Defer(failed_id));
    require(!next.Defer(failed_id));
    next.Complete(failed_id,true);require(next.Save()["entries"].size()==0);
    ShaderProfile::NativeWarmupFailures changed("gpu-driver-renderer-cache-B");
    changed.Load(failures.Save());require(!changed.Defer(failed_id));
    auto invalid=failures.Save();invalid["entries"][0]["remaining"]=99U;
    next.Load(invalid);require(!next.Defer(failed_id));
    invalid=failures.Save();invalid["entries"][0]["failures"]="1";
    next.Load(invalid);require(!next.Defer(failed_id));
    invalid=failures.Save();invalid["entries"].Append(invalid["entries"][0]);
    next.Load(invalid);require(!next.Defer(failed_id));
    auto records=CacheStore::Read(path,20);require(records.size()==2);
    require(records[0].environments==first.environments && records[1].environments==second.environments);
    require(records[0].key==first.key && records[1].key==second.key);
    bool limited=false;try{CacheStore::Read(path,20,CacheStore::ReadLimits{1024,100});}catch(const std::exception&){limited=true;}require(limited);
    limited=false;try{CacheStore::Read(path,20,CacheStore::ReadLimits{1,1024});}catch(const std::exception&){limited=true;}require(limited);
    require(CacheStore::Read(path,20).size()==2); // Budget failure never alters the source.
    // Same code, different metadata and key: one code frame, two env and two pipeline frames.
    std::ifstream file(path,std::ios::binary);std::string data((std::istreambuf_iterator<char>(file)),{});
    file.close();
    size_t cursor=16;unsigned code_count=0,env_count=0,pipeline_count=0;
    while(cursor<data.size()) {
        const auto type=data[cursor];size_t length{};
        for(size_t i=0;i<4;++i) length|=size_t{static_cast<unsigned char>(data[cursor+1+i])}<<(8*i);
        code_count+=type==1;env_count+=type==2;pipeline_count+=type==3;cursor+=37+length;
    }
    require(code_count==1 && env_count==2 && pipeline_count==2);
    {std::ofstream tail(path,std::ios::binary|std::ios::app);tail<<"partial";}
    require(CacheStore::Read(path,20).size()==2);
    CacheStore::Append(path,20,second);require(CacheStore::Read(path,20).size()==2);
    auto bad=path;bad+=".bad";data.back()^=1;
    {std::ofstream out(bad,std::ios::binary);out.write(data.data(),static_cast<std::streamsize>(data.size()));}
    bool rejected=false;try{CacheStore::Read(bad,20);}catch(const std::exception&){rejected=true;}require(rejected);
    auto legacy=path;legacy+=".legacy.bin";
    auto migrated=path;migrated+=".migrated.nxc";
    const auto put=[](std::string& out,u64 value,size_t count){for(size_t i=0;i<count;++i)out.push_back(static_cast<char>(value>>(i*8)));};
    std::string legacy_bytes="yuzucach";put(legacy_bytes,20,4);
    std::string env;put(env,96,8);for(int i=0;i<4;++i)put(env,0,8);
    for(u32 n:{0U,0U,0U,0U,88U,1U,5U})put(env,n,4);
    env+=std::string(96,'S');for(u32 n:{1U,1U,1U,0U})put(env,n,4);
    put(legacy_bytes,1,4);legacy_bytes+=env;legacy_bytes+=std::string(64,'K');
    auto alternate=env;alternate.back()=1; // Same key, distinct compilation environment.
    put(legacy_bytes,1,4);legacy_bytes+=alternate;legacy_bytes+=std::string(64,'K');
    {std::ofstream out(legacy,std::ios::binary);out<<legacy_bytes;}
    CacheStore::Append(migrated,20,first);
    require(CacheStore::MigrateLegacyVulkan(legacy,migrated,20));
    require(CacheStore::Read(migrated,20).size()==3);
    require(!CacheStore::MigrateLegacyVulkan(legacy,migrated,20));
    {std::ifstream in(legacy,std::ios::binary);const std::string saved((std::istreambuf_iterator<char>(in)),{});require(saved==legacy_bytes);}
    // Switching back to an existing writer rebuilds only its ID index.
    const auto before=std::filesystem::file_size(path);CacheStore::Append(path,20,first);
    require(std::filesystem::file_size(path)==before);
    std::filesystem::remove(legacy);std::filesystem::remove(migrated);
    auto stamp=migrated;stamp+=".legacy-source";std::filesystem::remove(stamp);
    std::cout<<"code/environment/pipeline deduplication, combinations, truncated tail, integrity and verified non-destructive legacy migration passed\n";
    std::filesystem::remove(path);std::filesystem::remove(bad);return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
