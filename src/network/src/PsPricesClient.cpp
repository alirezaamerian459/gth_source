#include "gth/network/PsPricesClient.h"

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <map>
#include <memory>

#pragma comment(lib, "winhttp.lib")

namespace gth::network {

namespace {

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
    std::wstring path;
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
constexpr size_t kMaxResponseBytes = 8 * 1024 * 1024;

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

        if (body.size() + available > kMaxResponseBytes) return {};
        std::string chunk(available, '\0');
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &bytesRead)) return {};
        chunk.resize(bytesRead);
        body += chunk;
    }
    return {std::move(body)};
}

}  // namespace

std::optional<std::string> ExtractFirstPsPricesCoverUrl(const std::string& htmlBody) {
    // A result card is an <a href="/region-us/game/..."> containing the same official cover URL
    // twice: once for the blurred background and once for the visible poster. Restricting the
    // image search to that anchor avoids accidentally selecting a platform logo or a page-wide ad.
    static const std::string gameMarker = "/region-us/game/";
    static const std::string imageMarker = "https://image.api.playstation.com/";

    size_t cursor = 0;
    while ((cursor = htmlBody.find(gameMarker, cursor)) != std::string::npos) {
        const auto anchorStart = htmlBody.rfind("<a", cursor);
        const auto anchorEnd = htmlBody.find("</a>", cursor);
        if (anchorStart == std::string::npos || anchorEnd == std::string::npos || anchorStart > cursor ||
            anchorEnd <= cursor) {
            cursor += gameMarker.size();
            continue;
        }

        const auto imageStart = htmlBody.find(imageMarker, cursor);
        if (imageStart != std::string::npos && imageStart < anchorEnd) {
            const auto imageEnd = htmlBody.find_first_of("\"'", imageStart);
            if (imageEnd != std::string::npos && imageEnd > imageStart) {
                return htmlBody.substr(imageStart, imageEnd - imageStart);
            }
        }
        cursor = anchorEnd + 4;
    }
    return std::nullopt;
}

struct PsPricesClient::Connections {
    std::unique_ptr<WinHttpHandle> session;
    std::map<std::wstring, std::unique_ptr<WinHttpHandle>> byHost;

    HttpOutcome get(const std::wstring& host, const std::wstring& path) {
        if (!session) {
            session = std::make_unique<WinHttpHandle>(WinHttpOpen(L"GameTransferHub/6.9.0",
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
            auto connection = std::make_unique<WinHttpHandle>(
                WinHttpConnect(*session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
            if (!*connection) return {};
            found = byHost.emplace(host, std::move(connection)).first;
        }
        return requestOn(*found->second, path);
    }
};

PsPricesClient::PsPricesClient() : connections_(std::make_unique<Connections>()) {}

PsPricesClient::~PsPricesClient() = default;

std::optional<std::string> PsPricesClient::fetchPosterByTitle(const std::string& gameTitle) {
    if (gameTitle.empty()) return std::nullopt;

    static const std::wstring kHost = L"psprices.com";
    const std::wstring searchPath = L"/region-us/games/?q=" + toWide(percentEncodeUtf8(gameTitle));
    const auto searchOutcome = connections_->get(kHost, searchPath);
    if (!searchOutcome.body.has_value()) return std::nullopt;

    const auto imageUrl = ExtractFirstPsPricesCoverUrl(*searchOutcome.body);
    if (!imageUrl.has_value()) return std::nullopt;

    const auto parsedUrl = crackUrl(toWide(*imageUrl));
    if (!parsedUrl.has_value() || !parsedUrl->https || parsedUrl->host != L"image.api.playstation.com")
        return std::nullopt;

    const auto imageOutcome = connections_->get(parsedUrl->host, parsedUrl->path);
    if (!imageOutcome.body.has_value() || imageOutcome.body->empty()) return std::nullopt;
    return imageOutcome.body;
}

}  // namespace gth::network
