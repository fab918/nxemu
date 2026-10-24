// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic publisher used to verify recovery when destructors are bypassed.
#include "native_present_benchmark.h"
#include <iostream>
int main(int argc,char** argv)try {
    if(argc!=3)throw std::runtime_error("expected directory and normal/abrupt mode");
    const std::filesystem::path path=argv[1];
    NXShaderBenchmark::Capture capture(path);
    for(unsigned wait=0;!std::filesystem::exists(path/"native-mappings-held.json");++wait){
        if(wait==200)throw std::runtime_error("observer did not retain mapping");
        Sleep(10);
    }
    for(unsigned i=0;i<7;++i)capture.Record(false,false);
    for(unsigned i=0;i<120;++i){capture.Record(true,true);Sleep(2);}
    if(std::string_view(argv[2])=="abrupt")ExitProcess(0);
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
