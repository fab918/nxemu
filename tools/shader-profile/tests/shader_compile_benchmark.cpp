// SPDX-License-Identifier: GPL-2.0-or-later
// CPU compiler only: hand-authored scheduling/NOP/BRA/EXIT words, no game shaders.
#include "yuzu_shader_recompiler/backend/spirv/emit_spirv.h"
#include "yuzu_shader_recompiler/frontend/maxwell/translate_program.h"
#include "yuzu_shader_recompiler/host_translate_info.h"
#include "video_settings.h"
#include "yuzu_common/logging/log.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
VideoSettings videoSettings{};
namespace Common::Log {
LogLevel g_classLevel[static_cast<uint8_t>(LogClass::Count)]{};
void FmtLogMessageImpl(LogClass, LogLevel, const char*, unsigned int, const char*, const char*, const fmt::format_args&) {}
}
void assert_fail_impl(){throw std::runtime_error("compiler assertion");}
[[noreturn]] void unreachable_impl(){throw std::runtime_error("compiler unreachable");}
struct Environment final : Shader::Environment {
    unsigned nops{},width{1};bool branch{};
    Environment(){stage=Shader::Stage::Compute;}
    void Start(u32 address){start_address=address;}
    u64 ReadInstruction(u32 address) override {
        address-=start_address;
        if(address%8 || address%32==0)throw std::runtime_error("Invalid instruction fetch");
        const auto index=(address/32)*3+(address%32)/8-1;
        const auto exit=nops+(branch?2:0);
        if(index>exit)throw std::runtime_error("Compiler walked past EXIT");
        if(branch && index==0)return 0xe24000000087000fULL; // BRA +8: skip one NOP.
        return index==exit ? 0xe30000000007000fULL : 0x50b0000000070f00ULL;
    }
    u32 ReadCbufValue(u32,u32) override {throw std::runtime_error("Unexpected cbuf query");}
    Shader::TextureType ReadTextureType(u32) override {throw std::runtime_error("Unexpected texture query");}
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {throw std::runtime_error("Unexpected image query");}
    bool IsTexturePixelFormatInteger(u32) override {throw std::runtime_error("Unexpected format query");}
    u32 ReadViewportTransformState() override {return 0;}
    u32 TextureBoundBuffer() const override {return 0;}
    u32 LocalMemorySize() const override {return 0;}
    u32 SharedMemorySize() const override {return 0;}
    std::array<u32,3> WorkgroupSize() const override {return {width,1,1};}
    bool HasHLEMacroState() const override {return false;}
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32,u32) override {return {};}
    void Dump(u64,u64) override {}
};
void Check(bool ok){if(!ok)throw std::runtime_error("Synthetic compiler output mismatch");}
double Median(std::vector<double> values){std::sort(values.begin(),values.end());return values[values.size()/2];}
int main(int argc,char** argv)try {
    Check(argc==2);const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    Shader::ObjectPool<Shader::IR::Inst> instructions;
    Shader::ObjectPool<Shader::IR::Block> blocks;
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow;
    Shader::HostTranslateInfo host{};Shader::Profile profile{};profile.supported_spirv=0x00010300;
    std::cout<<"{\"scope\":\"real_CFG_translate_optimize_SPIRV_CPU_only_no_driver\",\"samples\":[";
    bool comma{};
    for(unsigned nops:{0U,32U,1024U})for(unsigned width:{1U,64U})for(unsigned address:{0U,32U,65536U})for(bool branch:{false,true}) {
        Environment env;env.nops=nops;env.width=width;env.Start(address);env.branch=branch;
        std::vector<double> translation,emission,total;std::vector<u32> reference;
        for(unsigned trial=0;trial<61;++trial) {
            const auto start=std::chrono::steady_clock::now();
            {
                Shader::Maxwell::Flow::CFG cfg(env,flow,env.StartAddress());
                auto program=Shader::Maxwell::TranslateProgram(instructions,blocks,env,cfg,host);
                const auto translated=std::chrono::steady_clock::now();
                const auto code=Shader::Backend::SPIRV::EmitSPIRV(profile,program);
                const auto done=std::chrono::steady_clock::now();
                Check(code.size()>5 && code[0]==0x07230203);
                bool local_size{};
                for(size_t i=5;i<code.size();) {
                    const auto words=code[i]>>16,opcode=code[i]&65535;
                    Check(words && words<=code.size()-i);
                    if(opcode==16 && words==6 && code[i+2]==17) {
                        Check(code[i+3]==width && code[i+4]==1 && code[i+5]==1);local_size=true;
                    }
                    i+=words;
                }
                Check(local_size);
                if(trial) {
                    Check(code==reference);
                    translation.push_back(std::chrono::duration<double,std::micro>(translated-start).count());
                    emission.push_back(std::chrono::duration<double,std::micro>(done-translated).count());
                    total.push_back(std::chrono::duration<double,std::micro>(done-start).count());
                } else reference=code;
            }
            instructions.ReleaseContents();blocks.ReleaseContents();flow.ReleaseContents();
        }
        const auto output=root/("nop-"+std::to_string(nops)+"-width-"+std::to_string(width)+"-offset-"+std::to_string(address)+(branch?"-bra":"")+".spv");
        std::ofstream file(output,std::ios::binary);file.write(reinterpret_cast<const char*>(reference.data()),reference.size()*4);Check(bool(file));
        if(comma)std::cout<<',';comma=true;
        std::cout<<"{\"nop_instructions\":"<<nops<<",\"relative_branch\":"<<(branch?"true":"false")<<",\"start_address\":"<<address<<",\"workgroup_width\":"<<width<<",\"successful_compilations\":61,\"spirv_words\":"<<reference.size()
            <<",\"translation_median_us\":"<<Median(translation)<<",\"emission_median_us\":"<<Median(emission)<<",\"total_median_us\":"<<Median(total)<<'}';
    }
    std::cout<<"]}\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
