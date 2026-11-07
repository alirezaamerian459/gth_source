#include "gth/acceleratorserver/WinsockCachingProxyServer.h"

#include "gth/acceleratorserver/WinsockCachingProxyServerInternal.h"

#include <winsock2.h>

#include <ws2tcpip.h>

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <span>
#include <string_view>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace gth::acceleratorserver {

namespace fs = std::filesystem;
using namespace internal;

namespace {

// -- small utilities ---------------------------------------------------

bool sendAll(SOCKET socket, const char* data, int length) {
    int sent = 0;
    while (sent < length) {
        const int n = ::send(socket, data + sent, length - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool sendAll(SOCKET socket, std::string_view data) {
    return sendAll(socket, data.data(), static_cast<int>(data.size()));
}

// SHA-256 of an in-memory buffer via the same Windows CNG provider
// gth::verification uses for files - kept local here since a cache key has
// no reason to round-trip through the filesystem-oriented public API there.
std::string sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return "";

    DWORD hashObjLen = 0;
    DWORD written = 0;
    auto hashObjLenBytes = std::as_writable_bytes(std::span{&hashObjLen, 1});
    ::BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(hashObjLenBytes.data()),
                        static_cast<ULONG>(hashObjLenBytes.size()), &written, 0);
    DWORD hashLen = 0;
    auto hashLenBytes = std::as_writable_bytes(std::span{&hashLen, 1});
    ::BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(hashLenBytes.data()),
                        static_cast<ULONG>(hashLenBytes.size()), &written, 0);

    std::vector<UCHAR> hashObj(hashObjLen);
    std::vector<UCHAR> hashVal(hashLen);
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    if (BCRYPT_SUCCESS(::BCryptCreateHash(alg, &hash, hashObj.data(), hashObjLen, nullptr, 0, 0))) {
        ::BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                          static_cast<ULONG>(data.size()), 0);
        if (BCRYPT_SUCCESS(::BCryptFinishHash(hash, hashVal.data(), hashLen, 0))) {
            result.reserve(hashVal.size() * 2);
            for (const auto b : hashVal) result += std::format("{:02x}", static_cast<unsigned int>(b));
        }
        ::BCryptDestroyHash(hash);
    }
    ::BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

// -- HTTP request/response parsing --------------------------------------
// HeaderLine/ParsedRequest/TargetUrl and the pure parsing functions over them
// (findHeader/parseRequestLine/parseHeaderLines/resolveTargetUrl/absoluteUrl/
// isHopByHop) live in WinsockCachingProxyServerInternal.h/.cpp now - brought
// in via `using namespace internal` above - so they're unit-testable without
// a socket or a window (see tests/unit/test_winsock_caching_proxy_logic.cpp).

// Reads from `socket` (accumulating into `buffer`, a per-connection scratch
// string the caller owns across calls) until a blank-line-terminated header
// block is available. Whatever comes after the blank line in the same recv
// is returned in `leftover` rather than discarded - it's the start of the
// request/response body.
bool readHeaderBlock(SOCKET socket, std::string& buffer, std::string& block, std::string& leftover) {
    auto pos = buffer.find("\r\n\r\n");
    while (pos == std::string::npos) {
        std::array<char, 4096> chunk{};
        const int received = ::recv(socket, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (received <= 0) return false;
        buffer.append(chunk.data(), static_cast<size_t>(received));
        if (buffer.size() > 65536) return false;  // runaway header block
        pos = buffer.find("\r\n\r\n");
    }
    block = buffer.substr(0, pos + 2);  // keep the single trailing CRLF that ends the last header
    leftover = buffer.substr(pos + 4);
    buffer.clear();
    return true;
}

// -- on-disk cache format -------------------------------------------------
// <cacheDir>/<key>.meta : "Field: value\r\n" lines (Url/ETag/LastModified/
// ContentType/Length). <cacheDir>/<key>.bin : exactly the bytes the origin
// returned for that Url. A cache entry is only ever written from bytes this
// proxy itself streamed from the real origin (see handleGet) and only ever
// served back after the origin explicitly confirms via 304 that nothing
// changed - see IDownloadAcceleratorServer's class comment.

struct CacheMeta {
    std::string url;
    std::string etag;
    std::string lastModified;
    std::string contentType;
    long long length = 0;
};

fs::path metaPath(const fs::path& dir, const std::string& key) { return dir / (key + ".meta"); }
fs::path binPath(const fs::path& dir, const std::string& key) { return dir / (key + ".bin"); }

bool loadCacheMeta(const fs::path& dir, const std::string& key, CacheMeta& out) {
    std::ifstream in(metaPath(dir, key));
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string field = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') value.erase(0, 1);
        if (field == "Url") out.url = value;
        else if (field == "ETag") out.etag = value;
        else if (field == "LastModified") out.lastModified = value;
        else if (field == "ContentType") out.contentType = value;
        else if (field == "Length") out.length = std::atoll(value.c_str());
    }
    return fs::exists(binPath(dir, key));
}

void saveCacheMeta(const fs::path& dir, const std::string& key, const CacheMeta& meta) {
    std::ofstream out(metaPath(dir, key), std::ios::trunc);
    out << "Url: " << meta.url << "\r\n";
    out << "ETag: " << meta.etag << "\r\n";
    out << "LastModified: " << meta.lastModified << "\r\n";
    out << "ContentType: " << meta.contentType << "\r\n";
    out << "Length: " << meta.length << "\r\n";
}

}  // namespace

