#pragma once

#include <string>
#include <optional>

namespace gth::network {

// Result of attempting to serve a local game file
struct LocalGameServeResult {
    bool success;
    std::string errorMessage;
    std::string gameTitle;
    std::string localPath;
    long long fileSize;
};

// Serves game files from local storage to console via HTTP proxy
// Transparently replaces CDN downloads with local transfers
class ILocalGameServer {
public:
    virtual ~ILocalGameServer() = default;

    // Attempt to serve a game for the given request URL
    // nullopt: nothing to serve locally, the socket was NOT written to - the caller may
    //          fall through to the real origin.
    // value:   a response has been (at least partly) written to the socket, so the caller
    //          must NOT write anything else to it. success == false means the transfer
    //          broke off part-way (e.g. the console disconnected).
    virtual std::optional<LocalGameServeResult> tryServeGameFile(
        const std::string& requestUrl, const std::string& requestHeaders, void* clientSocket) = 0;

    // Check if a game can be served locally (without actually serving it)
    // Used to decide whether to proxy or serve local
    virtual bool canServeLocally(const std::string& requestUrl) = 0;
};

}  // namespace gth::network
