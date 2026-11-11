#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gth::application {

struct JbGameEntry {
    std::string title;
    std::string titleId;
    std::string platform;
    std::filesystem::path path;
    std::string storageId;
    std::string storageName;
    std::string storageRootPath;
};

struct JbLibraryScanResult {
    std::vector<JbGameEntry> entries;
    int libraryFolderCount = 0;
    int storageCount = 0;
    int exfatStorageCount = 0;
    std::string error;
};

// Shared read-only catalog for the JB tab, the hosted FTP server, and the
// LAN Console Proxy. The catalog never moves, modifies, or deletes files.
class IJbLibraryService {
public:
    virtual ~IJbLibraryService() = default;

    // Scan available local and reachable share storage locations. An empty
    // target scans every eligible storage; a non-empty target is a
    // Storage::storageId.
    virtual JbLibraryScanResult scan(const std::string& targetStorageId = {}) = 0;

    // Return the last completed scan without touching the filesystem.
    virtual JbLibraryScanResult snapshot() const = 0;

    // Find a package/archive by its exact filename (case-insensitive) inside
    // the JB catalog. The first existing match is returned.
    virtual std::optional<std::filesystem::path> findFileByName(const std::string& fileName) = 0;
};

}  // namespace gth::application
