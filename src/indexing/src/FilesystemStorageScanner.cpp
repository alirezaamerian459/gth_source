#include "gth/indexing/FilesystemStorageScanner.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <map>

#include "gth/core/index/DefaultArchivePathResolver.h"

namespace gth::indexing {

namespace fs = std::filesystem;
using core::ScannedFile;
using core::ScannedVariant;
using core::ScannedVersion;

namespace {

std::string fileTimeToIso8601(fs::file_time_type ft) {
    const auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(ft);
    const auto secs = std::chrono::floor<std::chrono::seconds>(systemTime);
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", secs);
}

// std::filesystem on Windows enforces the legacy ~260 char MAX_PATH unless
// the path is prefixed with \\?\ (or \\?\UNC\ for network shares). Without
// this, a single deeply-nested file/folder name throws a filesystem_error
// mid-scan once a library reaches multi-TB size.
fs::path toLongPath(const std::string& rawPath) {
    std::string normalized = rawPath;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    if (normalized.rfind("\\\\?\\", 0) == 0) return fs::path(normalized);
    if (normalized.rfind("\\\\", 0) == 0) return fs::path("\\\\?\\UNC\\" + normalized.substr(2));
    return fs::path("\\\\?\\" + normalized);
}

}  // namespace

std::vector<ScannedVariant> FilesystemStorageScanner::scan(const std::string& rootPath) {
    std::vector<ScannedVariant> results;

    std::error_code ec;
    const fs::path shortRoot(rootPath);
    if (!fs::exists(shortRoot, ec) || !fs::is_directory(shortRoot, ec)) return results;
    const fs::path root = toLongPath(rootPath);

    core::DefaultArchivePathResolver resolver;
    const auto opts = fs::directory_options::skip_permission_denied;

    // Container key (Platform/Title/Region) -> index into results, so
    // multiple version folders under the same container get grouped into
    // one ScannedVariant instead of one per version.
    std::map<std::string, size_t> containerIndex;

    for (const auto& platformEntry : fs::directory_iterator(root, opts, ec)) {
        if (!platformEntry.is_directory(ec)) continue;

        for (const auto& titleEntry : fs::directory_iterator(platformEntry.path(), opts, ec)) {
            if (!titleEntry.is_directory(ec)) continue;

            for (const auto& regionEntry : fs::directory_iterator(titleEntry.path(), opts, ec)) {
                if (!regionEntry.is_directory(ec)) continue;

                for (const auto& versionEntry : fs::directory_iterator(regionEntry.path(), opts, ec)) {
                    if (!versionEntry.is_directory(ec)) continue;

                    const std::vector<std::string> segments = {
                        platformEntry.path().filename().string(), titleEntry.path().filename().string(),
                        regionEntry.path().filename().string(), versionEntry.path().filename().string()};

                    auto components = resolver.parse(segments);
                    if (!components) continue;

                    ScannedVersion version;
                    version.version = components->version;
                    version.relativePath = fs::relative(versionEntry.path(), root, ec).string();

                    // Isolated per version folder: a problem entry here (e.g. an
                    // unusually long or malformed path) should only cost this one
                    // version's file listing, not abort scanning of the rest of
                    // the Platform/Title/Region/Version tree.
                    try {
                        for (const auto& fileEntry : fs::recursive_directory_iterator(versionEntry.path(), opts, ec)) {
                            if (!fileEntry.is_regular_file(ec)) continue;

                            ScannedFile file;
                            file.relativePath = fs::relative(fileEntry.path(), versionEntry.path(), ec).string();
                            file.sizeBytes = static_cast<long long>(fileEntry.file_size(ec));
                            file.lastModified = fileTimeToIso8601(fileEntry.last_write_time(ec));
                            version.files.push_back(std::move(file));
                        }
                    } catch (const std::exception&) {
                        // Keep whatever files were collected before the error.
                    }

                    const std::string containerRelativePath =
                        fs::relative(regionEntry.path(), root, ec).string();
                    const std::string containerKey =
                        components->platform + "|" + components->title + "|" + components->region;

                    auto it = containerIndex.find(containerKey);
                    if (it == containerIndex.end()) {
                        ScannedVariant variant;
                        variant.platform = components->platform;
                        variant.title = components->title;
                        variant.region = components->region;
                        variant.relativePath = containerRelativePath;
                        variant.versions.push_back(std::move(version));

                        results.push_back(std::move(variant));
                        containerIndex.emplace(containerKey, results.size() - 1);
                    } else {
                        results[it->second].versions.push_back(std::move(version));
                    }
                }
            }
        }
    }

    return results;
}

}  // namespace gth::indexing
