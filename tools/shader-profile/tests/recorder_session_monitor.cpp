// SPDX-License-Identifier: GPL-2.0-or-later
// Windows-only benchmark instrumentation. No emulation or per-draw hooks.
#include <windows.h>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include "common/json.h"
#include "native_present_benchmark.h"

// Keep the mapping alive outside the emulator: ExitProcess/DLL unloading may
// bypass the display destructor. Also save immediately at the stop marker.
class NativeCollector {
    struct View { std::string tag;HANDLE handle;const NXShaderBenchmark::Header* data; };
public:
    NativeCollector(std::filesystem::path directory,DWORD pid_) : path(std::move(directory)),pid(pid_) {}
    void Probe() noexcept { try {
        const auto metadata=path/"native-present-latest.json";
        if(!std::filesystem::exists(metadata) || std::filesystem::file_size(metadata)>4096)return;
        std::ifstream in(metadata);const std::string text((std::istreambuf_iterator<char>(in)),{});
        JsonValue root;JsonReader reader;if(!reader.Parse(text.data(),text.data()+text.size(),root))return;
        const auto name=root["mapping"].asString();
        const auto prefix="Local\\NXShaderPresent-"+std::to_string(pid)+"-";
        if(!name.starts_with(prefix) || name.find_first_not_of("0123456789",prefix.size())!=std::string::npos)return;
        const auto stem=std::string("Local\\NXShaderPresent-");
        for(const auto& view:views)if(view.tag==name.substr(stem.size()))return;
        if(views.size()>=64)return;
        const std::wstring wide(name.begin(),name.end());
        const HANDLE handle=OpenFileMappingW(FILE_MAP_READ,FALSE,wide.c_str());if(!handle)return;
        const auto data=static_cast<const NXShaderBenchmark::Header*>(MapViewOfFile(handle,FILE_MAP_READ,0,0,0));
        if(!data){CloseHandle(handle);return;}
        if(data->magic!=NXShaderBenchmark::Magic || data->capacity!=NXShaderBenchmark::Capacity){UnmapViewOfFile(data);CloseHandle(handle);return;}
        const auto tag=name.substr(stem.size());
        views.push_back({tag,handle,data});
        std::ofstream ready(path/"native-mappings-held.json");
        ready<<"{\"mappings\":"<<views.size()<<",\"latest\":\""<<tag<<"\"}";
    }catch(...){} }
    void Save() noexcept { try {
        for(const auto& view:views){
            const auto count=view.data->count.load(std::memory_order_acquire);
            if(count>NXShaderBenchmark::Capacity)continue;
            const auto samples=reinterpret_cast<const NXShaderBenchmark::Sample*>(view.data+1);
            const auto file=path/("native-held-"+view.tag+".csv");
            std::ofstream out(file);out<<"qpc,fresh_game,has_game\n";
            for(uint32_t i=0;i<count;++i)out<<samples[i].qpc<<','<<(samples[i].flags&1)<<','<<((samples[i].flags>>1)&1)<<'\n';
            out.flush();std::ofstream status(file.string()+".status.json");
            status<<"{\"samples\":"<<count<<",\"overflow\":"<<view.data->overflow<<",\"saved\":"<<(out?"true":"false")<<'}';
        }
    }catch(...){} }
    ~NativeCollector(){Save();for(const auto& view:views){UnmapViewOfFile(view.data);CloseHandle(view.handle);}}
private:
    std::filesystem::path path;DWORD pid;std::vector<View> views;
};

uint64_t Number(FILETIME value){return uint64_t(value.dwLowDateTime)|(uint64_t(value.dwHighDateTime)<<32);}
int main(int argc,char** argv)try {
    if(argc!=3)throw std::runtime_error("usage: recorder-session-monitor pid output-directory");
    const DWORD pid=std::stoul(argv[1]);const std::filesystem::path path=argv[2];
    const HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid);
    if(!process)throw std::runtime_error("Cannot read benchmark process");
    struct Close {HANDLE h;~Close(){CloseHandle(h);}} close{process};
    NativeCollector native(path,pid);
    if(!RegisterHotKey(nullptr,1,MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_F10))
        throw std::runtime_error("ALT+SHIFT+F10 unavailable; no measurement started");
    struct Unregister {~Unregister(){UnregisterHotKey(nullptr,1);}} unregister;
    if(!RegisterHotKey(nullptr,2,MOD_NOREPEAT,VK_F8))
        throw std::runtime_error("F8 unavailable; no measurement started");
    struct UnregisterF8 {~UnregisterF8(){UnregisterHotKey(nullptr,2);}} unregister_f8;
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    std::ofstream csv(path/"session.csv");
    if(!csv)throw std::runtime_error("Cannot write benchmark samples");
    csv<<"event,qpc,kernel_100ns,user_100ns,private_bytes,working_set_bytes,memory_available\n";
    const auto sample=[&](const char* event){
        LARGE_INTEGER now;QueryPerformanceCounter(&now);
        FILETIME created{},exited{},kernel{},user{};
        if(!GetProcessTimes(process,&created,&exited,&kernel,&user))throw std::runtime_error("Cannot sample process CPU time");
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
        const bool available=GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
        csv<<event<<','<<now.QuadPart<<','<<Number(kernel)<<','<<Number(user)<<','<<memory.PrivateUsage<<','<<memory.WorkingSetSize<<','<<available<<'\n';
        csv.flush();if(!csv)throw std::runtime_error("Benchmark sample write failed");
    };
    sample("ready");
    {
        std::ofstream ready(path/"monitor-ready.json");
        ready<<"{\"ready\":true,\"pid\":"<<pid<<",\"qpc_frequency\":"<<frequency.QuadPart
             <<",\"hotkey\":\"F8 (or ALT+SHIFT+F10)\",\"sample_period_ms\":500}";
        ready.flush();if(!ready)throw std::runtime_error("Cannot publish benchmark readiness");
    }
    bool recording{};
    for(;;){
        native.Probe();
        const DWORD result=MsgWaitForMultipleObjects(1,&process,FALSE,500,QS_ALLINPUT);
        if(result==WAIT_OBJECT_0){sample(recording?"exit_unclosed_segment":"exit");break;}
        if(result==WAIT_FAILED)throw std::runtime_error("Benchmark wait failed");
        MSG message;
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_HOTKEY && (message.wParam==1 || message.wParam==2)){
                recording=!recording;sample(recording?"start":"stop");
                Beep(recording?1000:600,60);
                if(!recording){native.Probe();native.Save();}
            }
        }
        if(result==WAIT_TIMEOUT)sample("sample");
    }
    std::cout<<"Benchmark process exited; samples saved\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
