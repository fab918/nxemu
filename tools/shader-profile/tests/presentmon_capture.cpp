// SPDX-License-Identifier: GPL-2.0-or-later
// External Windows measurement helper. Run elevated for ETW access. Redirect
// PresentMon stdout to a readable shared file, so readiness is checked live.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>
#include <stdexcept>

std::wstring Quote(const std::wstring& value) {
    std::wstring result=L"\"";size_t slashes{};
    for(wchar_t c:value) {
        if(c==L'\\'){++slashes;continue;}
        result.append(c==L'"'?slashes*2+1:slashes,L'\\');
        slashes=0;result+=c;
    }
    result.append(slashes*2,L'\\');return result+L'"';
}
int wmain(int argc,wchar_t** argv)try {
    if(argc!=5 && argc!=6)throw std::runtime_error("expected PresentMon executable, game PID, output directory, session name, optional duration");
    const std::filesystem::path directory=argv[3];
    const auto csv=directory/L"presentations.csv",errors=directory/L"presentmon-errors.log";
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    const auto open=[&](const std::filesystem::path& path) {
        return CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                           &security,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    };
    struct File {HANDLE value;~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
    File output{open(csv)},error{open(errors)};
    if(output.value==INVALID_HANDLE_VALUE || error.value==INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create shared capture files");
    File input{CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr)};
    if(input.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open capture input");
    std::wstring command=Quote(argv[1])+L" --process_id "+Quote(argv[2])+
        L" --session_name "+Quote(argv[4])+L" --output_stdout"
        L" --qpc_time --no_console_stats --no_track_input --terminate_on_proc_exit";
    if(argc==6) {
        const auto seconds=std::stoul(argv[5]);
        if(seconds==0 || seconds>60)throw std::runtime_error("invalid preflight duration");
        command+=L" --timed "+std::to_wstring(seconds)+L" --terminate_after_timed";
    }
    STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESTDHANDLES;
    start.hStdOutput=output.value;start.hStdError=error.value;start.hStdInput=input.value;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(argv[1],command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,
                       nullptr,directory.c_str(),&start,&process))
        throw std::runtime_error("Cannot launch PresentMon");
    struct Close {HANDLE value;~Close(){CloseHandle(value);}} thread{process.hThread},child{process.hProcess};
    {
        std::ofstream status(directory/L"presentmon-process.json");
        status<<"{\"pid\":"<<process.dwProcessId<<",\"stdout_shared\":true}";
    }
    WaitForSingleObject(process.hProcess,INFINITE);
    DWORD code{};GetExitCodeProcess(process.hProcess,&code);FlushFileBuffers(output.value);
    std::ofstream status(directory/L"presentmon-exit.json");status<<"{\"exit_code\":"<<code<<'}';
    return static_cast<int>(code);
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
