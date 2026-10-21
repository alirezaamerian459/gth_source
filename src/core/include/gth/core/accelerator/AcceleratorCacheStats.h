#pragma once

#include <cstdint>

namespace gth::core {

struct AcceleratorCacheStats {
    std::uint64_t entryCount = 0;
    std::uint64_t totalBytes = 0;
    std::uint64_t hitCount = 0;
    std::uint64_t missCount = 0;
};

}  // namespace gth::core
