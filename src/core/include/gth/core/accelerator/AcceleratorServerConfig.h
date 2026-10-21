#pragma once

#include <string>

namespace gth::core {

// Configuration for the LAN download-accelerator proxy (see
// IDownloadAcceleratorServer). port defaults to 8080, same "no admin
// elevation needed" reasoning as FtpServerConfig::port. cacheDir holds
// exactly the bytes a real origin server returned for a given URL, each
// entry tagged with the validators (ETag/Last-Modified) needed to
// revalidate it before ever being served again - see
// WinsockCachingProxyServer for how that revalidation works.
struct AcceleratorServerConfig {
    std::string cacheDir;
    int port = 8080;
    bool cacheEnabled = true;
};

}  // namespace gth::core
