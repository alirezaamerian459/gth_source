#pragma once

#include <string>

namespace gth::application {

struct ScanResult {
    bool success = false;
    std::string errorMessage;
    std::string scanJobId;

    int gamesAdded = 0;
    int variantsAdded = 0;
    int versionsAdded = 0;
    int versionsUpdated = 0;
    int versionsMissing = 0;
    int versionsUnchanged = 0;

    int titleLookupsAttempted = 0;
    int titleLookupsResolved = 0;
    int titleLookupsNotFound = 0;
    int titleLookupsFailed = 0;
    std::string lastTitleLookupFailure;
    int releasesNamedFromFiles = 0;
    int releasesNotEligible = 0;

    // Releases the database refused to save (the scan carries on past them).
    int variantsFailed = 0;
    std::string firstVariantError;
};

}  // namespace gth::application
