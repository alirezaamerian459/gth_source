#pragma once

namespace gth::application {

// How many non-current, still-Active versions to keep on disk after a
// successful update. -1 means "keep all" (never purge); 0 means "keep
// latest only" (purge the previous version immediately); N keeps the N
// newest previous versions. Matches the specification's Section 16 policy
// options exactly (Keep latest only / Keep latest + previous / Keep N
// versions / Keep all).
struct RetentionPolicy {
    int keepPreviousVersions = 1;
};

}  // namespace gth::application
