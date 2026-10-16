// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "yuzu_common/yuzu_assert.h"
#include "yuzu_video_core/query_cache/bank_base.h"
#include "query_bank_tail.h"

int main() {
    try {
        auto check = [](bool condition, const char* message) {
            if (!condition) { throw std::runtime_error(message); }
        };
        VideoCommon::QueryBankTail tail;
        tail.Begin(0);
        // Compare the constant-time tail with the old full-span membership
        // result across a long query with repeated slots in every bank.
        std::vector<size_t> span{0};
        for (size_t bank = 0; bank < 4096; ++bank) {
            for (size_t slot = 0; slot < 16; ++slot) {
                const bool reference = std::find(span.begin(), span.end(), bank) == span.end();
                check(tail.Append(bank) == reference, "Bank span count differs from full traversal");
                if (reference) { span.push_back(bank); }
            }
        }
        check(span.size() == 4096, "Long query lost banks");
        // A copied report must not be affected when the active span extends.
        auto snapshot = tail;
        check(tail.Append(4096), "New bank was missed");
        check(!snapshot.Append(4095), "Snapshot tail was mutated");
        tail.Begin(0);
        check(!tail.Append(0), "Reused bank was counted twice in a new query");
        check(tail.Append(7), "Bank identifiers need not be consecutive");

        // Verify the lifetime invariant that makes tail comparison valid using
        // the real bank pool: even a closed bank cannot be reused while any
        // active query or copied report still references it.
        VideoCommon::BankPool<VideoCommon::BankBase> pool;
        auto reserve = [&] {
            return pool.ReserveBank([](auto& banks, size_t) { banks.emplace_back(1); });
        };
        const auto first = reserve();
        auto& bank = pool.GetBank(first);
        bank.Reserve();
        bank.AddReference(2);
        check(reserve() != first, "Live bank was recycled");
        bank.CloseReference();
        check(reserve() != first, "Bank still held by a copied report was recycled");
        bank.CloseReference();
        check(reserve() == first, "Released bank could not be recycled");
        tail.Begin(first);
        check(!tail.Append(first), "Tail survived query release and bank reuse");
        std::cout << "Query bank spans, snapshots and recycling tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
