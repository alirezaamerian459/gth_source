#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace gth::core {

struct PieceDownloadResult {
    bool success = false;
    bool cancelled = false;
    std::string error;  // when neither succeeded nor was cancelled
};

// Fetches one file of a package from the publisher's servers.
class IPieceDownloader {
public:
    virtual ~IPieceDownloader() = default;

    // Downloads `url` to `destination`, continuing from whatever part of it is
    // already there, until it is `expectedSizeBytes` long. `onProgress` gets
    // the bytes on disk so far; returning false stops the download and leaves
    // the partial file in place for a later resume. Never throws.
    virtual PieceDownloadResult download(const std::string& url, const std::filesystem::path& destination,
                                         long long expectedSizeBytes,
                                         const std::function<bool(long long bytesSoFar)>& onProgress) = 0;
};

}  // namespace gth::core
