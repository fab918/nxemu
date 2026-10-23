// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/shader_profile_hash.h"
#include <random>
#include <stdexcept>
int main() {
    using VideoCommon::ShaderProfile::ResourceSha256;
    if(ResourceSha256("abc")!="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        throw std::runtime_error("SHA-256 standard vector mismatch");
    std::mt19937 random(123);
    for(size_t size:{0U,1U,7U,55U,56U,63U,64U,65U,127U,128U,129U,4096U,1048576U}) {
        std::string data(size,'\0');
        for(auto& value:data) value=static_cast<char>(random());
        if(ResourceSha256(data)!=sha256(data)) throw std::runtime_error("platform SHA-256 mismatch");
    }
}
