#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "ILocalGameServer.h"

namespace gth::application {
class ICdnUrlService;
class ILocalFileIndex;
}  // namespace gth::application

namespace gth::network {

// Serves package downloads from the user's own HDDs instead of the internet, the way PSX Download
// Helper does: the console asks (through the proxy) for one specific file, and if that exact file
// is on disk it is streamed back with proper HTTP range support.
//
// A request is matched, in order, by
//   1. a path the user mapped to that exact URL in the FTP tab's CDN URL Log (a file is served as
//      is; a folder is searched for the file named in the URL), then
//   2. the file name in the URL, looked up in the indexed game library (.pkg/.pup only).
// Anything else is left alone so the proxy can fetch it from the real origin.
class LocalGameServer : public ILocalGameServer {
public:
    // Either dependency may be null, which just disables that way of finding a file.
    explicit LocalGameServer(gth::application::ICdnUrlService* cdnUrlService = nullptr,
                             gth::application::ILocalFileIndex* localFileIndex = nullptr);

    std::optional<LocalGameServeResult> tryServeGameFile(const std::string& requestUrl,
                                                          const std::string& requestHeaders,
                                                          void* clientSocket) override;

    bool canServeLocally(const std::string& requestUrl) override;

private:
    struct Resolved {
        std::filesystem::path file;
        std::string fileName;       // as requested in the URL
        std::string cdnUrlId;       // the URL's row in the CDN URL Log, if it has one
        std::string existingMark;
        bool rowHasLocalPath = false;
        bool autoMatched = false;   // found through the library index rather than a user mapping
    };

    std::optional<Resolved> resolve(const std::string& requestUrl);

    // Streams the file (honouring a Range header) with HTTP response headers.
    // Returns false if the transfer broke off part-way.
    bool streamFileToSocket(const std::filesystem::path& filePath, const std::string& requestHeaders,
                            void* clientSocket);

    void sendHttpHeaders(void* clientSocket, long long fileSize, const std::string& lastModified,
                         bool isRangeRequest = false, long long rangeStart = 0, long long rangeEnd = 0);

    void sendHttpError(void* clientSocket, int statusCode, const std::string& statusMessage);

    // Parses "bytes=start-end", "bytes=start-" and "bytes=-suffix".
    bool parseRangeHeader(const std::string& rangeHeader, long long fileSize, long long& outStart, long long& outEnd);

    gth::application::ICdnUrlService* cdnUrlService_;
    gth::application::ILocalFileIndex* localFileIndex_;
};

}  // namespace gth::network
