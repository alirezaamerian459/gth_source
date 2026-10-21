#pragma once

#include "gth/core/index/IPieceDownloader.h"

namespace gth::network {

// Downloads a package piece from Sony's servers over WinHTTP, into a file that
// can be resumed: a partial file left by an interrupted or cancelled attempt
// is continued with an HTTP Range request rather than started over, which
// matters for pieces of several GB. Only addresses on Sony's own
// playstation.net hosts are accepted, and a secure connection to one is only
// trusted when its certificate chain is Sony's pinned root (see
// IsPinnedSonyChain).
class SonyPieceDownloader : public core::IPieceDownloader {
public:
    core::PieceDownloadResult download(const std::string& url, const std::filesystem::path& destination,
                                       long long expectedSizeBytes,
                                       const std::function<bool(long long bytesSoFar)>& onProgress) override;
};

// Whether `url` is an http(s) address on one of Sony's playstation.net hosts.
bool IsSonyDownloadUrl(const std::string& url);

}  // namespace gth::network
