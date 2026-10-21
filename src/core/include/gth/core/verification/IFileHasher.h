#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace gth::core {

// Content hashing used by backup and verification workflows. Range hashing
// lets callers validate fixed-size chunks without loading large game files
// into memory.
class IFileHasher {
public:
    virtual ~IFileHasher() = default;
    virtual std::optional<std::string> sha256(const std::filesystem::path& path) = 0;
    virtual std::optional<std::string> sha256Range(const std::filesystem::path& path, long long offsetBytes,
                                                    long long lengthBytes) = 0;
};

}  // namespace gth::core