// One control connection: reads exactly one HTTP request and answers it
// (CONNECT tunnel, or a resolved-and-optionally-cached forward), then the
// caller closes the socket - matches WinsockFtpServer's one-thread-per-
// connection model, just without FTP's multi-command control channel.
struct ProxySessionOptions {
    SOCKET clientSocket;
    std::string clientIp;
    int listenPort;
    WinsockCachingProxyServer::MirrorResolveFn resolveMirrorIp;
    fs::path cacheDir;
    bool cacheEnabled;
    std::atomic<std::uint64_t>& hitCount;
    std::atomic<std::uint64_t>& missCount;
    application::IConsoleManager& consoleManager;
    application::ICdnUrlService& cdnUrlService;
    std::shared_ptr<network::ILocalGameServer> localGameServer;
};

class ProxySession {
public:
    explicit ProxySession(ProxySessionOptions options)
        : client_(options.clientSocket),
          clientIp_(std::move(options.clientIp)),
          listenPort_(options.listenPort),
          resolveMirrorIp_(std::move(options.resolveMirrorIp)),
          cacheDir_(std::move(options.cacheDir)),
          cacheEnabled_(options.cacheEnabled),
          hitCount_(options.hitCount),
          missCount_(options.missCount),
          consoleManager_(options.consoleManager),
          cdnUrlService_(options.cdnUrlService),
          localGameServer_(std::move(options.localGameServer)) {}

    void run() {
        DWORD timeoutMs = 15000;
        const auto timeoutBytes = std::as_bytes(std::span{&timeoutMs, 1});
        ::setsockopt(client_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(timeoutBytes.data()),
                     static_cast<int>(timeoutBytes.size()));

        std::string buffer;
        std::string block;
        std::string leftover;
        if (!readHeaderBlock(client_, buffer, block, leftover)) return;

        std::istringstream lineStream(block);
        std::string requestLine;
        std::getline(lineStream, requestLine);
        if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

        ParsedRequest req;
        if (!parseRequestLine(requestLine, req)) return;
        parseHeaderLines(block, req.headers);
        req.leftoverBody = leftover;

        // Best-effort identifying data for the Consoles tab's connection
        // archive - some consoles/apps don't send a User-Agent at all, in
        // which case this is just "" and the Consoles tab shows the IP alone.
        const std::string userAgent = findHeader(req.headers, "user-agent").value_or("");
        try {
            consoleManager_.recordConnection(clientIp_, userAgent, listenPort_, core::ConsoleProtocol::Http);
        } catch (const std::exception&) {
            // Connection archiving is best-effort; it must not interrupt proxy handling.
        } catch (...) {
            // Preserve best-effort behavior for non-standard exceptions as well.
        }

        if (toLower(req.method) == "connect") {
            handleConnect(req);
        } else {
            handleForward(req, block);
        }
    }

private:
    SOCKET connectWithTimeout(const sockaddr_in& addr, int timeoutMs) {
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return INVALID_SOCKET;

        u_long nonBlocking = 1;
        ::ioctlsocket(s, FIONBIO, &nonBlocking);
        ::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(s, &writeSet);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        const bool connected = ::select(0, nullptr, &writeSet, nullptr, &tv) > 0;
        u_long blockingMode = 0;
        ::ioctlsocket(s, FIONBIO, &blockingMode);
        if (!connected) {
            ::closesocket(s);
            return INVALID_SOCKET;
        }
        return s;
    }

