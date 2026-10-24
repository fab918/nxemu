// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yuzu_video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "yuzu_video_core/shader_profile_journal.h"
#include "yuzu_common/cityhash.h"
#include <memory>
#include <bit>
#include <cstring>

namespace Vulkan {
struct GuestInputHash {
    u64 operator()(const void* data, size_t size) const noexcept {
        return Common::CityHash64(static_cast<const char*>(data), size);
    }
};

// Ephemeral, bounded memoization only. Hash matches always compare complete raw
// input words/keys; eviction merely causes another canonical capture. No shader
// instructions, guest addresses, persistent identities or renderer keys change.
template <class Hash = GuestInputHash>
class GuestRecipeInputCache {
    static constexpr size_t Groups=7, Sets=64, Ways=4, Slots=Sets*Ways;
    using Key=std::array<u64,Groups+3>;
    struct RawSlot { u64 hash{}, id{}; };
    struct Group {
        std::vector<u16> methods;
        std::vector<u32> current, values;
        std::array<RawSlot,Slots> slots{};
        std::array<u8,Sets> next{};
    };
public:
    void EnableDiagnosticKeys() noexcept { diagnostic_keys = true; }
    const Key& DiagnosticKey() const noexcept { return diagnostic_key; }
    struct State {
        Key key{};
        VideoCommon::ShaderProfile::Journal::Entry entry{};
        FixedPipelineState projected{};
        bool valid{}, prefix_known{};
    };
    struct Result { State* state; bool hit; };
private:
    struct Storage {
        std::array<Group,Groups+1> groups;
        std::array<State,Slots> states;
        std::array<u8,Sets> next{};
    };
public:
    GuestRecipeInputCache() : storage(std::make_unique<Storage>()) {
        auto dirty=std::make_unique<Tegra::Engines::Maxwell3D::DirtyState>();
        GuestPipelineState::Track(*dirty);
        static_assert(Maxwell::NUM_REGS <= UINT16_MAX);
        for(size_t method=0;method<Maxwell::NUM_REGS;++method)
            for(size_t group=0;group<Groups;++group)
                if(dirty->shader_profile_registers[method] & (1U<<group))
                    storage->groups[group].methods.push_back(static_cast<u16>(method));
        for(size_t i=0;i<=Groups;++i) {
            auto& group=storage->groups[i];
            group.current.resize(i==Groups?104:group.methods.size());
            group.values.resize(group.current.size()*Slots);
        }
    }
    Result Find(const Maxwell& regs, u64 changes, bool full,
                Maxwell::PrimitiveTopology topology,
                Tegra::Engines::Maxwell3D::EngineHint hint, u32 features,
                const void* prefix, bool prefix_changed) {
        u64 remaining=(full?GuestPipelineState::All:changes&GuestPipelineState::All)|
                      (full || prefix_changed?1U<<Groups:0U);
        while(remaining) {
            const size_t i=std::countr_zero(remaining);
            remaining&=remaining-1;
            auto& group=storage->groups[i];
            if(group.current.empty())continue;
            if(i==Groups)std::memcpy(group.current.data(),prefix,104*sizeof(u32));
            else for(size_t word=0;word<group.methods.size();++word)
                group.current[word]=regs.reg_array[group.methods[word]];
            const auto hash=Hash{}(group.current.data(),group.current.size()*sizeof(u32));
            if (diagnostic_keys) diagnostic_key[i==Groups?Groups+2:i]=hash;
            const size_t set=hash%Sets, begin=set*Ways;
            size_t selected=Slots;
            for(size_t way=0;way<Ways;++way) {
                const size_t index=begin+way;
                const auto& slot=group.slots[index];
                if(slot.id && slot.hash==hash && std::equal(group.current.begin(),group.current.end(),
                    group.values.begin()+index*group.current.size())) {
                    selected=index;break;
                }
            }
            if(selected==Slots) {
                selected=begin+group.next[set]++%Ways;
                if(++generation==0)throw std::runtime_error("Guest input cache generation exhausted");
                group.slots[selected]={hash,generation};
                std::copy(group.current.begin(),group.current.end(),group.values.begin()+selected*group.current.size());
            }
            key[i==Groups?Groups+2:i]=group.slots[selected].id;
        }
        key[Groups]=static_cast<u32>(topology)|(u64{static_cast<u32>(hint)}<<32);
        key[Groups+1]=features;
        if (diagnostic_keys) { diagnostic_key[Groups]=key[Groups]; diagnostic_key[Groups+1]=features; }
        const size_t set=Hash{}(key.data(),sizeof(key))%Sets, begin=set*Ways;
        for(size_t way=0;way<Ways;++way) {
            auto& state=storage->states[begin+way];
            if(state.valid && state.key==key)return {&state,true};
        }
        auto& state=storage->states[begin+storage->next[set]++%Ways];
        state.valid=true;state.key=key;state.prefix_known=false;
        return {&state,false};
    }
    size_t StorageBytes() const noexcept {
        size_t bytes=sizeof(Storage);
        for(const auto& group:storage->groups)
            bytes+=group.methods.capacity()*sizeof(u16)+(group.current.capacity()+group.values.capacity())*sizeof(u32);
        return bytes;
    }
private:
    std::unique_ptr<Storage> storage;
    Key key{};
    Key diagnostic_key{};
    bool diagnostic_keys{};
    u64 generation{};
};
} // namespace Vulkan
