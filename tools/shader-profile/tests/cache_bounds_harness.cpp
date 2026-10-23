// Source-derived regression harness: production method bodies, fake GPU memory.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using GPUVAddr = u64;
constexpr size_t INST_SIZE = sizeof(u64);
#define ASSERT(value) do { if (!(value)) throw std::runtime_error("precondition"); } while (false)
namespace Shader {
struct LogicError : std::runtime_error {
    LogicError(const char* text, u32) : std::runtime_error(text) {}
};
}
namespace Common {
size_t hash_size{};
// Only record the unchanged input range; the hash algorithm is outside this test.
u64 CityHash64(const char*, size_t size) { hash_size = size; return 123; }
}
struct Memory {
    std::array<u64, 2048> words{};
    size_t last_block_size{};
    size_t instruction_reads{};
    void ReadBlock(u64 address, void* target, size_t size) {
        if (address + size > sizeof(words)) throw std::runtime_error("memory bounds");
        last_block_size = size;
        std::memcpy(target, reinterpret_cast<const char*>(words.data()) + address, size);
    }
    template<class T> T Read(u64 address) {
        ++instruction_reads;
        T value{};
        ReadBlock(address, &value, sizeof(value));
        return value;
    }
};
class GenericEnvironment {
public:
    Memory* gpu_memory{};
    u64 program_base{};
    u32 start_address{};
    u32 read_lowest = std::numeric_limits<u32>::max(), read_highest{};
    u32 cached_lowest = std::numeric_limits<u32>::max(), cached_highest{};
    bool frozen{}, has_unbound_instructions{};
    std::vector<u64> code;
    std::shared_ptr<const std::vector<u64>> shared_code;
    std::span<const u64> Code() const { return shared_code ? std::span(*shared_code) : std::span(code); }
    u64 ReadInstruction(u32);
    std::optional<u64> Analyze();
    void SetCachedSize(size_t);
    void SetCachedCode(std::shared_ptr<const std::vector<u64>>, size_t);
    std::shared_ptr<const std::vector<u64>> ShareCachedCode(size_t);
    size_t CachedSizeWords() const noexcept;
    size_t CachedSizeBytes() const noexcept;
    size_t ReadSizeBytes() const noexcept;
    std::optional<u64> TryFindSize();
};
// METHOD_BODIES

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() try {
    for (const u32 start : {0U, 128U}) {
        for (const size_t length : {size_t{8}, size_t{104}, size_t{4096}, size_t{4104}}) {
            Memory memory;
            const u64 terminal = 0xE2400FFFFF87000FULL;
            memory.words[(start + length - 8)/8] = terminal;
            memory.words[(start + length)/8] = 0xDEADBEEF;
            GenericEnvironment env;
            env.gpu_memory = &memory;
            env.start_address = start;
            require(env.Analyze() == 123, "Analyze failed");
            require(Common::hash_size == length - 8, "historical hash range changed");
            const auto size = env.CachedSizeBytes();
            require(size == length, "Analyze range must include terminal exactly once");
            const auto shared = env.ShareCachedCode(size);
            require(env.CachedSizeBytes() == size, "sharing adds an extra word");
            require(shared->size()*8 == size, "snapshot size differs from ShaderInfo size");
            require(memory.last_block_size == size, "extra memory read after program end");
            require(shared->back() == terminal, "terminal missing from shared snapshot");
            require(env.ReadInstruction(start + static_cast<u32>(size) - 8) == terminal,
                    "terminal read failed");
            require(memory.instruction_reads == 0 && !env.has_unbound_instructions,
                    "inclusive terminal treated as uncached");
            GenericEnvironment variant;
            variant.gpu_memory = &memory;
            variant.start_address = start;
            variant.SetCachedCode(shared, size);
            require(variant.CachedSizeBytes() == size, "variant adds an extra word");
            variant.frozen = true;
            require(variant.ReadInstruction(start + static_cast<u32>(size) - 8) == terminal,
                    "frozen terminal read failed");
            bool rejected = false;
            try { variant.ReadInstruction(start + static_cast<u32>(size)); }
            catch (const Shader::LogicError&) { rejected = true; }
            require(rejected, "frozen snapshot accepted instruction after program");
            GenericEnvironment compute;
            compute.gpu_memory = &memory;
            compute.start_address = start;
            compute.SetCachedSize(size);
            require(compute.CachedSizeBytes() == size && compute.code.size()*8 == size,
                    "compute cache size grew");
            compute.ReadInstruction(start + static_cast<u32>(size));
            require(compute.has_unbound_instructions, "live out-of-range read not recorded");
        }
    }
    std::cout << "8 range cases passed: analyze, shared/compute snapshots, inclusive terminal, frozen/live bounds\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
