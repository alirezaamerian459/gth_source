#pragma once

#include <vector>

#include "gth/core/index/IStorageScanner.h"
#include "gth/core/index/ITitleLookupService.h"
#include "gth/indexing/FilesystemStorageScanner.h"

namespace gth::indexing {

// Enhanced storage scanner that handles both:
// 1. Traditional directory structure: Platform/Title/Region/Version/...files
// 2. Individual .pkg files in any directory
class EnhancedStorageScanner : public core::IStorageScanner {
public:
    // titleLookup is optional (nullptr = skip online lookup entirely, the
    // default for tests and anywhere offline behavior must stay
    // deterministic) and non-owning - the caller keeps it alive for at
    // least this scanner's lifetime.
    explicit EnhancedStorageScanner(const FilesystemStorageScanner& dirScanner = FilesystemStorageScanner(),
                                     core::ITitleLookupService* titleLookup = nullptr);

    // Scan storage for games - combines directory scan + .pkg file scan
    std::vector<core::ScannedVariant> scan(const std::string& rootPath) override;
    core::ScanDiagnostics lastScanDiagnostics() const override;
    bool pinReleaseVersion(const std::string& rootPath, const std::string& versionRelativePath,
                            const std::string& version, std::string& error) override;

private:
    FilesystemStorageScanner dirScanner_;
    core::ITitleLookupService* titleLookup_;
    core::ScanDiagnostics diagnostics_;

    // Scan directory recursively for .pkg files
    std::vector<core::ScannedVariant> scanForPkgFiles(const std::string& rootPath);
};

}  // namespace gth::indexing
