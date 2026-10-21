#pragma once

#include <vector>

#include "gth/core/entities/GameVariant.h"
#include "gth/core/entities/UpdateInfo.h"

namespace gth::application {

// Compares each GameVariant's current version against every registered
// IUpdateSource. A variant with no IsCurrent version yet (nothing indexed)
// is reported NoSafeMatch, per the spec's rule that an update never proceeds
// on an uncertain match.
class IUpdateDetector {
public:
    virtual ~IUpdateDetector() = default;

    virtual core::UpdateInfo detectForVariant(const core::GameVariant& variant) = 0;
    virtual std::vector<core::UpdateInfo> detectForAllVariants() = 0;
};

}  // namespace gth::application
