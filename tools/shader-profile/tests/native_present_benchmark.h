// SPDX-License-Identifier: GPL-2.0-or-later
// Windows-only instrumentation for an isolated benchmark build. The source
// renderer does not include this header. No disk I/O or allocation on a frame.
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace NXShaderBenchmark {
inline constexpr uint64_t Magic=0x4e58505245533031ULL;
inline constexpr uint32_t Capacity=262144;
struct Header {
    uint64_t magic{},frequency{};
    uint32_t capacity{};
    std::atomic<uint32_t> count{};
    uint32_t overflow{},reserved{};
};
struct Sample { uint64_t qpc{};uint32_t flags{},reserved{}; };
static_assert(sizeof(Header)==32 && sizeof(Sample)==16);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
class Capture {
public:
    static std::unique_ptr<Capture> CreateIfRequested() {
        const char* directory=std::getenv("NXEMU_NATIVE_PRESENT_DIR");
        return directory && *directory?std::make_unique<Capture>(std::filesystem::u8path(directory)):nullptr;
    }
    explicit Capture(const std::filesystem::path& directory) {
        static std::atomic<uint32_t> serial{};
        const auto instance=++serial;
        LARGE_INTEGER created;QueryPerformanceCounter(&created);
        const auto tag=std::to_string(GetCurrentProcessId())+"-"+std::to_string(created.QuadPart);
        file=directory/("native-present-"+tag+".csv");
        const std::string name="Local\\NXShaderPresent-"+tag;
        const std::wstring wide(name.begin(),name.end());
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,
            sizeof(Header)+Capacity*sizeof(Sample),wide.c_str());
        if(!mapping)throw std::runtime_error("Cannot create presentation benchmark mapping");
        header=static_cast<Header*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,0));
        if(!header){CloseHandle(mapping);throw std::runtime_error("Cannot map presentation benchmark");}
        new(header) Header{};
        LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
        header->magic=Magic;header->frequency=frequency.QuadPart;header->capacity=Capacity;
        samples=reinterpret_cast<Sample*>(header+1);
        std::memset(samples,0,Capacity*sizeof(Sample)); // Touch pages before any measured frame.
        std::ofstream ready(directory/"native-present-latest.json");
        ready<<"{\"mapping\":\"Local\\\\NXShaderPresent-"<<tag
             <<"\",\"frequency\":"<<frequency.QuadPart<<",\"capacity\":"<<Capacity
             <<",\"instance\":"<<instance<<",\"scope\":\"vkQueuePresentKHR return; not scanout\"}";
    }
    // Display's existing present_mutex serializes UI and game presents.
    void Record(bool fresh,bool has_game) noexcept {
        const auto index=header->count.load(std::memory_order_relaxed);
        if(index>=Capacity){header->overflow=1;return;}
        LARGE_INTEGER now;QueryPerformanceCounter(&now);
        new(samples+index) Sample{static_cast<uint64_t>(now.QuadPart),uint32_t(fresh)|(uint32_t(has_game)<<1),0};
        header->count.store(index+1,std::memory_order_release);
    }
    ~Capture() {
        try {
            const auto count=header->count.load(std::memory_order_acquire);
            std::ofstream out(file);out<<"qpc,fresh_game,has_game\n";
            for(uint32_t i=0;i<count;++i)
                out<<samples[i].qpc<<','<<(samples[i].flags&1)<<','<<((samples[i].flags>>1)&1)<<'\n';
            out.flush();
            std::ofstream status(file.string()+".status.json");
            status<<"{\"samples\":"<<count<<",\"overflow\":"<<header->overflow
                  <<",\"saved\":"<<(out?"true":"false")<<'}';
        }catch(...){}
        UnmapViewOfFile(header);CloseHandle(mapping);
    }
private:
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    std::filesystem::path file;
    HANDLE mapping{};
    Header* header{};
    Sample* samples{};
};
} // namespace NXShaderBenchmark
#endif
