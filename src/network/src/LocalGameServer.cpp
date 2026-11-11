#include "gth/network/LocalGameServer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "gth/application/ftp/ICdnUrlService.h"
#include "gth/application/jb/IJbLibraryService.h"
#include "gth/application/storage/ILocalFileIndex.h"
#include "gth/logging/Logging.h"

#pragma comment(lib, "ws2_32.lib")

namespace {

namespace fs = std::filesystem;

std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool sendToSocket(void* socket, const std::string& data) {
    SOCKET s = static_cast<SOCKET>(reinterpret_cast<intptr_t>(socket));
    int totalSent = 0;
    while (totalSent < static_cast<int>(data.size())) {
        int sent = ::send(s, data.c_str() + totalSent, static_cast<int>(data.size()) - totalSent, 0);
        if (sent == SOCKET_ERROR) {
            return false;
        }
        totalSent += sent;
    }
    return true;
}

// Streams bytes [startByte, endByte] of the file in 64KB chunks.
bool sendFileToSocket(void* socket, const fs::path& filePath, long long startByte, long long endByte) {
    SOCKET s = static_cast<SOCKET>(reinterpret_cast<intptr_t>(socket));
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return false;
    }

    file.seekg(startByte, std::ios::beg);

    constexpr size_t chunkSize = 65536;
    std::vector<char> buffer(chunkSize);
    long long bytesRemaining = endByte - startByte + 1;

    while (bytesRemaining > 0 && file.good()) {
        const size_t toRead = std::min(static_cast<size_t>(bytesRemaining), chunkSize);
        file.read(buffer.data(), static_cast<std::streamsize>(toRead));
        const size_t bytesRead = static_cast<size_t>(file.gcount());
        if (bytesRead == 0) break;

        size_t sentTotal = 0;
        while (sentTotal < bytesRead) {
            const int sent =
                ::send(s, buffer.data() + sentTotal, static_cast<int>(bytesRead - sentTotal), 0);
            if (sent == SOCKET_ERROR) {
                return false;
            }
            sentTotal += static_cast<size_t>(sent);
        }
        bytesRemaining -= static_cast<long long>(bytesRead);
    }

    return bytesRemaining == 0;
}

// Paths saved from the FTP tab's CDN URL Log are UTF-8 (see FtpView's WideToUtf8) - decode them
// explicitly instead of letting std::filesystem assume the ANSI code page.
fs::path pathFromUtf8(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string pathToUtf8(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// A mapped file is served whatever it is called; a mapped folder is searched for the file the
// URL names. Never guess some other file in the folder: a console verifies what it downloads, so
// handing it the wrong package is worse than letting it fetch the real one.
fs::path findMappedFile(const fs::path& mapped, const std::string& wantedLowerName) {
    std::error_code ec;
    if (fs::is_regular_file(mapped, ec)) return mapped;
    if (wantedLowerName.empty() || !fs::is_directory(mapped, ec)) return {};

    fs::recursive_directory_iterator it(mapped, fs::directory_options::skip_permission_denied, ec);
    for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc)) continue;
        if (toLower(it->path().filename().string()) == wantedLowerName) return it->path();
    }
    return {};
}

std::string httpDate(std::chrono::system_clock::time_point tp) {
    static const char* const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm g{};
    gmtime_s(&g, &t);
    std::array<char, 64> buf{};
    std::snprintf(buf.data(), buf.size(), "%s, %02d %s %04d %02d:%02d:%02d GMT", kDays[g.tm_wday], g.tm_mday,
                  kMonths[g.tm_mon], g.tm_year + 1900, g.tm_hour, g.tm_min, g.tm_sec);
    return buf.data();
}

std::string lastModifiedOf(const fs::path& file) {
    std::error_code ec;
    const auto ft = fs::last_write_time(file, ec);
    if (ec) return httpDate(std::chrono::system_clock::now());
    return httpDate(std::chrono::clock_cast<std::chrono::system_clock>(ft));
}

}  // namespace

