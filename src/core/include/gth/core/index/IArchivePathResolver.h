#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/index/ArchivePathComponents.h"

namespace gth::core {

// Translates between a variant's folder path (relative to its storage root)
// and its {Platform, Title, Region, Version} components. Real per-platform
// metadata (TitleId/ContentId read from disc/package headers) will plug in
// as an IMetadataParser in a later milestone; until then, folder naming is
// the only source of truth, so this is the one place that convention is
// encoded - callers never parse paths themselves.
class IArchivePathResolver {
public:
    virtual ~IArchivePathResolver() = default;

    // segments is the path split into components, e.g. {"PS5", "Spider-Man 2", "Europe", "1.06"}.
    // Returns nullopt if the segment count/shape doesn't match the convention.
    virtual std::optional<ArchivePathComponents> parse(const std::vector<std::string>& segments) = 0;

    virtual std::string build(const ArchivePathComponents& components) = 0;
};

}  // namespace gth::core
