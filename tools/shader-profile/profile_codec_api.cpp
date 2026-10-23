// SPDX-License-Identifier: GPL-2.0-or-later
// Offline encoder bridge for Python. The importing emulator does not load this library.
#include "yuzu_video_core/shader_profile_codec.h"
#include <cstring>
#include <zstd.h>
#ifdef _WIN32
#define NXSHADER_EXPORT extern "C" __declspec(dllexport)
#else
#define NXSHADER_EXPORT extern "C"
#endif
NXSHADER_EXPORT size_t nxshader_pack_bound(size_t size) {
    return size && size<=32*1024*1024 ? 32+ZSTD_compressBound(size) : 0;
}
NXSHADER_EXPORT int nxshader_pack(const char* input,size_t size,int level,char* output,
                                 size_t capacity,size_t* written) {
    if(!input || !output || !written)return -1;
    *written=0;
    try {
        const auto encoded=VideoCommon::ShaderProfile::Pack(std::string_view(input,size),level);
        if(encoded.size()>capacity)return -2;
        std::memcpy(output,encoded.data(),encoded.size());*written=encoded.size();return 0;
    } catch(...) {return -3;}
}
