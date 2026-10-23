// SPDX-License-Identifier: GPL-2.0-or-later
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "yuzu_video_core/pipeline_cache_store.h"
using namespace VideoCommon;
void require(bool value) { if(!value) throw std::runtime_error("cache store regression"); }
int main(int argc,char** argv) try {
    if(argc!=2) return 2;
    const std::filesystem::path path=argv[1];
    require(!std::filesystem::exists(path));
    std::string environment(68,'\0'); environment[0]=16; environment+=std::string(16,'A'); environment+="metadata";
    CacheStore::Record first{{environment},std::string(64,'K')};
    CacheStore::Append(path,20,first);
    const auto size=std::filesystem::file_size(path);
    CacheStore::Append(path,20,first); require(std::filesystem::file_size(path)==size);
    auto second=first; second.key[0]='L';second.environments[0].back()='B';
    CacheStore::Append(path,20,second);
    auto records=CacheStore::Read(path,20);require(records.size()==2);
    require(records[0].environments==first.environments && records[1].environments==second.environments);
    require(records[0].key==first.key && records[1].key==second.key);
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
    std::cout<<"code/environment/pipeline deduplication, combinations, truncated tail and integrity passed\n";
    std::filesystem::remove(path);std::filesystem::remove(bad);return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
