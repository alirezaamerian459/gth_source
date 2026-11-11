#pragma once

#include <string>
#include <vector>

namespace gth::core {

// A read-only virtual path exposed alongside the configured physical root.
// The FTP server resolves the physical path only for the explicitly mapped
// virtual subtree; the ordinary shared root keeps its existing behavior.
struct FtpVirtualMapping {
    std::string virtualPath;
    std::string physicalPath;
    bool readOnly = true;
};

// Configuration for the built-in LAN FTP server (see IFtpServer). port
// defaults to 2121 rather than the well-known FTP port 21 - ports below
// 1024 need administrator elevation to bind on Windows, and this server is
// meant to just work for a regular user sharing a folder between two of
// their own machines.
struct FtpServerConfig {
    std::string rootPath;
    int port = 2121;
    std::vector<FtpVirtualMapping> virtualMappings;
};

}  // namespace gth::core
