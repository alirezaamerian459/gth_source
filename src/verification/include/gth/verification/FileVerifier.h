#pragma once

#include "gth/core/updates/IUpdateVerifier.h"

namespace gth::verification {

class FileVerifier : public core::IUpdateVerifier {
public:
    core::VerificationResult verify(const std::string& path, long long expectedSizeBytes,
                                     const std::optional<std::string>& expectedSha256Hex) override;
};

}  // namespace gth::verification