namespace gth::network {

LocalGameServer::LocalGameServer(gth::application::ICdnUrlService* cdnUrlService,
                                 gth::application::ILocalFileIndex* localFileIndex,
                                 gth::application::IJbLibraryService* jbLibraryService)
    : cdnUrlService_(cdnUrlService), localFileIndex_(localFileIndex), jbLibraryService_(jbLibraryService) {}

std::optional<LocalGameServer::Resolved> LocalGameServer::resolve(const std::string& requestUrl) {
    Resolved resolved;
    resolved.fileName = gth::application::packageFileNameFromUrl(requestUrl);
    const std::string wantedLower = toLower(resolved.fileName);

    // 1. A path the user mapped to this exact URL in the FTP tab.
    if (cdnUrlService_) {
        for (const auto& row : cdnUrlService_->getAll()) {
            if (row.url != requestUrl) continue;

            resolved.cdnUrlId = row.cdnUrlId;
            resolved.existingMark = row.markText;
            resolved.rowHasLocalPath = !row.localPath.empty();
            if (resolved.rowHasLocalPath) {
                auto mapped = findMappedFile(pathFromUtf8(row.localPath), wantedLower);
                if (!mapped.empty()) {
                    resolved.file = std::move(mapped);
                    return resolved;
                }
                GTH_LOG_WARN("LocalGameServer: nothing servable at the path mapped to {} ({})", requestUrl,
                             row.localPath);
            }
            break;
        }
    }

    // 2. The exact file name, anywhere in the indexed library.
    if (localFileIndex_ && gth::application::isServablePackageName(resolved.fileName)) {
        if (auto found = localFileIndex_->findByFileName(resolved.fileName); found.has_value()) {
            resolved.file = std::move(*found);
            resolved.autoMatched = true;
            return resolved;
        }
    }

    // JB folders are intentionally separate from the normal indexed library.
    // Consult the shared JB catalog only after the existing library lookup
    // misses, so ordinary proxy traffic keeps its original precedence and
    // behavior.
    if (jbLibraryService_) {
        if (auto found = jbLibraryService_->findFileByName(resolved.fileName); found.has_value()) {
            resolved.file = std::move(*found);
            resolved.autoMatched = true;
            return resolved;
        }
    }

    return std::nullopt;
}

bool LocalGameServer::canServeLocally(const std::string& requestUrl) { return resolve(requestUrl).has_value(); }

std::optional<LocalGameServeResult> LocalGameServer::tryServeGameFile(const std::string& requestUrl,
                                                                       const std::string& requestHeaders,
                                                                       void* clientSocket) {
    if (!clientSocket) {
        return std::nullopt;
    }

    // Nothing to serve (or the file/drive isn't there): leave the socket untouched so the proxy can
    // still fetch from the real origin instead of the console getting a broken response.
    const auto resolved = resolve(requestUrl);
    if (!resolved) {
        return std::nullopt;
    }

    LocalGameServeResult result{};
    result.gameTitle = resolved->fileName;
    result.localPath = pathToUtf8(resolved->file);
    std::error_code sizeEc;
    const auto size = fs::file_size(resolved->file, sizeEc);
    result.fileSize = sizeEc ? 0 : static_cast<long long>(size);

    // Show the match in the CDN URL Log (Source: Local) so it is visible - and overridable - there.
    if (cdnUrlService_ && resolved->autoMatched && !resolved->cdnUrlId.empty() && !resolved->rowHasLocalPath) {
        try {
            cdnUrlService_->updateLocalPath(resolved->cdnUrlId, result.localPath);
            if (resolved->existingMark.empty()) {
                cdnUrlService_->updateMark(resolved->cdnUrlId, "Local: " + resolved->fileName);
            }
        } catch (...) {
        }
    }

    // From here on a response has been started on the socket, so always return a value: the proxy
    // must not append a second (origin) response after it.
    result.success = streamFileToSocket(resolved->file, requestHeaders, clientSocket);
    if (result.success) {
        GTH_LOG_INFO("LocalGameServer: served {} ({} bytes) from disk", result.localPath, result.fileSize);
    } else {
        result.errorMessage = "transfer interrupted";
        GTH_LOG_WARN("LocalGameServer: transfer of {} interrupted", result.localPath);
    }
    return result;
}

bool LocalGameServer::streamFileToSocket(const fs::path& filePath, const std::string& requestHeaders,
                                          void* clientSocket) {
    std::error_code ec;
    const auto sizeRaw = fs::file_size(filePath, ec);
    if (ec) {
        sendHttpError(clientSocket, 404, "Not Found");
        return false;
    }
    const long long fileSize = static_cast<long long>(sizeRaw);

    // Header names are case-insensitive (consoles send "range:" as often as "Range:")
    std::string rangeHeader;
    const std::string lowerHeaders = toLower(requestHeaders);
    const size_t rangePos = lowerHeaders.find("\nrange:");
    if (rangePos != std::string::npos) {
        const size_t valueStart = rangePos + 7;
        size_t endPos = requestHeaders.find("\r\n", valueStart);
        if (endPos == std::string::npos) endPos = requestHeaders.size();
        rangeHeader = requestHeaders.substr(valueStart, endPos - valueStart);
        const auto first = rangeHeader.find_first_not_of(" \t");
        rangeHeader = first == std::string::npos ? "" : rangeHeader.substr(first);
    }

    long long rangeStart = 0;
    long long rangeEnd = fileSize - 1;
    const std::string lastModified = lastModifiedOf(filePath);

    if (!rangeHeader.empty() && parseRangeHeader(rangeHeader, fileSize, rangeStart, rangeEnd)) {
        sendHttpHeaders(clientSocket, fileSize, lastModified, true, rangeStart, rangeEnd);
    } else {
        rangeStart = 0;
        rangeEnd = fileSize - 1;
        sendHttpHeaders(clientSocket, fileSize, lastModified, false);
    }

    return sendFileToSocket(clientSocket, filePath, rangeStart, rangeEnd);
}

void LocalGameServer::sendHttpHeaders(void* clientSocket, long long fileSize, const std::string& lastModified,
                                      bool isRangeRequest, long long rangeStart, long long rangeEnd) {
    std::ostringstream oss;

    if (isRangeRequest) {
        oss << "HTTP/1.1 206 Partial Content\r\n";
        oss << "Content-Range: bytes " << rangeStart << "-" << rangeEnd << "/" << fileSize << "\r\n";
    } else {
        oss << "HTTP/1.1 200 OK\r\n";
    }

    oss << "Server: Apache\r\n";
    oss << "Accept-Ranges: bytes\r\n";
    oss << "Cache-Control: max-age=3600\r\n";
    oss << "Content-Type: application/octet-stream\r\n";
    oss << "Date: " << httpDate(std::chrono::system_clock::now()) << "\r\n";
    oss << "Last-Modified: " << lastModified << "\r\n";
    oss << "Content-Length: " << (isRangeRequest ? (rangeEnd - rangeStart + 1) : fileSize) << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";

    sendToSocket(clientSocket, oss.str());
}

void LocalGameServer::sendHttpError(void* clientSocket, int statusCode, const std::string& statusMessage) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode << " " << statusMessage << "\r\n";
    oss << "Content-Type: text/html\r\n";
    oss << "Connection: close\r\n";
    oss << "Content-Length: 0\r\n";
    oss << "\r\n";

