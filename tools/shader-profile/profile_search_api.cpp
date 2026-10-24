// SPDX-License-Identifier: GPL-2.0-or-later
// Test/benchmark bridge; the search implementation is ordinary portable C++.
#include "yuzu_video_core/shader_profile_search.h"
#include <cstdint>
#include "yuzu_common/cityhash.h"
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif
using VideoCommon::ShaderProfile::PrefixSearch;
EXPORT void* nxshader_search_create(const unsigned char* prefixes, size_t count) {
    if (!prefixes || !count || count > 65536) return nullptr;
    try {
        std::vector<PrefixSearch::Prefix> values(count);
        std::memcpy(values.data(), prefixes, count * PrefixSearch::Width);
        return new PrefixSearch(values);
    } catch (...) { return nullptr; }
}
EXPORT void nxshader_search_destroy(void* search) { delete static_cast<PrefixSearch*>(search); }
EXPORT int nxshader_search_scan(const void* search, const unsigned char* input, size_t size,
                               size_t* resume, size_t* positions, size_t capacity, size_t* count) {
    if (!search || (!input && size) || !resume || !positions || !capacity || !count) return -1;
    try {
        *count = static_cast<const PrefixSearch*>(search)->Scan({input, size}, *resume,
                                                               {positions, capacity});
        return 0;
    } catch (...) { return -2; }
}

EXPORT uint64_t nxshader_hash64(const char* code, size_t size) {
    return Common::CityHash64(code, size);
}
