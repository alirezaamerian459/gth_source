#pragma once

#include "gth/core/index/IStorageScanner.h"

namespace gth::indexing {

// Walks a storage root exactly four levels deep
// ({Platform}/{Title}/{Region}/{Version}/), reporting every folder at that
// depth as a variant along with every regular file underneath it. Folders
// that don't reach that depth, or whose path resolver rejects a segment
// (see DefaultArchivePathResolver), are silently skipped - not every folder
// on a drive is necessarily part of the archive.
class FilesystemStorageScanner : public core::IStorageScanner {
public:
    std::vector<core::ScannedVariant> scan(const std::string& rootPath) override;
};

}  // namespace gth::indexing
