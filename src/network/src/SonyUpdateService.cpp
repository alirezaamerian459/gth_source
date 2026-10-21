#include "gth/network/SonyUpdateService.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <thread>


#include "SonyHttp.h"
#include "gth/core/index/VersionComparer.h"
#include "gth/network/SonyLinkImport.h"
#include "gth/logging/Logging.h"


namespace gth::network {

namespace {

#define GTH_LOG_IF_READY(level, ...)                                            \
    do {                                                                         \
        if (auto& gthLogger = ::gth::logging::logger()) gthLogger->level(__VA_ARGS__); \
    } while (0)

// SHA-256 of Sony's "SCEI DNAS Root 05" certificate, the root of the chain its
// update hosts (sgst.prod.dl.playstation.net) present. Windows doesn't ship
// that root, and the leaf certificates rotate between servers, so the root is
// what gets pinned.
constexpr const char* kSonyRootSha256 = "51d5a7833f67f1d1c6212f75997e4b83dada0f7910ba4b5a3fa4e9b7b9bc827c";

// The key the PS4 itself uses to derive a title's update-file address; it has
// been public in community tools for years and only obscures the URL - the
// file behind it is served to anyone.
constexpr const char* kPs4UpdateKeyHex = "AD62E37F905E06BC19593142281C112CEC0E7EC3E97EFDCAEFCDBAAFA6378D84";

constexpr DWORD kTimeoutMs = 8000;
constexpr size_t kMaxResponseBytes = 4'000'000;
constexpr int kConsecutiveFailuresBeforePause = 3;
constexpr std::chrono::seconds kPauseAfterRepeatedFailures{60};

using namespace sonyhttp;

// One WinHTTP session with a kept-alive connection per host: checking a
// library means two requests per game against the same server, and setting up
// TLS afresh for each one is what would make that slow.
class WinHttpFetcher {
public:
    // `maxBytes` > 0: ask for only that many leading bytes (and read no more than that even if the server
    // ignores the Range header and starts sending the whole file).
    SonyHttpResponse get(const std::string& url, size_t maxBytes = 0) {
        const auto parsed = crackUrl(url);
        if (!parsed) return failure("not a valid address: " + url);

        SonyHttpResponse response;
        for (int attempt = 0; attempt < 2; ++attempt) {
            response = getOnce(*parsed, maxBytes);
            const bool staleConnection = response.status == 0 && (lastError_ == ERROR_WINHTTP_CONNECTION_ERROR ||
                                                                   lastError_ == ERROR_WINHTTP_INVALID_SERVER_RESPONSE ||
                                                                   lastError_ == 12031);
            if (!staleConnection) break;
            connections_.erase(key(*parsed));  // the server closed an idle connection: retry on a fresh one
        }
        return response;
    }

private:
    static std::wstring key(const ParsedUrl& url) {
        return url.host + L":" + std::to_wstring(url.port) + (url.https ? L"s" : L"");
    }

    SonyHttpResponse failure(const std::string& text) {
        SonyHttpResponse response;
        response.failure = text;
        return response;
    }

    SonyHttpResponse failedWith(DWORD code) {
        lastError_ = code;
        return failure(describeWinHttpError(code));
    }

