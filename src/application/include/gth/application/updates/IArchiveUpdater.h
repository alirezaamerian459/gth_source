#pragma once

#include <string>

#include "gth/application/updates/RetentionPolicy.h"

namespace gth::application {

struct ReplacementOutcome {
    bool success = false;
    std::string errorMessage;
};

// The one component allowed to modify the archive itself. Everything
// upstream (download, verify) only ever touches a temporary path; this is
// where a verified update actually becomes part of the archive - and it is
// built so that failure at any point leaves the previous current version
// exactly as it was:
//   1. Safety checks (free space, no path collision) - nothing written yet.
//   2. The new version is placed at its own new path - the previous
//      current version's folder is never opened, let alone modified.
//   3. Only after the new version is safely on disk does the database flip
//      which GameVersion is current.
//   4. Retention policy purges old versions - a deliberate, policy-driven
//      cleanup, distinct from a version going "Missing" because a scan
//      didn't find it.
class IArchiveUpdater {
public:
    virtual ~IArchiveUpdater() = default;

    virtual ReplacementOutcome applyVerifiedUpdate(const std::string& updateJobId, const std::string& verifiedPath,
                                                    const RetentionPolicy& policy) = 0;
};

}  // namespace gth::application
