#include "gth/network/TheGamesDbClient.h"

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <map>
#include <memory>

#pragma comment(lib, "winhttp.lib")

namespace gth::network {

namespace {

// Thin RAII wrapper so the several early-return paths below can't leak a WinHTTP handle - the
// same shape OnlineTitleLookupService.cpp's own copy already uses.
class WinHttpHandle {
public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : handle_(handle) {}
    ~WinHttpHandle() {
        if (handle_) WinHttpCloseHandle(handle_);
    }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    operator HINTERNET() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_ = nullptr;
};

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

// Percent-encodes raw UTF-8 bytes for use in a URL query value - a game title can carry spaces,
// apostrophes, or non-ASCII letters, none of which the search page's "name" parameter can take
// as-is.
std::string percentEncodeUtf8(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

struct ParsedUrl {
    bool https = false;
    std::wstring host;
    INTERNET_PORT port = 0;
    std::wstring path;  // including any query
};

std::optional<ParsedUrl> crackUrl(const std::wstring& wide) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) return std::nullopt;

    ParsedUrl parsed;
    parsed.https = parts.nScheme == INTERNET_SCHEME_HTTPS;
    parsed.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    parsed.port = parts.nPort;
    if (parts.lpszUrlPath && parts.dwUrlPathLength) parsed.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength) parsed.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (parsed.path.empty()) parsed.path = L"/";
    return parsed;
}

constexpr DWORD kTimeoutMs = 6000;
constexpr size_t kMaxResponseBytes = 8 * 1024 * 1024;  // a search page or a cover image, never more

struct HttpOutcome {
    std::optional<std::string> body;
};

HttpOutcome requestOn(HINTERNET connection, const std::wstring& path) {
    WinHttpHandle request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) return {};

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return {};
    if (!WinHttpReceiveResponse(request, nullptr)) return {};

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                         &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 200) return {};

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;

        std::string chunk(available, '\0');
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &bytesRead)) break;
        chunk.resize(bytesRead);
        body += chunk;
        if (body.size() > kMaxResponseBytes) break;
    }
    return {std::move(body)};
}

}  // namespace

std::optional<std::string> ExtractFirstBoxArtUrl(const std::string& htmlBody) {
    // The search page lists each match as a card whose cover, when the game has one at all, is
    // this exact CDN path - distinctive enough that a plain substring search is safe (verified
    // live against the real page; see thegamesdb.net/search.php).
    static const std::string needle = "https://cdn.thegamesdb.net/images/thumb/boxart/front/";
    const auto pos = htmlBody.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    const auto end = htmlBody.find_first_of("\"'", pos);
    if (end == std::string::npos) return std::nullopt;
    return htmlBody.substr(pos, end - pos);
}

struct TheGamesDbClient::Connections {
    std::unique_ptr<WinHttpHandle> session;
    std::map<std::wstring, std::unique_ptr<WinHttpHandle>> byHost;

    HttpOutcome get(const std::wstring& host, const std::wstring& path) {
        if (!session) {
            session = std::make_unique<WinHttpHandle>(WinHttpOpen(L"GameTransferHub/5.9.1",
                                                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!*session) {
                session.reset();
                return {};
            }
            WinHttpSetTimeouts(*session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
        }

        auto found = byHost.find(host);
        if (found == byHost.end()) {
            auto connection =
                std::make_unique<WinHttpHandle>(WinHttpConnect(*session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
            if (!*connection) return {};
            found = byHost.emplace(host, std::move(connection)).first;
        }
        return requestOn(*found->second, path);
    }
};

TheGamesDbClient::TheGamesDbClient() : connections_(std::make_unique<Connections>()) {}

TheGamesDbClient::~TheGamesDbClient() = default;

std::optional<std::string> TheGamesDbClient::fetchPosterByTitle(const std::string& gameTitle) {
    if (gameTitle.empty()) return std::nullopt;

    static const std::wstring kHost = L"thegamesdb.net";
    const std::wstring searchPath = L"/search.php?name=" + toWide(percentEncodeUtf8(gameTitle));
    const auto searchOutcome = connections_->get(kHost, searchPath);
    if (!searchOutcome.body) return std::nullopt;

    const auto imageUrl = ExtractFirstBoxArtUrl(*searchOutcome.body);
    if (!imageUrl) return std::nullopt;

    const auto parsedUrl = crackUrl(toWide(*imageUrl));
    if (!parsedUrl || !parsedUrl->https) return std::nullopt;

    const auto imageOutcome = connections_->get(parsedUrl->host, parsedUrl->path);
    if (!imageOutcome.body || imageOutcome.body->empty()) return std::nullopt;
    return imageOutcome.body;
}

}  // namespace gth::network
