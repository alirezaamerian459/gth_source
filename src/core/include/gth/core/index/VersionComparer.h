#pragma once

#include <string>

namespace gth::core {

// Compares dotted numeric version strings ("1.04" vs "1.06", "1.9" vs
// "1.10") component-by-component as integers, not lexicographically - a
// plain string compare would wrongly say "1.9" > "1.10". Falls back to
// lexicographic comparison for any component that isn't purely numeric, so
// non-standard version strings still get a deterministic (if unprincipled)
// ordering instead of crashing.
//
// Returns <0 if a<b, 0 if equal, >0 if a>b.
int compareVersions(const std::string& a, const std::string& b);

}  // namespace gth::core
