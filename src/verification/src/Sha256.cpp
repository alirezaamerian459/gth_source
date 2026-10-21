#include "gth/verification/Sha256.h"

#include <windows.h>

#include <algorithm>
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace gth::verification {

namespace {

// RAII so every BCrypt handle gets closed on every return path, including
// the early-out error cases below.
struct BCryptAlgHandle {
    BCRYPT_ALG_HANDLE handle = nullptr;
    ~BCryptAlgHandle() {
        if (handle) ::BCryptCloseAlgorithmProvider(handle, 0);
    }
};

struct BCryptHashHandle {
    BCRYPT_HASH_HANDLE handle = nullptr;
    ~BCryptHashHandle() {
        if (handle) ::BCryptDestroyHash(handle);
    }
};

}  // namespace

std::optional<std::string> computeSha256Hex(const std::string& filePath) {
    return computeSha256Hex(std::filesystem::path(filePath));
}

std::optional<std::string> computeSha256Hex(const std::filesystem::path& filePath) {
    BCryptAlgHandle alg;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg.handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return std::nullopt;
    }

    DWORD hashObjectLength = 0;
    DWORD bytesWritten = 0;
    if (!BCRYPT_SUCCESS(::BCryptGetProperty(alg.handle, BCRYPT_OBJECT_LENGTH,
                                             reinterpret_cast<PUCHAR>(&hashObjectLength), sizeof(DWORD),
                                             &bytesWritten, 0))) {
        return std::nullopt;
    }
    std::vector<UCHAR> hashObject(hashObjectLength);

    DWORD hashLength = 0;
    if (!BCRYPT_SUCCESS(::BCryptGetProperty(alg.handle, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength),
                                             sizeof(DWORD), &bytesWritten, 0))) {
        return std::nullopt;
    }
    std::vector<UCHAR> hashValue(hashLength);

    BCryptHashHandle hash;
    if (!BCRYPT_SUCCESS(
            ::BCryptCreateHash(alg.handle, &hash.handle, hashObject.data(), hashObjectLength, nullptr, 0, 0))) {
        return std::nullopt;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return std::nullopt;

    std::vector<char> buffer(1 << 16);
    while (true) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytesRead = file.gcount();
        if (bytesRead > 0) {
            if (!BCRYPT_SUCCESS(::BCryptHashData(hash.handle, reinterpret_cast<PUCHAR>(buffer.data()),
                                                  static_cast<ULONG>(bytesRead), 0))) {
                return std::nullopt;
            }
        }
        if (bytesRead < static_cast<std::streamsize>(buffer.size())) break;  // EOF or read error
    }

    if (!BCRYPT_SUCCESS(::BCryptFinishHash(hash.handle, hashValue.data(), hashLength, 0))) {
        return std::nullopt;
    }

    std::ostringstream hex;
    for (const auto byte : hashValue) {
        hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return hex.str();
}

std::optional<std::string> computeSha256HexRange(const std::string& filePath, long long offsetBytes,
                                                 long long lengthBytes) {
    return computeSha256HexRange(std::filesystem::path(filePath), offsetBytes, lengthBytes);
}

std::optional<std::string> computeSha256HexRange(const std::filesystem::path& filePath, long long offsetBytes,
                                                 long long lengthBytes) {
    if (offsetBytes < 0 || lengthBytes < 0) return std::nullopt;

    BCryptAlgHandle alg;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg.handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return std::nullopt;
    }

    DWORD hashObjectLength = 0;
    DWORD bytesWritten = 0;
    if (!BCRYPT_SUCCESS(::BCryptGetProperty(alg.handle, BCRYPT_OBJECT_LENGTH,
                                             reinterpret_cast<PUCHAR>(&hashObjectLength), sizeof(DWORD),
                                             &bytesWritten, 0))) {
        return std::nullopt;
    }
    std::vector<UCHAR> hashObject(hashObjectLength);

    DWORD hashLength = 0;
    if (!BCRYPT_SUCCESS(::BCryptGetProperty(alg.handle, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength),
                                             sizeof(DWORD), &bytesWritten, 0))) {
        return std::nullopt;
    }
    std::vector<UCHAR> hashValue(hashLength);

    BCryptHashHandle hash;
    if (!BCRYPT_SUCCESS(
            ::BCryptCreateHash(alg.handle, &hash.handle, hashObject.data(), hashObjectLength, nullptr, 0, 0))) {
        return std::nullopt;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    file.seekg(offsetBytes, std::ios::beg);
    if (!file) return std::nullopt;

    std::vector<char> buffer(1 << 16);
    long long remaining = lengthBytes;
    while (remaining > 0) {
        const auto requested = static_cast<std::streamsize>(std::min<long long>(remaining, buffer.size()));
        file.read(buffer.data(), requested);
        const auto bytesRead = file.gcount();
        if (bytesRead <= 0 || !BCRYPT_SUCCESS(::BCryptHashData(hash.handle, reinterpret_cast<PUCHAR>(buffer.data()),
                                                                static_cast<ULONG>(bytesRead), 0))) {
            return std::nullopt;
        }
        remaining -= bytesRead;
        if (bytesRead != requested && remaining > 0) return std::nullopt;
    }

    if (!BCRYPT_SUCCESS(::BCryptFinishHash(hash.handle, hashValue.data(), hashLength, 0))) {
        return std::nullopt;
    }

    std::ostringstream hex;
    for (const auto byte : hashValue) {
        hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return hex.str();
}

}  // namespace gth::verification
