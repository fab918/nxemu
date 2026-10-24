// SPDX-License-Identifier: GPL-2.0-or-later
// Offline capacity analysis of bounded, hashed graphics-input traces. These
// hashes are diagnostic identifiers, never evidence for recipe equivalence.
#include "yuzu_common/cityhash.h"
#include <array>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <cstdint>
using Key=std::array<uint64_t,10>;
struct Raw { uint64_t hash{},id{}; };
struct State { Key key{};bool valid{}; };
int main(int argc,char** argv)try {
    if(argc!=2)throw std::runtime_error("expected input-key trace");
    std::ifstream in(argv[1],std::ios::binary);
    std::array<char,8> magic{};in.read(magic.data(),magic.size());
    if(std::string_view(magic.data(),magic.size())!="NXITR001")throw std::runtime_error("trace header");
    std::vector<Key> rows;Key row{};
    while(in.read(reinterpret_cast<char*>(row.data()),sizeof(row))) {
        if(rows.size()==524288)throw std::runtime_error("trace limit");
        rows.push_back(row);
    }
    if(!in.eof() || in.gcount()!=0)throw std::runtime_error("truncated trace");
    std::set<Key> unique(rows.begin(),rows.end());
    std::cout<<"{\"observations\":"<<rows.size()<<",\"unique_input_hash_keys\":"<<unique.size()<<",\"simulations\":[";
    bool comma{};
    for(const auto capacities:{std::array<size_t,2>{256,256},{512,1024},{1024,2048},{2048,4096},{0,2048}}) {
        const auto raw_slots=capacities[0],state_slots=capacities[1];
        std::array<std::vector<Raw>,8> groups;
        std::array<std::vector<size_t>,8> raw_next;
        for(size_t g=0;g<8;++g){const auto slots=raw_slots?raw_slots:(g==1 || g==7?1024:256);groups[g].resize(slots);raw_next[g].resize(slots/4);}
        std::vector<State> states(state_slots);std::vector<size_t> next(state_slots/4);
        uint64_t generation{},misses{},raw_misses{};
        for(const auto& inputs:rows) {
            Key key=inputs;
            for(size_t g=0;g<8;++g){
                const auto i=g==7?9:g;const auto hash=inputs[i];
                const auto slots=groups[g].size();
                const auto set=hash%(slots/4),begin=set*4;
                auto& group=groups[g];size_t selected=slots;
                for(size_t way=0;way<4;++way)
                    if(group[begin+way].id && group[begin+way].hash==hash){selected=begin+way;break;}
                if(selected==slots){selected=begin+raw_next[g][set]++%4;group[selected]={hash,++generation};++raw_misses;}
                key[i]=group[selected].id;
            }
            const auto set=Common::CityHash64(reinterpret_cast<const char*>(key.data()),sizeof(key))%(state_slots/4),begin=set*4;
            bool hit{};
            for(size_t way=0;way<4;++way)if(states[begin+way].valid && states[begin+way].key==key){hit=true;break;}
            if(!hit){states[begin+next[set]++%4]={key,true};++misses;}
        }
        if(comma)std::cout<<',';comma=true;
        std::cout<<"{\"raw_slots\":"<<raw_slots<<",\"raw_policy\":\""<<(raw_slots?"uniform":"selective")<<"\",\"state_slots\":"<<state_slots
                 <<",\"canonical_captures\":"<<misses<<",\"raw_misses\":"<<raw_misses<<'}';
    }
    std::cout<<"]}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
