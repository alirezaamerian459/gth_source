#include "gth/network/OnlineTitleLookupService.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>

#include <windows.h>
#include <winhttp.h>

#include "gth/logging/Logging.h"

#pragma comment(lib, "winhttp.lib")

namespace gth::network {

namespace {

// Thin RAII wrapper so the several early-return paths below can't leak a
// WinHTTP handle.
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

// Title IDs are alphanumeric (e.g. "PPSA20050"), so this only needs to
// exist for safety against anything unexpected reaching it, not to handle
// any real-world case.
std::string percentEncode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
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

std::string describeWinHttpError(DWORD code) {
    std::string meaning;
    switch (code) {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: meaning = "couldn't resolve the site's address - no internet or DNS blocked"; break;
        case ERROR_WINHTTP_CANNOT_CONNECT: meaning = "couldn't connect to the site - firewall or no internet"; break;
        case ERROR_WINHTTP_TIMEOUT: meaning = "the site didn't answer in time"; break;
        case ERROR_WINHTTP_CONNECTION_ERROR: meaning = "the connection was dropped"; break;
        case ERROR_WINHTTP_SECURE_FAILURE: meaning = "secure connection (TLS) failed - check the PC's date/time and certificates"; break;
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED: meaning = "the site asked for a client certificate"; break;
        default: meaning = "network error"; break;
    }
    return meaning + " (error " + std::to_string(code) + ")";
}

// logging::logger() is only valid after logging::init() (the app calls it at
// startup; tests and tools don't) - lookups must never crash just because
// nobody set a log file up.
#define GTH_LOG_IF_READY(level, ...)                                            \
    do {                                                                         \
        if (auto& gthLogger = ::gth::logging::logger()) gthLogger->level(__VA_ARGS__); \
    } while (0)

constexpr DWORD kTimeoutMs = 4000;
constexpr size_t kMaxResponseBytes = 1'000'000;

struct HttpOutcome {
    std::optional<std::string> body;
    std::string failure;  // set iff body is empty
    DWORD errorCode = 0;  // the WinHTTP error behind `failure`, or 0 when the site answered with a non-200 status
};

HttpOutcome failedWith(DWORD code) {
    HttpOutcome outcome;
    outcome.failure = describeWinHttpError(code);
    outcome.errorCode = code;
    return outcome;
}

// One GET on an already-open connection to the site.
HttpOutcome requestOn(HINTERNET connection, const std::wstring& path) {
    WinHttpHandle request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) return failedWith(GetLastError());

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        return failedWith(GetLastError());
    }
    if (!WinHttpReceiveResponse(request, nullptr)) return failedWith(GetLastError());

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                         &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 200) {
        HttpOutcome outcome;
        outcome.failure = "the site answered HTTP " + std::to_string(statusCode);
        if (statusCode == 403 || statusCode == 429) outcome.failure += " (blocking or rate-limiting requests)";
        return outcome;
    }

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
    HttpOutcome outcome;
    outcome.body = std::move(body);
    return outcome;
}

// A connection the server has quietly closed (idle keep-alive timeout) fails
// its next request in one of these ways; that says nothing about the site
// being unreachable, so it's worth exactly one retry on a fresh connection.
bool looksLikeAStaleConnection(DWORD errorCode) {
    return errorCode == ERROR_WINHTTP_CONNECTION_ERROR || errorCode == ERROR_WINHTTP_INVALID_SERVER_RESPONSE ||
           errorCode == 12031;  // ERROR_WINHTTP_CONNECTION_RESET
}

// After this many network-level failures in a row, stop trying for a while:
// with no internet (or a site blocking us) every remaining title would
// otherwise each burn the full timeout before failing the same way, turning
// a large library's scan into a very long wait for nothing.
constexpr int kConsecutiveFailuresBeforePause = 3;
constexpr std::chrono::seconds kPauseAfterRepeatedFailures{90};

}  // namespace

struct OnlineTitleLookupService::Connections {
    // Declared session-first so the connections are closed before it is.
    std::unique_ptr<WinHttpHandle> session;
    std::map<std::wstring, std::unique_ptr<WinHttpHandle>> byHost;

    HttpOutcome get(const std::wstring& host, const std::wstring& path) {
        HttpOutcome outcome;
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (!session) {
                session = std::make_unique<WinHttpHandle>(WinHttpOpen(L"GameTransferHub/5.9.1",
                                                                       WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                                                       WINHTTP_NO_PROXY_NAME,
                                                                       WINHTTP_NO_PROXY_BYPASS, 0));
                if (!*session) {
                    const DWORD code = GetLastError();
                    session.reset();
                    return failedWith(code);
                }
                WinHttpSetTimeouts(*session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
            }

            auto found = byHost.find(host);
            if (found == byHost.end()) {
                auto connection = std::make_unique<WinHttpHandle>(
                    WinHttpConnect(*session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
                if (!*connection) return failedWith(GetLastError());
                found = byHost.emplace(host, std::move(connection)).first;
            }

            outcome = requestOn(*found->second, path);
            if (outcome.body || attempt == 1 || !looksLikeAStaleConnection(outcome.errorCode)) return outcome;
            byHost.erase(host);  // drop the stale connection and try once more on a fresh one
        }
        return outcome;
    }
};

OnlineTitleLookupService::OnlineTitleLookupService(std::wstring ps5Host, std::wstring ps4Host)
    : ps5Host_(std::move(ps5Host)), ps4Host_(std::move(ps4Host)), connections_(std::make_unique<Connections>()) {}

OnlineTitleLookupService::~OnlineTitleLookupService() = default;

namespace {

void appendUtf8(std::string& out, uint32_t codePoint) {
    if (codePoint < 0x80) {
        out += static_cast<char>(codePoint);
    } else if (codePoint < 0x800) {
        out += static_cast<char>(0xC0 | (codePoint >> 6));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else if (codePoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codePoint >> 12));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codePoint >> 18));
        out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}

// Four hex digits at json[pos..pos+4); nullopt when they aren't.
std::optional<uint32_t> hex4(const std::string& json, size_t pos) {
    if (pos + 4 > json.size()) return std::nullopt;
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
        const char c = json[pos + i];
        value <<= 4;
        if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
        else return std::nullopt;
    }
    return value;
}

}  // namespace

