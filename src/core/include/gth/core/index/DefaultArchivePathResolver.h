#pragma once

#include "gth/core/index/IArchivePathResolver.h"

namespace gth::core {

// {Platform}/{Title}/{Region}/{Version}/ - exactly four path segments, in
// that order. Pure string logic, no filesystem access, so it's testable
// without touching disk.
class DefaultArchivePathResolver : public IArchivePathResolver {
public:
    std::optional<ArchivePathComponents> parse(const std::vector<std::string>& segments) override;
    std::string build(const ArchivePathComponents& components) override;
};

}  // namespace gth::core
