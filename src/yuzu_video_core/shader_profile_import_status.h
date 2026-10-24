// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/json.h"
#include <string_view>
namespace VideoCommon::ShaderProfile
{
inline bool CanPublishImport(bool ready, std::string_view stamp, bool cancelled,
                             size_t built, size_t queued, size_t failures) noexcept
{
    return ready && !stamp.empty() && !cancelled && built == queued && failures == 0;
}
inline bool ReusableImportMarker(const JsonValue & marker, std::string_view stamp)
{
    if (stamp.empty() || !marker.isObject()) return false;
    for (const char * key : {"schema", "native_queued", "native_completed", "native_failures"})
    {
        const auto & value = marker[key];
        if (value.Type() != JsonValueType::UnsignedInt &&
            !(value.Type() == JsonValueType::Int && value.asInt64() >= 0)) return false;
    }
    return marker["schema"].asUInt64() == 2 && marker["stamp"].asString() == stamp &&
           marker["status"].asString() == "ready-compatible-subset" && marker["native_failures"].asUInt64() == 0 &&
           marker["native_completed"].asUInt64() == marker["native_queued"].asUInt64();
}
} // namespace VideoCommon::ShaderProfile
