#pragma once

#include "gth/core/updates/IUpdateVerifier.h"
#include "gth/core/verification/IFileHasher.h"

namespace gth::verification {

class FileVerifier : public core::IUpdateVerifier, public core::IFileHasher {
public:
    core::VerificationResult verify(const std::string& path, long long expectedSizeBytes,
                                     const std::optional<std::string>& expectedSha256Hex) override;
    core::VerificationResult verifyRange(const std::string& path, long long offsetBytes, long long expectedSizeBytes,
                                          const std::string& expectedSha256Hex) override;
    std::optional<std::string> sha256(const std::filesystem::path& path) override;
    std::optional<std::string> sha256Range(const std::filesystem::path& path, long long offsetBytes,
                                            long long lengthBytes) override;
};

}  // namespace gth::verification
