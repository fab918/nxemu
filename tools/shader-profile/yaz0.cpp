// SPDX-License-Identifier: GPL-2.0-or-later
// Benchmark bridge to the same decoder used by the native importer.
#include "yuzu_video_core/shader_profile_resources.h"
#include <cstdint>
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif
static int Decode(const uint8_t* src, size_t size, uint8_t* dst, size_t capacity, size_t* consumed) {
    if(!src || (!dst && capacity) || size<16) return -1;
    const size_t expected=(size_t(src[4])<<24)|(size_t(src[5])<<16)|(size_t(src[6])<<8)|src[7];
    if(expected!=capacity) return -2;
    try {
        VideoCommon::ShaderProfile::DecodeYaz0Into(
            {reinterpret_cast<const char*>(src),size},
            {reinterpret_cast<char*>(dst),capacity},consumed);
        return 0;
    } catch(...) { return -3; }
}
EXPORT int nxshader_yaz0(const uint8_t* src,size_t size,uint8_t* dst,size_t capacity) {
    return Decode(src,size,dst,capacity,nullptr);
}
EXPORT int nxshader_yaz0_extent(const uint8_t* src,size_t size,uint8_t* dst,size_t capacity,size_t* consumed) {
    return Decode(src,size,dst,capacity,consumed);
}
