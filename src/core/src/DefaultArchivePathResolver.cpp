#include "gth/core/index/DefaultArchivePathResolver.h"

namespace gth::core {

std::optional<ArchivePathComponents> DefaultArchivePathResolver::parse(const std::vector<std::string>& segments) {
    if (segments.size() != 4) return std::nullopt;

    for (const auto& segment : segments) {
        if (segment.empty()) return std::nullopt;
    }

    ArchivePathComponents components;
    components.platform = segments[0];
    components.title = segments[1];
    components.region = segments[2];
    components.version = segments[3];
    return components;
}

std::string DefaultArchivePathResolver::build(const ArchivePathComponents& components) {
    return components.platform + "\\" + components.title + "\\" + components.region + "\\" + components.version;
}

}  // namespace gth::core
