// SPDX-License-Identifier: GPL-2.0-or-later
#include "yuzu_video_core/shader_profile_codec.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <zstd.h>

namespace VideoCommon::ShaderProfile {
namespace {
constexpr std::string_view Magic="NXSHADR2";
constexpr size_t Limit=32*1024*1024;
void Check(bool ok) { if(!ok) throw std::runtime_error("Invalid compressed nxshader envelope"); }
void Zcheck(size_t result) { Check(!ZSTD_isError(result)); }
void Put(std::string& out,uint64_t n,size_t size) {
    for(size_t i=0;i<size;++i) out.push_back(static_cast<char>(n>>(8*i)));
}
uint64_t Get(std::string_view bytes,size_t offset,size_t size) {
    Check(offset<=bytes.size() && size<=bytes.size()-offset);uint64_t n{};
    for(size_t i=0;i<size;++i) n|=uint64_t{static_cast<unsigned char>(bytes[offset+i])}<<(8*i);
    return n;
}
} // namespace
std::string Pack(std::string_view json,int level) {
    Check(!json.empty() && json.size()<=Limit && level>=1 && level<=19);
    std::unique_ptr<ZSTD_CCtx,decltype(&ZSTD_freeCCtx)> context(ZSTD_createCCtx(),ZSTD_freeCCtx);
    Check(context!=nullptr);
    Zcheck(ZSTD_CCtx_setParameter(context.get(),ZSTD_c_compressionLevel,level));
    Zcheck(ZSTD_CCtx_setParameter(context.get(),ZSTD_c_checksumFlag,1));
    std::string compressed(ZSTD_compressBound(json.size()),'\0');
    const auto size=ZSTD_compress2(context.get(),compressed.data(),compressed.size(),json.data(),json.size());
    Zcheck(size);compressed.resize(size);
    std::string out(Magic);Put(out,1,4);Put(out,1,4);Put(out,json.size(),8);Put(out,size,8);
    out+=compressed;return out;
}
std::string Unpack(std::string_view bytes) {
    if(bytes.substr(0,8)!=Magic) {Check(bytes.size()<=Limit);return std::string(bytes);}
    Check(bytes.size()>=32 && Get(bytes,8,4)==1 && Get(bytes,12,4)==1);
    const auto raw_size=Get(bytes,16,8),stored=Get(bytes,24,8);
    Check(raw_size && raw_size<=Limit && stored==bytes.size()-32 && stored>=9);
    const auto frame=bytes.substr(32);
    // A single standard Zstd frame, known bounded output size, checksum enabled, no external dictionary.
    Check(Get(frame,0,4)==0xfd2fb528U && (static_cast<unsigned char>(frame[4])&4U)!=0);
    const auto frame_size=ZSTD_findFrameCompressedSize(frame.data(),frame.size());Zcheck(frame_size);
    Check(frame_size==frame.size() && ZSTD_getFrameContentSize(frame.data(),frame.size())==raw_size &&
          ZSTD_getDictID_fromFrame(frame.data(),frame.size())==0);
    std::string out(static_cast<size_t>(raw_size),'\0');
    const auto decoded=ZSTD_decompress(out.data(),out.size(),frame.data(),frame.size());Zcheck(decoded);
    Check(decoded==raw_size);return out;
}
} // namespace VideoCommon::ShaderProfile
