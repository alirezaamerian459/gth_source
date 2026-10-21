#pragma once

#include <string>
#include <vector>

#include "gth/core/index/ScannedVariant.h"

namespace gth::core {

// Walks a storage's root directory and reports every folder that matches
// the archive's {Platform}/{Title}/{Region}/{Version} convention, along with
// the files inside each one. Pure observation, like IStorageDetector - the
// Application layer decides what to do with the results (add/update/mark
// missing) via IGameIndexer.
// What the most recent scan() call learned about the optional online title
// lookup (see ITitleLookupService) - surfaced to the UI so a lookup that
// silently isn't working (no internet, a site blocking requests, ...) is
// visible instead of just looking like "the names never changed".
struct ScanDiagnostics {
    int titleLookupsAttempted = 0;
    int titleLookupsResolved = 0;
    int titleLookupsNotFound = 0;  // reached the site, no matching title
    int titleLookupsFailed = 0;    // couldn't get a usable answer at all
    std::string lastTitleLookupFailure;

    // Why the rest of the releases weren't looked up at all - so a scan that
    // attempted no lookups can say why, instead of saying nothing.
    int releasesNamedFromFiles = 0;      // already named: by the package's own metadata, or a name saved by an earlier scan
    int releasesNotEligible = 0;         // not PS4/PS5 (or no title ID) - no tracker site covers them
};

class IStorageScanner {
public:
    virtual ~IStorageScanner() = default;
    virtual std::vector<ScannedVariant> scan(const std::string& rootPath) = 0;
    virtual ScanDiagnostics lastScanDiagnostics() const { return {}; }

    // Saves a version the user typed in, next to the release's own files, so
    // later scans keep it instead of writing "unknown" over it - PS5 dumps
    // carry no readable version, so this is the one place a real one can come
    // from. `versionRelativePath` is the version's folder key as scan()
    // reported it (containing folder + Content ID). An empty `version` hands
    // the field back to automatic detection. Returns false with a reason in
    // `error` when there's nowhere to save it (no Content ID, read-only share).
    virtual bool pinReleaseVersion(const std::string& rootPath, const std::string& versionRelativePath,
                                    const std::string& version, std::string& error) {
        (void)rootPath;
        (void)versionRelativePath;
        (void)version;
        error = "This scanner can't save versions.";
        return false;
    }
};

}  // namespace gth::core
