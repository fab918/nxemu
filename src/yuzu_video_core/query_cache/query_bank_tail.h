// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <utility>

namespace VideoCommon {
// An active query holds references to every bank in its span. Those banks
// cannot be recycled; new slots therefore stay in the tail or append a new bank.
class QueryBankTail {
public:
    void Begin(std::size_t bank) { last = bank; }
    bool Append(std::size_t bank) { return std::exchange(last, bank) != bank; }

private:
    std::size_t last{};
};
} // namespace VideoCommon