    SonyHttpResponse getOnce(const ParsedUrl& url, size_t maxBytes) {
        lastError_ = 0;
        if (!session_) {
            session_ = std::make_unique<WinHttpHandle>(WinHttpOpen(L"GameTransferHub/5.9.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!*session_) {
                const DWORD code = GetLastError();
                session_.reset();
                return failedWith(code);
            }
            WinHttpSetTimeouts(*session_, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
        }

        auto found = connections_.find(key(url));
        if (found == connections_.end()) {
            auto connection = std::make_unique<WinHttpHandle>(WinHttpConnect(*session_, url.host.c_str(), url.port, 0));
            if (!*connection) return failedWith(GetLastError());
            found = connections_.emplace(key(url), std::move(connection)).first;
        }

        WinHttpHandle request(WinHttpOpenRequest(*found->second, L"GET", url.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                  WINHTTP_DEFAULT_ACCEPT_TYPES, url.https ? WINHTTP_FLAG_SECURE : 0));
        if (!request) return failedWith(GetLastError());

        // Sony's update hosts present a chain that ends in a root Windows
        // doesn't know. Let the handshake through for those hosts only, then
        // insist below that the chain really is Sony's pinned root.
        const bool sonyHost = url.https && isSonyHost(url.host);
        if (sonyHost) {
            DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA;
            WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
        }

        std::wstring rangeHeader;
        if (maxBytes > 0) rangeHeader = L"Range: bytes=0-" + std::to_wstring(maxBytes - 1);
        if (!WinHttpSendRequest(request, rangeHeader.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : rangeHeader.c_str(),
                                rangeHeader.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            return failedWith(GetLastError());
        }
        if (sonyHost) {
            std::string why;
            if (!serverChainIsSonys(request, why)) return failure("refused the connection: " + why);
        }
        if (!WinHttpReceiveResponse(request, nullptr)) return failedWith(GetLastError());

        SonyHttpResponse response;
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        response.status = static_cast<int>(status);

        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
            response.body.append(chunk.data(), read);
            if (maxBytes > 0 && response.body.size() >= maxBytes) {
                response.body.resize(maxBytes);
                break;
            }
            if (response.body.size() > kMaxResponseBytes) break;
        }
        return response;
    }

    // Declared session-first so connections are closed before it is.
    std::unique_ptr<WinHttpHandle> session_;
    std::map<std::wstring, std::unique_ptr<WinHttpHandle>> connections_;
    DWORD lastError_ = 0;
};

std::string decodeXmlEntities(std::string text) {
    static const std::pair<const char*, const char*> entities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
    for (const auto& [entity, replacement] : entities) {
        for (size_t at = text.find(entity); at != std::string::npos; at = text.find(entity, at + 1)) {
            text.replace(at, std::char_traits<char>::length(entity), replacement);
        }
    }
    return text;
}

// One attribute's value from an XML start tag's text, matched only as a whole
// name (so "version" never matches inside "content_ver" or "metadata_ver").
std::optional<std::string> xmlAttribute(const std::string& tag, const std::string& name) {
    const std::string needle = " " + name + "=\"";
    const size_t start = tag.find(needle);
    if (start == std::string::npos) return std::nullopt;
    const size_t valueStart = start + needle.size();
    const size_t valueEnd = tag.find('"', valueStart);
    if (valueEnd == std::string::npos) return std::nullopt;
    return decodeXmlEntities(tag.substr(valueStart, valueEnd - valueStart));
}

long long toNumber(const std::string& digits) {
    if (digits.empty() || digits.size() > 18) return 0;
    for (char c : digits) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
    }
    return std::stoll(digits);
}

// Every <package ...> start tag in the document, as its raw text.
std::vector<std::string> packageTags(const std::string& xml) {
    std::vector<std::string> tags;
    size_t at = 0;
    while ((at = xml.find("<package", at)) != std::string::npos) {
        const size_t afterName = at + 8;
        const size_t end = xml.find('>', at);
        if (end == std::string::npos) break;
        if (afterName < xml.size() && std::isspace(static_cast<unsigned char>(xml[afterName]))) {
            tags.push_back(xml.substr(at, end - at));
        }
        at = end;
    }
    return tags;
}

std::optional<SonyUpdateInfo> newestPackage(const std::string& xml, const std::string& versionAttribute) {
    std::optional<SonyUpdateInfo> newest;
    for (const auto& tag : packageTags(xml)) {
        const auto version = xmlAttribute(tag, versionAttribute);
        if (!version || version->empty()) continue;

        SonyUpdateInfo info;
        info.version = *version;
        if (const auto size = xmlAttribute(tag, "size")) info.sizeBytes = toNumber(*size);
        if (const auto manifest = xmlAttribute(tag, "manifest_url")) info.manifestUrl = *manifest;
        if (const auto url = xmlAttribute(tag, "url")) info.url = *url;
        if (const auto digest = xmlAttribute(tag, "digest")) info.digest = *digest;
        if (!newest || core::compareVersions(info.version, newest->version) > 0) newest = std::move(info);
    }
    return newest;
}

// The title ID at the start of a run of letters+digits ("PPSA01280"), found
// anywhere in `text` as long as it isn't part of a longer identifier.
std::string findTitleId(const std::string& text) {
    for (size_t i = 0; i + 9 <= text.size(); ++i) {
        if (i > 0 && std::isalnum(static_cast<unsigned char>(text[i - 1]))) continue;
        bool shape = true;
        for (size_t k = 0; k < 9 && shape; ++k) {
            const auto c = static_cast<unsigned char>(text[i + k]);
            shape = k < 4 ? std::isalpha(c) != 0 : std::isdigit(c) != 0;
        }
        if (!shape) continue;
        if (i + 9 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 9]))) continue;
        return toUpper(text.substr(i, 9));
    }
    return {};
}

}  // namespace

