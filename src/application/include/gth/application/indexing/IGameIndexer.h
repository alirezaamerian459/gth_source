#pragma once

#include <string>

#include "gth/application/indexing/ScanResult.h"
#include "gth/application/indexing/VersionEditResult.h"

namespace gth::application {

// Orchestrates one indexing pass over a Storage: scan its folder tree,
// reconcile against Games/GameVariants/GameFiles, record a ScanJob. This is
// the boundary the UI calls through - it never talks to IStorageScanner or
// the index repositories directly.
class IGameIndexer {
public:
    virtual ~IGameIndexer() = default;
    virtual ScanResult scanStorage(const std::string& storageId) = 0;

    // PS5 dumps carry no readable version, so the user supplies it. Both
    // calls save it next to the game's files (so rescans keep it) and update
    // the library. An empty `version` hands the game back to automatic
    // detection at the next scan.
    virtual SetVersionResult setVersion(const std::string& gameVariantId, const std::string& version) = 0;

    // One "TITLEID version" per line (comma/tab/space separated, '#' starts a
    // comment); every game with that title ID gets that version.
    virtual ImportVersionsResult importVersions(const std::string& text) = 0;
};

}  // namespace gth::application
