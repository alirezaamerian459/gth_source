#pragma once

#include <string>
#include <vector>

namespace gth::core {

struct ScannedFile {
    std::string relativePath;  // relative to the version folder
    long long sizeBytes = 0;
    std::string lastModified;  // ISO-8601 UTC
};

struct ScannedVersion {
    std::string version;
    std::string relativePath;  // relative to the storage root
    std::vector<ScannedFile> files;
};

// One {Platform}/{Title}/{Region} container, with every version folder found
// underneath it. A variant with more than one entry in `versions` means
// multiple version folders currently coexist on disk (an in-progress atomic
// update, or a retention policy keeping old copies).
struct ScannedVariant {
    std::string platform;
    std::string title;
    std::string region;
    std::string relativePath;  // relative to the storage root: {Platform}/{Title}/{Region}
    std::vector<ScannedVersion> versions;
};

}  // namespace gth::core