bool ShouldRetrySonyResponse(const SonyHttpResponse& response) {
    // No answer at all (a dropped connection, a timeout), a busy server, or a server error can pass by itself;
    // any other answer - 403, 404, ... - is Sony's actual reply and asking again changes nothing.
    return response.status == 0 || response.status == 429 || response.status >= 500;
}

SonyFetcher WithRetries(SonyFetcher inner, SonyRetryPolicy policy, std::function<void(std::chrono::milliseconds)> sleep) {
    if (!sleep) sleep = [](std::chrono::milliseconds pause) { std::this_thread::sleep_for(pause); };
    return [inner = std::move(inner), policy, sleep = std::move(sleep)](const std::string& url) {
        SonyHttpResponse response;
        auto pause = policy.firstPause;
        const int attempts = policy.attempts < 1 ? 1 : policy.attempts;
        for (int attempt = 1; attempt <= attempts; ++attempt) {
            response = inner(url);
            if (!ShouldRetrySonyResponse(response) || attempt >= attempts) break;
            sleep(pause);
            pause *= 2;
        }
        return response;
    };
}

SonyFetcher MakeSonyFetcher() {
    auto fetcher = std::make_shared<WinHttpFetcher>();
    // A library check is a long run of small requests: one dropped packet mustn't turn a game into "check failed".
    return WithRetries([fetcher](const std::string& url) { return fetcher->get(url); });
}

SonyHeadFetcher MakeSonyHeadFetcher() {
    auto fetcher = std::make_shared<WinHttpFetcher>();
    return [fetcher](const std::string& url, size_t maxBytes) { return fetcher->get(url, maxBytes); };
}

bool IsPinnedSonyChain(unsigned long trustErrorStatus, const std::string& rootSha256Hex) {
    if (trustErrorStatus == 0) return true;
    return trustErrorStatus == CERT_TRUST_IS_UNTRUSTED_ROOT && rootSha256Hex == kSonyRootSha256;
}

std::string SonyPs4UpdateUrl(const std::string& titleId) {
    unsigned char key[32];
    for (size_t i = 0; i < sizeof(key); ++i) key[i] = static_cast<unsigned char>(std::stoi(std::string(kPs4UpdateKeyHex + 2 * i, 2), nullptr, 16));

    const std::string id = toUpper(titleId);
    const std::string message = "np_" + id;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    std::string digest;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        UCHAR mac[32]{};
        if (BCryptHash(algorithm, key, sizeof(key), reinterpret_cast<PUCHAR>(const_cast<char*>(message.data())),
                       static_cast<ULONG>(message.size()), mac, sizeof(mac)) == 0) {
            static const char* digits = "0123456789abcdef";
            for (UCHAR byte : mac) {
                digest += digits[byte >> 4];
                digest += digits[byte & 0xF];
            }
        }
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return "http://gs-sec.ww.np.dl.playstation.net/plo/np/" + id + "/" + digest + "/" + id + "-ver.xml";
}

std::optional<SonyUpdateInfo> ParseSonyPs4UpdateXml(const std::string& xml) { return newestPackage(xml, "version"); }

std::optional<SonyUpdateInfo> ParseSonyPs5VersionXml(const std::string& xml) { return newestPackage(xml, "content_ver"); }