    bool resolveConnectAddress(const std::string& host, int port, sockaddr_in& out) {
        out = sockaddr_in{};
        out.sin_family = AF_INET;
        out.sin_port = ::htons(static_cast<u_short>(port));

        if (resolveMirrorIp_) {
            if (const auto mirrorIp = resolveMirrorIp_(host)) {
                if (::InetPtonA(AF_INET, mirrorIp->c_str(), &out.sin_addr) == 1) return true;
            }
        }

        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0 || !result) return false;
        out = *reinterpret_cast<sockaddr_in*>(result->ai_addr);
        ::freeaddrinfo(result);
        return true;
    }

    void sendStatusOnly(int code, const char* reason) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\nConnection: close\r\n\r\n", code, reason);
        sendAll(client_, buf, static_cast<int>(std::strlen(buf)));
    }

    void handleConnect(const ParsedRequest& req) {
        const auto colon = req.target.find(':');
        const std::string host = colon == std::string::npos ? req.target : req.target.substr(0, colon);
        const int port = colon == std::string::npos ? 443 : std::atoi(req.target.substr(colon + 1).c_str());

        // Only host:port is ever visible here - the real path is inside the
        // TLS tunnel this proxy never terminates - so this is as much detail
        // as the CDN URL Log can ever show for HTTPS traffic.
        try {
            cdnUrlService_.captureUrl("https://" + req.target, host, true);
        } catch (const std::exception&) {
        } catch (...) {
        }

        sockaddr_in addr{};
        if (host.empty() || !resolveConnectAddress(host, port, addr)) {
            sendStatusOnly(502, "Bad Gateway");
            return;
        }
        const SOCKET upstream = connectWithTimeout(addr, 8000);
        if (upstream == INVALID_SOCKET) {
            sendStatusOnly(502, "Bad Gateway");
            return;
        }

        if (!sendAll(client_, "HTTP/1.1 200 Connection Established\r\n\r\n")) {
            ::closesocket(upstream);
            return;
        }

        // Whatever the client already sent past its own header block (TLS
        // ClientHello, typically) needs relaying first - it was consumed
        // into req.leftoverBody by readHeaderBlock and would otherwise be
        // lost.
        if (!req.leftoverBody.empty()) sendAll(upstream, req.leftoverBody);

        relayBidirectional(client_, upstream);
        ::closesocket(upstream);
    }

    void relayBidirectional(SOCKET a, SOCKET b) {
        u_long nonBlocking = 1;
        ::ioctlsocket(a, FIONBIO, &nonBlocking);
        ::ioctlsocket(b, FIONBIO, &nonBlocking);

        char buf[16384];
        while (true) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(a, &readSet);
            FD_SET(b, &readSet);
            timeval tv{};
            tv.tv_sec = 30;
            if (::select(0, &readSet, nullptr, nullptr, &tv) <= 0) break;

            bool alive = true;
            if (FD_ISSET(a, &readSet)) {
                const int n = ::recv(a, buf, sizeof(buf), 0);
                if (n <= 0 || !sendAll(b, buf, n)) alive = false;
            }
            if (alive && FD_ISSET(b, &readSet)) {
                const int n = ::recv(b, buf, sizeof(buf), 0);
                if (n <= 0 || !sendAll(a, buf, n)) alive = false;
            }
            if (!alive) break;
        }
    }

    // Reads exactly `length` bytes of a request/response body, starting
    // with whatever's already sitting in `already` (bytes read past the
    // header block in the same recv), and hands each chunk to `sink` as it
    // arrives rather than buffering the whole body in memory.
    template <typename Sink>
    bool relayBody(SOCKET from, long long length, std::string already, Sink sink) {
        long long remaining = length - static_cast<long long>(already.size());
        if (!already.empty()) sink(already.data(), static_cast<int>(already.size()));
        char buf[16384];
        while (remaining > 0) {
            const int toRead = static_cast<int>(std::min<long long>(remaining, sizeof(buf)));
            const int n = ::recv(from, buf, toRead, 0);
            if (n <= 0) return false;
            sink(buf, n);
            remaining -= n;
        }
        return true;
    }

    void handleForward(const ParsedRequest& req, const std::string& rawHeaderBlock) {
        TargetUrl target;
        if (!resolveTargetUrl(req, target)) {
            sendStatusOnly(400, "Bad Request");
            return;
        }

        const bool isGet = toLower(req.method) == "get";
        const std::string url = absoluteUrl(target);

        try {
            cdnUrlService_.captureUrl(url, target.host, true);
        } catch (const std::exception&) {
        } catch (...) {
        }

        // Local-serve: a title recognized in the request path is streamed
        // straight from the game library, bypassing both the cache and the
        // real origin entirely - checked before ever dialing upstream. Only
        // reachable here, on a plain HTTP GET whose path this proxy can
        // actually see - never for the CONNECT/HTTPS path above.
        if (isGet && localGameServer_) {
            auto served =
                localGameServer_->tryServeGameFile(url, rawHeaderBlock, (void*)static_cast<intptr_t>(client_));
            if (served.has_value()) return;
        }

        sockaddr_in addr{};
        if (!resolveConnectAddress(target.host, target.port, addr)) {
            sendStatusOnly(502, "Bad Gateway");
            return;
        }
        const SOCKET upstream = connectWithTimeout(addr, 8000);
        if (upstream == INVALID_SOCKET) {
            sendStatusOnly(502, "Bad Gateway");
            return;
        }

        const std::string cacheKey = sha256Hex(url);

        CacheMeta cached;
        const bool haveCache =
            isGet && cacheEnabled_ && loadCacheMeta(cacheDir_, cacheKey, cached) && cached.url == url;

        // Rebuild the outgoing request: origin-form target (never
        // absolute-form upstream), Host set from the resolved target so a
        // mirror substitution still presents the real hostname to the
        // origin/CDN edge, hop-by-hop headers dropped, our own
        // conditional-revalidation headers added when we hold a cached copy
        // and the client didn't already ask its own conditional question.
        std::ostringstream out;
        out << req.method << " " << target.pathAndQuery << " HTTP/1.1\r\n";
        out << "Host: " << target.host << (target.port != 80 ? (":" + std::to_string(target.port)) : "") << "\r\n";
        bool clientSentConditional = false;
        for (const auto& h : req.headers) {
            const std::string lower = toLower(h.name);
            if (lower == "host" || isHopByHop(lower)) continue;
            if (lower == "if-none-match" || lower == "if-modified-since") clientSentConditional = true;
            out << h.name << ": " << h.value << "\r\n";
        }
        if (haveCache && !clientSentConditional) {
            if (!cached.etag.empty()) out << "If-None-Match: " << cached.etag << "\r\n";
            if (!cached.lastModified.empty()) out << "If-Modified-Since: " << cached.lastModified << "\r\n";
        }
        out << "Connection: close\r\n\r\n";

        if (!sendAll(upstream, out.str())) {
            ::closesocket(upstream);
            sendStatusOnly(502, "Bad Gateway");
            return;
        }

        const auto requestContentLength = findHeader(req.headers, "content-length");
        if (requestContentLength) {
            const long long len = std::atoll(requestContentLength->c_str());
            relayBody(client_, len, req.leftoverBody, [&](const char* data, int n) { sendAll(upstream, data, n); });
        }

        forwardResponse(upstream, isGet && cacheEnabled_, cacheKey, url, haveCache, cached);
        ::closesocket(upstream);
    }

    void forwardResponse(SOCKET upstream, bool cacheable, const std::string& cacheKey, const std::string& url,
                          bool haveCache, const CacheMeta& cached) {
        std::string buffer, block, leftover;
        if (!readHeaderBlock(upstream, buffer, block, leftover)) {
            sendStatusOnly(502, "Bad Gateway");
            return;
        }

        std::istringstream lineStream(block);
        std::string statusLine;
        std::getline(lineStream, statusLine);
        if (!statusLine.empty() && statusLine.back() == '\r') statusLine.pop_back();
        int statusCode = 0;
        {
            std::istringstream iss(statusLine);
            std::string version;
            iss >> version >> statusCode;
        }

        std::vector<HeaderLine> headers;
        parseHeaderLines(block, headers);

        if (statusCode == 304 && haveCache) {
            hitCount_.fetch_add(1);
            std::ifstream body(binPath(cacheDir_, cacheKey), std::ios::binary);
            std::ostringstream resp;
            resp << "HTTP/1.1 200 OK\r\n";
            if (!cached.contentType.empty()) resp << "Content-Type: " << cached.contentType << "\r\n";
            resp << "Content-Length: " << cached.length << "\r\n";
            resp << "X-Gth-Cache: HIT\r\n";
            resp << "Connection: close\r\n\r\n";
            if (!sendAll(client_, resp.str())) return;

            char buf[65536];
            while (body.read(buf, sizeof(buf)) || body.gcount() > 0) {
                if (!sendAll(client_, buf, static_cast<int>(body.gcount()))) return;
            }
            return;
        }

        if (cacheable) missCount_.fetch_add(1);

        const auto contentLengthHeader = findHeader(headers, "content-length");
        const auto transferEncoding = findHeader(headers, "transfer-encoding");
        const bool chunked = transferEncoding && toLower(*transferEncoding).find("chunked") != std::string::npos;
        const bool storeInCache = cacheable && statusCode == 200 && contentLengthHeader.has_value() && !chunked;

        // Relay the origin's status/header block to the client verbatim
        // (their own X-Gth-Cache-free response), just forcing Connection:
        // close since this session never keeps the client connection alive
        // past one request.
        std::ostringstream respHeaders;
        respHeaders << statusLine << "\r\n";
        for (const auto& h : headers) {
            if (toLower(h.name) == "connection") continue;
            respHeaders << h.name << ": " << h.value << "\r\n";
        }
        respHeaders << (storeInCache ? "X-Gth-Cache: MISS\r\n" : "") << "Connection: close\r\n\r\n";
        if (!sendAll(client_, respHeaders.str())) return;

        if (storeInCache) {
            const long long length = std::atoll(contentLengthHeader->c_str());
            const fs::path tempPath = binPath(cacheDir_, cacheKey + ".tmp");
            std::ofstream tempFile(tempPath, std::ios::binary | std::ios::trunc);
            bool ok = tempFile.is_open();
            if (ok) {
                ok = relayBody(upstream, length, leftover, [&](const char* data, int n) {
                    sendAll(client_, data, n);
                    tempFile.write(data, n);
                });
            }
            tempFile.close();
            if (ok) {
                std::error_code ec;
                fs::rename(tempPath, binPath(cacheDir_, cacheKey), ec);
                if (!ec) {
                    CacheMeta meta;
                    meta.url = url;
                    meta.etag = findHeader(headers, "etag").value_or("");
                    meta.lastModified = findHeader(headers, "last-modified").value_or("");
                    meta.contentType = findHeader(headers, "content-type").value_or("");
                    meta.length = length;
                    saveCacheMeta(cacheDir_, cacheKey, meta);
                }
            } else {
                std::error_code ec;
                fs::remove(tempPath, ec);
            }
            return;
        }

        if (contentLengthHeader) {
            const long long length = std::atoll(contentLengthHeader->c_str());
            relayBody(upstream, length, leftover, [&](const char* data, int n) { sendAll(client_, data, n); });
            return;
        }

        // No Content-Length (chunked or close-delimited): relay raw bytes
        // as they arrive, starting with whatever's already in `leftover`,
        // until the origin closes the connection.
        if (!leftover.empty()) sendAll(client_, leftover);
        char buf[16384];
        while (true) {
            const int n = ::recv(upstream, buf, sizeof(buf), 0);
            if (n <= 0) break;
            if (!sendAll(client_, buf, n)) break;
        }
    }

    SOCKET client_;
    std::string clientIp_;
    int listenPort_;
    WinsockCachingProxyServer::MirrorResolveFn resolveMirrorIp_;
    fs::path cacheDir_;
    bool cacheEnabled_;
    std::atomic<std::uint64_t>& hitCount_;
    std::atomic<std::uint64_t>& missCount_;
    application::IConsoleManager& consoleManager_;
    application::ICdnUrlService& cdnUrlService_;
    std::shared_ptr<network::ILocalGameServer> localGameServer_;
};