std::optional<std::string> ParseTitleLookupResponse(const std::string& jsonBody, const std::string& queriedTitleId) {
    // Minimal "find the first key:"value"" extractor with JSON escape handling
    // (including \uXXXX, which is how the site writes a trademark sign or an
    // accented letter in a name) - good enough for this endpoint's known flat
    // shape, not a general JSON parser. Scoped to the first "results" entry only.
    auto extractField = [](const std::string& json, const std::string& key) -> std::optional<std::string> {
        const std::string needle = "\"" + key + "\":\"";
        const size_t start = json.find(needle);
        if (start == std::string::npos) return std::nullopt;
        size_t pos = start + needle.size();
        std::string value;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] != '\\' || pos + 1 >= json.size()) {
                value += json[pos];
                ++pos;
                continue;
            }

            const char escaped = json[pos + 1];
            pos += 2;
            switch (escaped) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case 'b': value += '\b'; break;
                case 'f': value += '\f'; break;
                case 'u': {
                    auto unit = hex4(json, pos);
                    if (!unit) break;  // not a valid escape: drop it rather than invent a character
                    pos += 4;
                    uint32_t codePoint = *unit;
                    // A character outside the basic plane arrives as two \u escapes (a surrogate pair).
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF && pos + 6 <= json.size() && json[pos] == '\\' &&
                        json[pos + 1] == 'u') {
                        if (auto low = hex4(json, pos + 2); low && *low >= 0xDC00 && *low <= 0xDFFF) {
                            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (*low - 0xDC00);
                            pos += 6;
                        }
                    }
                    if (codePoint >= 0xD800 && codePoint <= 0xDFFF) break;  // a lone surrogate isn't a character
                    appendUtf8(value, codePoint);
                    break;
                }
                default: value += escaped; break;  // \" \\ \/ - the character itself
            }
        }
        return value;
    };

    if (jsonBody.find("\"success\":true") == std::string::npos) return std::nullopt;

    auto returnedId = extractField(jsonBody, "titleid");
    if (!returnedId || returnedId->empty()) return std::nullopt;

    auto toUpper = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
        return s;
    };
    if (toUpper(*returnedId) != toUpper(queriedTitleId)) return std::nullopt;

    auto name = extractField(jsonBody, "name");
    if (!name || name->empty()) return std::nullopt;
    return name;
}

std::string OnlineTitleLookupService::lastFailureReason() const { return lastFailureReason_; }

std::optional<std::string> OnlineTitleLookupService::lookupTitle(const std::string& platform,
                                                                   const std::string& titleId) {
    lastFailureReason_.clear();
    if (titleId.empty()) return std::nullopt;

    std::wstring host;
    if (platform == "PS5") {
        host = ps5Host_;
    } else if (platform == "PS4") {
        host = ps4Host_;
    } else {
        return std::nullopt;  // no community tracker site for PS3 or anything else
    }

    if (std::chrono::steady_clock::now() < pausedUntil_) {
        lastFailureReason_ = "skipped after repeated failures: " + pauseReason_;
        return std::nullopt;
    }

    const std::wstring path = L"/api/internal/search?term=" + toWide(percentEncode(titleId));
    auto outcome = connections_->get(host, path);
    if (!outcome.body) {
        lastFailureReason_ = outcome.failure;
        GTH_LOG_IF_READY(warn, "Online title lookup for {} failed: {}", titleId, outcome.failure);
        if (++consecutiveFailures_ >= kConsecutiveFailuresBeforePause) {
            pausedUntil_ = std::chrono::steady_clock::now() + kPauseAfterRepeatedFailures;
            pauseReason_ = outcome.failure;
            consecutiveFailures_ = 0;
            GTH_LOG_IF_READY(warn, "Pausing online title lookups for {}s after {} failures in a row",
                         kPauseAfterRepeatedFailures.count(), kConsecutiveFailuresBeforePause);
        }
        return std::nullopt;
    }
    consecutiveFailures_ = 0;

    auto title = ParseTitleLookupResponse(*outcome.body, titleId);
    if (title) {
        GTH_LOG_IF_READY(info, "Online title lookup: {} -> {}", titleId, *title);
    } else {
        GTH_LOG_IF_READY(info, "Online title lookup: {} not found on the tracker site", titleId);
    }
    return title;
}

}  // namespace gth::network