std::optional<SonyPackageManifest> ParseSonyPackageManifest(const std::string& json) {
    const auto numberAfter = [&](const std::string& key, size_t from, size_t& endOut) -> std::optional<long long> {
        const std::string needle = "\"" + key + "\"";
        const size_t at = json.find(needle, from);
        if (at == std::string::npos) return std::nullopt;
        size_t pos = at + needle.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n' || json[pos] == ':')) ++pos;
        size_t digitsEnd = pos;
        while (digitsEnd < json.size() && std::isdigit(static_cast<unsigned char>(json[digitsEnd]))) ++digitsEnd;
        endOut = digitsEnd;
        if (digitsEnd == pos) return std::nullopt;
        return toNumber(json.substr(pos, digitsEnd - pos));
    };

    SonyPackageManifest manifest;
    size_t end = 0;
    if (const auto total = numberAfter("originalFileSize", 0, end)) manifest.originalFileSize = *total;

    // Each piece is one flat {...} object inside "pieces"; read its three
    // fields from that object's own text so they can't be mixed up between
    // neighbours.
    const size_t piecesAt = json.find("\"pieces\"");
    size_t cursor = piecesAt == std::string::npos ? std::string::npos : json.find('[', piecesAt);
    while (cursor != std::string::npos) {
        const size_t open = json.find('{', cursor);
        const size_t arrayEnd = json.find(']', cursor);
        if (open == std::string::npos || (arrayEnd != std::string::npos && arrayEnd < open)) break;
        const size_t close = json.find('}', open);
        if (close == std::string::npos) break;
        const std::string object = json.substr(open, close - open + 1);
        cursor = close;

        const auto stringField = [&](const std::string& key) -> std::string {
            const std::string needle = "\"" + key + "\"";
            size_t pos = object.find(needle);
            if (pos == std::string::npos) return {};
            pos = object.find('"', pos + needle.size());
            if (pos == std::string::npos) return {};
            std::string value;
            for (++pos; pos < object.size() && object[pos] != '"'; ++pos) {
                if (object[pos] == '\\' && pos + 1 < object.size()) ++pos;  // \/ and \" and \\ all stand for the next character
                value += object[pos];
            }
            return value;
        };

        core::ReleasePiece piece;
        piece.url = stringField("url");
        piece.sha256 = stringField("hashValue");
        std::transform(piece.sha256.begin(), piece.sha256.end(), piece.sha256.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto numberField = [&](const char* key) -> std::optional<long long> {
            const std::string needle = std::string("\"") + key + "\"";
            const size_t at = object.find(needle);
            if (at == std::string::npos) return std::nullopt;
            size_t pos = at + needle.size();
            while (pos < object.size() && (object[pos] == ' ' || object[pos] == ':' || object[pos] == '\t')) ++pos;
            size_t digitsEnd = pos;
            while (digitsEnd < object.size() && std::isdigit(static_cast<unsigned char>(object[digitsEnd]))) ++digitsEnd;
            if (digitsEnd == pos) return std::nullopt;
            return toNumber(object.substr(pos, digitsEnd - pos));
        };
        piece.sizeBytes = numberField("fileSize").value_or(0);
        piece.offsetBytes = numberField("fileOffset").value_or(-1);
        manifest.pieceSizes.push_back(piece.sizeBytes);
        manifest.pieces.push_back(std::move(piece));
    }
    if (manifest.pieceSizes.empty() && manifest.originalFileSize == 0) return std::nullopt;
    return manifest;
}

std::map<std::string, std::string> ParseSonyVersionLinks(const std::string& text) {
    std::map<std::string, std::string> links;
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        const std::string line = text.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        if (line.empty() || line[0] == '#') continue;
        size_t urlStart = line.find("https://");
        if (urlStart == std::string::npos) urlStart = line.find("http://");
        if (urlStart == std::string::npos) continue;
        size_t urlEnd = urlStart;
        while (urlEnd < line.size() && !std::isspace(static_cast<unsigned char>(line[urlEnd])) && line[urlEnd] != '"' &&
               line[urlEnd] != ',' && line[urlEnd] != ';') {
            ++urlEnd;
        }
        const std::string url = line.substr(urlStart, urlEnd - urlStart);

        std::string titleId = findTitleId(url);
        if (titleId.empty()) titleId = findTitleId(line.substr(0, urlStart));
        if (!titleId.empty()) links[titleId] = url;
    }
    return links;
}

