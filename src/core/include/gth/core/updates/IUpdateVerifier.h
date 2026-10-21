#pragma once

#include <optional>
#include <string>

namespace gth::core {

struct VerificationResult {
    bool passed = false;

    // True only if a hash was actually computed and matched. A directory
    // download can pass verification (existence + size checked) with this
    // still false, since there's no defined convention yet for hashing a
    // whole directory - see IUpdateVerifier::verify.
    bool hashVerified = false;

    std::string errorMessage;
};

// Checks a downloaded update before it's allowed anywhere near the archive's
// active version folder: existence, size, and (for a single downloaded
// file) SHA-256 if the source provided one to check against. Verification
// failing here is exactly the case the spec calls "UPDATE FAILED - old
// archive remains untouched" - this type has no way to touch the archive at
// all, so that guarantee is structural, not a promise the caller has to
// keep.
class IUpdateVerifier {
public:
    virtual ~IUpdateVerifier() = default;

    // path is what IUpdateSource::downloadUpdate wrote to (a file or a
    // directory). expectedSizeBytes <= 0 means "unknown, don't check".
    // expectedSha256Hex is checked only when the downloaded content is a
    // single file - see VerificationResult::hashVerified.
    virtual VerificationResult verify(const std::string& path, long long expectedSizeBytes,
                                       const std::optional<std::string>& expectedSha256Hex) = 0;

    // Verify a byte range inside a larger file. Implementations that cannot hash ranges fail closed,
    // so callers never mistake a size-only check for checksum verification.
    virtual VerificationResult verifyRange(const std::string&, long long, long long, const std::string&) {
        VerificationResult result;
        result.errorMessage = "Range verification is not supported.";
        return result;
    }
};

}  // namespace gth::core
