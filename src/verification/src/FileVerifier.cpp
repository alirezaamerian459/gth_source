#include "gth/verification/FileVerifier.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>

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
        result.errorMessage = std::format("Size mismatch: expected {} bytes, found {} bytes", expectedSizeBytes,
                                          actualSize);
        return result;
    }

    if (expectedSha256Hex.has_value() && !expectedSha256Hex->empty() && !isDirectory) {
        auto actualHash = computeSha256Hex(path);
        if (!actualHash.has_value()) {
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

VerificationResult FileVerifier::verifyRange(const std::string& path, long long offsetBytes,
                                               long long expectedSizeBytes,
                                               const std::string& expectedSha256Hex) {
    VerificationResult result;
    if (offsetBytes < 0 || expectedSizeBytes <= 0 || expectedSha256Hex.empty()) {
        result.errorMessage = "Invalid file range or expected SHA-256 checksum.";
        return result;
    }

    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        result.errorMessage = "Range verification requires a readable regular file: " + path;
        return result;
    }
    const auto fileSize = static_cast<long long>(fs::file_size(path, ec));
    if (ec || offsetBytes > fileSize || expectedSizeBytes > fileSize - offsetBytes) {
        result.errorMessage = "The requested range is outside the file.";
        return result;
    }

    const auto actualHash = computeSha256HexRange(path, offsetBytes, expectedSizeBytes);
    if (!actualHash.has_value()) {
        result.errorMessage = "Failed to compute the range hash for: " + path;
        return result;
    }
    if (!equalsIgnoreCase(*actualHash, expectedSha256Hex)) {
        result.errorMessage = "Hash mismatch: expected " + expectedSha256Hex + ", computed " + *actualHash;
        return result;
    }

    result.passed = true;
    result.hashVerified = true;
    return result;
}

std::optional<std::string> FileVerifier::sha256(const std::filesystem::path& path) { return computeSha256Hex(path); }

std::optional<std::string> FileVerifier::sha256Range(const std::filesystem::path& path, long long offsetBytes,
                                                      long long lengthBytes) {
    return computeSha256HexRange(path, offsetBytes, lengthBytes);
}

}  // namespace gth::verification
