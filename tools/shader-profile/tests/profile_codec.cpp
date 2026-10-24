// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/shader_profile_codec.h"
#include "common/json.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using VideoCommon::ShaderProfile::Pack;
using VideoCommon::ShaderProfile::Unpack;
std::string read(const char* path) {
    if(std::filesystem::file_size(path)>33*1024*1024)throw std::runtime_error("input limit");
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("cannot read input");
    return std::string((std::istreambuf_iterator<char>(input)),{});
}
void write(const char* path,const std::string& data) {
    if(std::filesystem::exists(path))throw std::runtime_error("output already exists");
    std::ofstream out(path,std::ios::binary);out.write(data.data(),static_cast<std::streamsize>(data.size()));
    if(!out)throw std::runtime_error("cannot write output");
}
template<class F> double median(F fn) {
    std::vector<double> times;
    for(unsigned i=0;i<11;++i) {
        const auto begin=std::chrono::steady_clock::now();fn();
        const auto duration=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        if(i)times.push_back(duration);
    }
    std::sort(times.begin(),times.end());return (times[4]+times[5])/2;
}
int main(int argc,char** argv) try {
    if(argc<3)throw std::runtime_error("usage: nxshader-codec pack|unpack|bench input [output] [level]");
    const std::string command=argv[1];const auto input=read(argv[2]);
    if(command=="pack" && (argc==4 || argc==5))write(argv[3],Pack(input,argc==5?std::stoi(argv[4]):3));
    else if(command=="unpack" && argc==4)write(argv[3],Unpack(input));
    else if((command=="bench" || command=="bench-payload") && argc==3) {
        const bool binary=command=="bench-payload";
        const auto parse=[](const std::string& text){JsonValue value;if(!JsonReader().Parse(text.data(),text.data()+text.size(),value))throw std::runtime_error("JSON parse failed");};
        const auto plain_parse=binary ? 0.0 : median([&]{parse(input);});
        std::cout<<"{\"raw_bytes\":"<<input.size()<<",\"plain_parse_ms\":"<<plain_parse<<",\"levels\":[";
        bool first=true;
        for(const int level:{1,3,6}) {
            const auto compressed=Pack(input,level);
            const auto pack_ms=median([&]{auto output=Pack(input,level);if(output!=compressed)throw std::runtime_error("nondeterministic compression");});
            const auto unpack_ms=median([&]{if(Unpack(compressed)!=input)throw std::runtime_error("roundtrip mismatch");});
            const auto read_parse_ms=binary ? unpack_ms : median([&]{parse(Unpack(compressed));});
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"level\":"<<level<<",\"bytes\":"<<compressed.size()<<",\"pack_ms\":"<<pack_ms
                     <<",\"unpack_ms\":"<<unpack_ms<<",\"decode_and_parse_ms\":"<<read_parse_ms<<'}';
        }
        std::cout<<"]}\n";
    } else throw std::runtime_error("invalid arguments");
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
