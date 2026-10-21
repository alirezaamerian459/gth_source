#pragma once

#include <optional>
#include <string>

namespace gth::verification {

// Lowercase hex SHA-256 digest of a file's contents, computed via Windows
// CNG (BCrypt) - no third-party crypto dependency. Returns nullopt if the
// file can't be opened or the OS hashing APIs fail.
std::optional<std::string> computeSha256Hex(const std::string& filePath);

}  // namespace gth::verification