SonyLinksMergeResult MergeSonyVersionLinks(const std::filesystem::path& file, const std::string& incomingText) {
    SonyLinksMergeResult result;
    const auto incoming = ParseSonyVersionLinks(incomingText);
    result.incoming = incoming.size();

    std::map<std::string, std::string> all;
    std::error_code ec;
    if (std::filesystem::is_regular_file(file, ec)) {
        std::ifstream in(file, std::ios::binary);
        all = ParseSonyVersionLinks(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    }
    for (const auto& [titleId, url] : incoming) all[titleId] = url;
    result.total = all.size();
    if (incoming.empty()) return result;  // nothing to add: leave the file exactly as it is

    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "# Sony version links: <title ID> <tab> <version file link>. Lines can be added by hand or by a download tool.\n";
    for (const auto& [titleId, url] : all) out << titleId << '\t' << url << '\n';
    out.flush();
    result.written = static_cast<bool>(out);
    return result;
}

std::set<std::string> ReadSonyLinkTitleIds(const std::vector<std::filesystem::path>& files) {
    std::set<std::string> ids;
    for (const auto& file : files) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) continue;
        std::ifstream in(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (const auto& [titleId, url] : ParseSonyVersionLinks(text)) ids.insert(titleId);
    }
    return ids;
}

std::string BuildSonyLinksNeededList(const std::vector<std::pair<std::string, std::string>>& titleIdsAndNames) {
    std::string text =
        "# Games that still need a Sony version link.\n"
        "# Put the game's version-file link at the END of its line (after the second tab), for example:\n"
        "#   PPSA02343<TAB>It Takes Two<TAB>https://sgst.prod.dl.playstation.net/sgst/prod/00/np/PPSA02343_00/<id>-version.xml\n"
        "# The link is 'versionFileUri' in the game's own param.json. Lines without a link are ignored; save the file\n"
        "# and press Check Sony for Updates - no restart needed.\n";
    for (const auto& [titleId, name] : titleIdsAndNames) text += titleId + "\t" + name + "\t\n";
    return text;
}

SonyUpdateService::SonyUpdateService(std::vector<std::filesystem::path> linkFiles, SonyFetcher fetcher)
    : linkFiles_(std::move(linkFiles)), fetcher_(std::move(fetcher)) {}

