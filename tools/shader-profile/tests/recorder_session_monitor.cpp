// SPDX-License-Identifier: GPL-2.0-or-later
// Windows-only benchmark instrumentation. No emulation or per-draw hooks.
#include <windows.h>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <stdexcept>

uint64_t Number(FILETIME value){return uint64_t(value.dwLowDateTime)|(uint64_t(value.dwHighDateTime)<<32);}
int main(int argc,char** argv)try {
    if(argc!=3)throw std::runtime_error("usage: recorder-session-monitor pid output-directory");
    const DWORD pid=std::stoul(argv[1]);const std::filesystem::path path=argv[2];
    const HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid);
    if(!process)throw std::runtime_error("Cannot read benchmark process");
    struct Close {HANDLE h;~Close(){CloseHandle(h);}} close{process};
    if(!RegisterHotKey(nullptr,1,MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_F10))
        throw std::runtime_error("ALT+SHIFT+F10 unavailable; no measurement started");
    struct Unregister {~Unregister(){UnregisterHotKey(nullptr,1);}} unregister;
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
             <<",\"hotkey\":\"ALT+SHIFT+F10\",\"sample_period_ms\":500}";
        ready.flush();if(!ready)throw std::runtime_error("Cannot publish benchmark readiness");
    }
    bool recording{};
    for(;;){
        const DWORD result=MsgWaitForMultipleObjects(1,&process,FALSE,500,QS_ALLINPUT);
        if(result==WAIT_OBJECT_0){sample(recording?"exit_unclosed_segment":"exit");break;}
        if(result==WAIT_FAILED)throw std::runtime_error("Benchmark wait failed");
        MSG message;
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_HOTKEY && message.wParam==1){
                recording=!recording;sample(recording?"start":"stop");
            }
        }
        if(result==WAIT_TIMEOUT)sample("sample");
    }
    std::cout<<"Benchmark process exited; samples saved\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