WinsockCachingProxyServer::WinsockCachingProxyServer(MirrorResolveFn resolveMirrorIp,
                                                       application::IConsoleManager& consoleManager,
                                                       application::ICdnUrlService& cdnUrlService,
                                                       std::shared_ptr<network::ILocalGameServer> localGameServer)
    : resolveMirrorIp_(std::move(resolveMirrorIp)),
      consoleManager_(consoleManager),
      cdnUrlService_(cdnUrlService),
      localGameServer_(std::move(localGameServer)) {}

WinsockCachingProxyServer::~WinsockCachingProxyServer() { stop(); }

void WinsockCachingProxyServer::start(const core::AcceleratorServerConfig& config) {
    if (running_.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lock(cacheDirMutex_);
        cacheDir_ = config.cacheDir;
    }
    std::error_code ec;
    if (!config.cacheDir.empty()) fs::create_directories(config.cacheDir, ec);
    acceptThread_ = std::thread(&WinsockCachingProxyServer::acceptLoop, this, config);
}

void WinsockCachingProxyServer::stop() {
    if (!running_.exchange(false)) return;
    if (acceptThread_.joinable()) acceptThread_.join();
    port_.store(0);
}

core::AcceleratorCacheStats WinsockCachingProxyServer::cacheStats() const {
    core::AcceleratorCacheStats stats;
    stats.hitCount = hitCount_.load();
    stats.missCount = missCount_.load();

    std::string dir;
    {
        std::lock_guard<std::mutex> lock(cacheDirMutex_);
        dir = cacheDir_;
    }
    if (dir.empty()) return stats;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.path().extension() != ".bin") continue;
        stats.entryCount++;
        stats.totalBytes += static_cast<std::uint64_t>(entry.file_size(ec));
    }
    return stats;
}