void SonyUpdateService::reloadLinksIfChanged() {
    std::vector<FileStamp> now;
    for (const auto& path : linkFiles_) {
        FileStamp stamp;
        std::error_code ec;
        stamp.exists = std::filesystem::is_regular_file(path, ec);
        if (stamp.exists) {
            stamp.modified = std::filesystem::last_write_time(path, ec);
            stamp.size = std::filesystem::file_size(path, ec);
        }
        now.push_back(stamp);
    }
    if (linksLoadedOnce_ && now == stamps_) return;

    std::map<std::string, std::string> links;
    for (size_t i = 0; i < linkFiles_.size(); ++i) {
        if (!now[i].exists) continue;
        std::ifstream in(linkFiles_[i], std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (auto& [titleId, url] : ParseSonyVersionLinks(text)) links[titleId] = std::move(url);
    }
    ps5Links_ = std::move(links);
    stamps_ = std::move(now);
    linksLoadedOnce_ = true;
}

size_t SonyUpdateService::knownPs5LinkCount() {
    reloadLinksIfChanged();
    return ps5Links_.size();
}

core::LatestReleaseResult SonyUpdateService::latestRelease(const std::string& platform, const std::string& titleId) {
    core::LatestReleaseResult result;
    if (titleId.empty() || (platform != "PS4" && platform != "PS5")) {
        result.detail = "Sony's servers only cover PS4 and PS5 titles with a title ID.";
        return result;
    }

    if (std::chrono::steady_clock::now() < pausedUntil_) {
        result.detail = "skipped after repeated failures: " + pauseReason_;
        return result;
    }

    result = platform == "PS4" ? ps4Latest(toUpper(titleId)) : ps5Latest(toUpper(titleId));

    // Only failures to get any answer count towards backing off; a 404 or a
    // missing link says nothing about the network.
    const bool transportFailure = result.status == core::LatestReleaseStatus::Failed && result.detail.rfind("no answer", 0) == 0;
    if (transportFailure) {
        if (++consecutiveTransportFailures_ >= kConsecutiveFailuresBeforePause) {
            pausedUntil_ = std::chrono::steady_clock::now() + kPauseAfterRepeatedFailures;
            pauseReason_ = result.detail;
            consecutiveTransportFailures_ = 0;
            GTH_LOG_IF_READY(warn, "Pausing Sony update checks for {}s after repeated failures: {}",
                             kPauseAfterRepeatedFailures.count(), pauseReason_);
        }
    } else {
        consecutiveTransportFailures_ = 0;
    }
    return result;
}

core::LatestReleaseResult SonyUpdateService::ps4Latest(const std::string& titleId) {
    core::LatestReleaseResult result;
    const auto response = fetcher_(SonyPs4UpdateUrl(titleId));
    if (response.status == 0) {
        result.detail = "no answer from Sony: " + response.failure;
        return result;
    }
    if (response.status == 404) {
        result.status = core::LatestReleaseStatus::NoUpdatePublished;
        return result;
    }
    if (response.status != 200) {
        result.detail = "Sony answered HTTP " + std::to_string(response.status);
        return result;
    }
    const auto info = ParseSonyPs4UpdateXml(response.body);
    if (!info) {
        result.detail = "Sony's reply wasn't in the expected format";
        return result;
    }
    result.status = core::LatestReleaseStatus::Found;
    result.release.version = info->version;
    result.release.packageSizeBytes = info->sizeBytes;

    // Where the update's files are: a manifest listing every piece with its SHA-256 (the usual case), or, for an
    // update Sony addresses directly, the one file with the whole package's digest. Without either the version
    // is still worth reporting - it just can't be downloaded from here. pieceSizes stays empty for PS4: it is the
    // PS5 full package's file sizes, used to recognise a dump, and an update isn't a dump.
    if (!info->manifestUrl.empty()) {
        const auto manifestResponse = fetcher_(info->manifestUrl);
        if (manifestResponse.status == 200) {
            if (const auto manifest = ParseSonyPackageManifest(manifestResponse.body)) {
                result.release.pieces = manifest->pieces;
                if (manifest->originalFileSize > 0) result.release.packageSizeBytes = manifest->originalFileSize;
            }
        }
    }
    if (result.release.pieces.empty() && !info->url.empty() && info->sizeBytes > 0 && info->digest.size() == 64) {
        core::ReleasePiece piece;
        piece.url = info->url;
        piece.sizeBytes = info->sizeBytes;
        piece.sha256 = info->digest;
        std::transform(piece.sha256.begin(), piece.sha256.end(), piece.sha256.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        result.release.pieces.push_back(std::move(piece));
    }
    return result;
}

core::LatestReleaseResult SonyUpdateService::ps5Latest(const std::string& titleId) {
    core::LatestReleaseResult result;
    reloadLinksIfChanged();
    const auto link = ps5Links_.find(titleId);
    if (link == ps5Links_.end()) {
        result.status = core::LatestReleaseStatus::NoSonyLink;
        result.detail = "no Sony version link saved for " + titleId;
        return result;
    }

    const auto versionFile = fetcher_(link->second);
    if (versionFile.status == 0) {
        result.detail = "no answer from Sony: " + versionFile.failure;
        return result;
    }
    if (versionFile.status != 200) {
        result.detail = "Sony answered HTTP " + std::to_string(versionFile.status) + " for the saved link - it may be out of date";
        return result;
    }
    const auto info = ParseSonyPs5VersionXml(versionFile.body);
    if (!info) {
        result.detail = "Sony's version file wasn't in the expected format";
        return result;
    }

    result.status = core::LatestReleaseStatus::Found;
    result.release.version = info->version;
    result.release.packageSizeBytes = info->sizeBytes;

    // The manifest is what lets a dump be recognized by its file sizes. The
    // version alone is still worth returning if it can't be fetched.
    if (!info->manifestUrl.empty()) {
        const auto manifestResponse = fetcher_(info->manifestUrl);
        if (manifestResponse.status == 200) {
            if (const auto manifest = ParseSonyPackageManifest(manifestResponse.body)) {
                result.release.pieceSizes = manifest->pieceSizes;
                result.release.pieces = manifest->pieces;
                if (manifest->originalFileSize > 0) result.release.packageSizeBytes = manifest->originalFileSize;
            }
        }
    }
    return result;
}

}  // namespace gth::network
