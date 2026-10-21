#pragma once

#include <string>

namespace gth::core {

// Configuration for the built-in LAN FTP server (see IFtpServer). port
// defaults to 2121 rather than the well-known FTP port 21 - ports below
// 1024 need administrator elevation to bind on Windows, and this server is
// meant to just work for a regular user sharing a folder between two of
// their own machines.
struct FtpServerConfig {
    std::string rootPath;
    int port = 2121;
};

}  // namespace gth::core
