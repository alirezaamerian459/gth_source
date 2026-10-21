#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace gth::verification {

// Lowercase hex SHA-256 digest of a file's contents, computed via Windows
// CNG (BCrypt) - no third-party crypto dependency. Returns nullopt if the
// file can't be opened or the OS hashing APIs fail.
std::optional<std::string> computeSha256Hex(const std::string& filePath);
std::optional<std::string> computeSha256Hex(const std::filesystem::path& filePath);

// Lowercase SHA-256 digest of exactly `lengthBytes` starting at `offsetBytes` in a file.
// Returns nullopt if the range is invalid, unreadable, or hashing fails.
std::optional<std::string> computeSha256HexRange(const std::string& filePath, long long offsetBytes,
                                                 long long lengthBytes);
std::optional<std::string> computeSha256HexRange(const std::filesystem::path& filePath, long long offsetBytes,
                                                 long long lengthBytes);

}  // namespace gth::verification
