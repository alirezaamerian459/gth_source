#include "gth/verification/FileVerifier.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "gth/verification/Sha256.h"

namespace gth::verification {

namespace fs = std::filesystem;
using core::VerificationResult;

namespace {

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(),
                       [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) ==
                                                    std::tolower(static_cast<unsigned char>(y)); });
}

}  // namespace

VerificationResult FileVerifier::verify(const std::string& path, long long expectedSizeBytes,
                                         const std::optional<std::string>& expectedSha256Hex) {
    VerificationResult result;
    std::error_code ec;

    if (!fs::exists(path, ec)) {
        result.errorMessage = "Downloaded content not found at: " + path;
        return result;
    }

    const bool isDirectory = fs::is_directory(path, ec);
    long long actualSize = 0;

    if (isDirectory) {
        for (const auto& entry :
             fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec)) {
            if (entry.is_regular_file(ec)) actualSize += static_cast<long long>(entry.file_size(ec));
        }
    } else {
        actualSize = static_cast<long long>(fs::file_size(path, ec));
    }

    if (expectedSizeBytes > 0 && actualSize != expectedSizeBytes) {
        result.errorMessage = "Size mismatch: expected " + std::to_string(expectedSizeBytes) + " bytes, found " +
                               std::to_string(actualSize) + " bytes";
        return result;
    }

    if (expectedSha256Hex && !expectedSha256Hex->empty() && !isDirectory) {
        auto actualHash = computeSha256Hex(path);
        if (!actualHash) {
            result.errorMessage = "Failed to compute hash for: " + path;
            return result;
        }

        if (!equalsIgnoreCase(*actualHash, *expectedSha256Hex)) {
            result.errorMessage = "Hash mismatch: expected " + *expectedSha256Hex + ", computed " + *actualHash;
            return result;
        }

        result.hashVerified = true;
    }

    result.passed = true;
    return result;
}

}  // namespace gth::verification
