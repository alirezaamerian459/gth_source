#pragma once

#include <string>

namespace gth::core {

// The parsed pieces of a variant folder path under a storage root, per the
// archive's standard layout: {Platform}/{Title}/{Region}/{Version}/
struct ArchivePathComponents {
    std::string platform;
    std::string title;
    std::string region;
    std::string version;
};

}  // namespace gth::core
