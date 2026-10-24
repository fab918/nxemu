// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <unordered_map>
#include <utility>

namespace Vulkan
{
// Startup only. Retain the first value solely while all observed inputs agree.
// Conflicts invalidate the whole key, independent of record order. Disk records
// remain distinct; normal gameplay can compile the actual environment later.
template <class Key, class Inputs>
class StartupPipelineVariants
{
public:
    struct Candidate
    {
        Inputs inputs;
        bool ambiguous{};
    };
    template <class Equal>
    void Observe(const Key & key, Inputs inputs, Equal equal)
    {
        const auto it = entries.find(key);
        if (it == entries.end())
            entries.emplace(key, Candidate{std::move(inputs), false});
        else if (!it->second.ambiguous && !equal(it->second.inputs, inputs))
            it->second.ambiguous = true;
    }
    template <class F, class Rejected>
    size_t ConsumeSafe(F consume, Rejected reject)
    {
        size_t rejected{};
        for (auto & [key, candidate] : entries)
        {
            if (candidate.ambiguous)
            {
                ++rejected;
                reject(key);
            }
            else
                consume(key, std::move(candidate.inputs));
        }
        entries.clear();
        return rejected;
    }

    template <class F>
    size_t ConsumeSafe(F consume)
    {
        return ConsumeSafe(std::move(consume), [](const Key &) {});
    }

private:
    std::unordered_map<Key, Candidate> entries;
};
} // namespace Vulkan