void WinsockCachingProxyServer::clearCache() {
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(cacheDirMutex_);
        dir = cacheDir_;
    }
    if (dir.empty()) return;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const auto ext = entry.path().extension();
        if (ext == ".bin" || ext == ".meta" || ext == ".tmp") fs::remove(entry.path(), ec);
    }
}

void WinsockCachingProxyServer::acceptLoop(core::AcceleratorServerConfig config) {
    WSADATA wsaData;
    if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        running_.store(false);
        return;
    }

    const SOCKET listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        ::WSACleanup();
        running_.store(false);
        return;
    }

    BOOL reuse = TRUE;
    ::setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = ::htons(static_cast<u_short>(config.port));

    if (::bind(listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listenSocket, SOMAXCONN) != 0) {
        ::closesocket(listenSocket);
        ::WSACleanup();
        running_.store(false);
        return;
    }

    sockaddr_in boundAddr{};
    int boundLen = sizeof(boundAddr);
    ::getsockname(listenSocket, reinterpret_cast<sockaddr*>(&boundAddr), &boundLen);
    port_.store(::ntohs(boundAddr.sin_port));

    u_long nonBlocking = 1;
    ::ioctlsocket(listenSocket, FIONBIO, &nonBlocking);

    std::vector<std::jthread> connectionThreads;
    const std::string cacheDir = config.cacheDir;
    const bool cacheEnabled = config.cacheEnabled;

    while (running_.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);
        timeval tv{};
        tv.tv_usec = 200000;

        if (::select(0, &readSet, nullptr, nullptr, &tv) <= 0) continue;

        sockaddr_in peerAddr{};
        int peerAddrLen = sizeof(peerAddr);
        const auto clientSocket =
            ::accept(listenSocket, reinterpret_cast<sockaddr*>(&peerAddr), &peerAddrLen);
        if (clientSocket == INVALID_SOCKET) continue;

        u_long blockingMode = 0;
        ::ioctlsocket(clientSocket, FIONBIO, &blockingMode);
        std::array<char, INET_ADDRSTRLEN> ipBuf{};
        std::string clientIp;
        if (::inet_ntop(AF_INET, &peerAddr.sin_addr, ipBuf.data(), ipBuf.size())) {
            clientIp = ipBuf.data();
        }
        connectionThreads.emplace_back(&WinsockCachingProxyServer::handleClient, this,
                                        static_cast<std::uintptr_t>(clientSocket), clientIp, cacheDir,
                                        cacheEnabled);
    }

    for (auto& t : connectionThreads) {
        if (t.joinable()) t.join();
    }

    ::closesocket(listenSocket);
    ::WSACleanup();
}

void WinsockCachingProxyServer::handleClient(std::uintptr_t clientSocketHandle, std::string clientIp,
                                             const std::string& cacheDir, bool cacheEnabled) {
    const SOCKET clientSocket = static_cast<SOCKET>(clientSocketHandle);
    ProxySession session({.clientSocket = clientSocket,
                          .clientIp = std::move(clientIp),
                          .listenPort = port_.load(),
                          .resolveMirrorIp = resolveMirrorIp_,
                          .cacheDir = fs::path(cacheDir),
                          .cacheEnabled = cacheEnabled,
                          .hitCount = hitCount_,
                          .missCount = missCount_,
                          .consoleManager = consoleManager_,
                          .cdnUrlService = cdnUrlService_,
                          .localGameServer = localGameServer_});
    session.run();
    ::closesocket(clientSocket);
}

}  // namespace gth::acceleratorserver