    sendToSocket(clientSocket, oss.str());
}

bool LocalGameServer::parseRangeHeader(const std::string& rangeHeader, long long fileSize, long long& outStart,
                                        long long& outEnd) {
    // Parse "bytes=start-end" format
    size_t eqPos = rangeHeader.find('=');
    if (eqPos == std::string::npos || toLower(rangeHeader.substr(0, eqPos)) != "bytes") {
        return false;
    }

    std::string range = rangeHeader.substr(eqPos + 1);
    size_t dashPos = range.find('-');
    if (dashPos == std::string::npos) {
        return false;
    }

    try {
        std::string startStr = range.substr(0, dashPos);
        std::string endStr = range.substr(dashPos + 1);

        // Suffix range ("bytes=-500" = the last 500 bytes)
        if (startStr.empty()) {
            if (endStr.empty()) return false;
            const long long suffix = std::stoll(endStr);
            if (suffix <= 0) return false;
            outStart = suffix >= fileSize ? 0 : fileSize - suffix;
            outEnd = fileSize - 1;
            return true;
        }

        outStart = std::stoll(startStr);
        outEnd = !endStr.empty() ? std::min(std::stoll(endStr), fileSize - 1) : fileSize - 1;

        return outStart >= 0 && outStart <= outEnd && outStart < fileSize;
    } catch (...) {
        return false;
    }
}

}  // namespace gth::network
