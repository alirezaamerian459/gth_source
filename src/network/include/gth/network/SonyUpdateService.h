#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gth/core/index/ILatestReleaseService.h"

namespace gth::network {

struct SonyHttpResponse {
    int status = 0;        // 0 = no HTTP answer at all (see `failure`)
    std::string body;
    std::string failure;   // why there was no answer
};

// Fetches one URL. Injectable so tests never touch the network.
using SonyFetcher = std::function<SonyHttpResponse(const std::string& url)>;

// The real fetcher: WinHTTP, connection reuse per host, and - for Sony's own
// hosts only - trust in Sony's private root certificate, pinned by
// fingerprint (see IsPinnedSonyChain) rather than by switching verification
// off. Not thread-safe; one caller at a time.
SonyFetcher MakeSonyFetcher();

// Whether a reply is worth asking for again: no answer at all, HTTP 429, or a 5xx.
bool ShouldRetrySonyResponse(const SonyHttpResponse& response);

struct SonyRetryPolicy {
    int attempts = 3;                            // in total, the first try included
    std::chrono::milliseconds firstPause{700};   // doubles before each further try
};

// `inner`, asked again when the answer isn't final (see ShouldRetrySonyResponse). `sleep` is injectable so
// tests don't wait; by default the thread sleeps.
SonyFetcher WithRetries(SonyFetcher inner, SonyRetryPolicy policy = {},
                        std::function<void(std::chrono::milliseconds)> sleep = {});

// Asks Sony's update servers for a title's newest release.
//
//  * PS4: the server address is derived from the title ID (an HMAC that the
//    console itself computes and that community tools have long documented),
//    and answers with the latest update's version and size.
//  * PS5: the address of a title's version file carries a random ID that
//    can't be derived, so it comes from a links file - one "<title ID> <tab>
//    <version file URL>" per line, the format the PS5 scene's public lists
//    use. The file is re-read whenever it changes, so a download automation
//    can simply append to it. Sony's answer has the newest version and the
//    exact size of every split file of the full package.
//
// Every failure mode returns a result with an explanation; nothing throws.
class SonyUpdateService : public core::ILatestReleaseService {
public:
    // `linkFiles`: places to look for the PS5 links, in order; later files
    // override earlier ones. Missing files are simply skipped.
    explicit SonyUpdateService(std::vector<std::filesystem::path> linkFiles, SonyFetcher fetcher = MakeSonyFetcher());

    core::LatestReleaseResult latestRelease(const std::string& platform, const std::string& titleId) override;

    // How many PS5 titles the links files currently cover (for the UI).
    size_t knownPs5LinkCount();

private:
    struct FileStamp {
        bool exists = false;
        std::filesystem::file_time_type modified{};
        uintmax_t size = 0;
        bool operator==(const FileStamp&) const = default;
    };

    void reloadLinksIfChanged();
    core::LatestReleaseResult ps4Latest(const std::string& titleId);
    core::LatestReleaseResult ps5Latest(const std::string& titleId);

    std::vector<std::filesystem::path> linkFiles_;
    SonyFetcher fetcher_;
    std::vector<FileStamp> stamps_;
    std::map<std::string, std::string> ps5Links_;  // title ID -> version file URL
    bool linksLoadedOnce_ = false;

    int consecutiveTransportFailures_ = 0;
    std::chrono::steady_clock::time_point pausedUntil_{};
    std::string pauseReason_;
};

// ---- Pure pieces, exposed so they can be tested against canned bodies -----

// "http://gs-sec.ww.np.dl.playstation.net/plo/np/CUSA07410/<hmac>/CUSA07410-ver.xml"
std::string SonyPs4UpdateUrl(const std::string& titleId);

struct SonyUpdateInfo {
    std::string version;
    long long sizeBytes = 0;
    std::string manifestUrl;  // the JSON listing the package's pieces (PS5, and PS4 updates)
    std::string url;          // PS4, when the update is one file that is addressed directly
    std::string digest;       // PS4: SHA-256 of the whole update package, upper-case hex, when Sony states it
};

// PS4 "-ver.xml": the newest <package version=".." size=".."> in it.
std::optional<SonyUpdateInfo> ParseSonyPs4UpdateXml(const std::string& xml);

// PS5 "-version.xml": the newest <package content_ver=".." manifest_url="..">.
std::optional<SonyUpdateInfo> ParseSonyPs5VersionXml(const std::string& xml);

struct SonyPackageManifest {
    long long originalFileSize = 0;
    std::vector<long long> pieceSizes;
    std::vector<core::ReleasePiece> pieces;  // url, size and SHA-256 of each, in order
};

// The package manifest JSON: "originalFileSize" and every piece's "url",
// "fileSize" and "hashValue".
std::optional<SonyPackageManifest> ParseSonyPackageManifest(const std::string& json);

// Title ID -> version file URL from a links file. Accepts the PS5 scene's
// 3-column list (content ID, name, URL) as well as plain "title ID, URL"
// lines; the title ID is read from the URL's own path when it has one.
std::map<std::string, std::string> ParseSonyVersionLinks(const std::string& text);

// Adds the links found in `incomingText` to the links file at `file` (creating
// it if needed), keeping the links already there; for a title in both, the
// incoming one wins. Rewrites the whole file in one piece.
struct SonyLinksMergeResult {
    size_t incoming = 0;  // links found in the incoming text
    size_t total = 0;     // links in the file afterwards
    bool written = false;
};
SonyLinksMergeResult MergeSonyVersionLinks(const std::filesystem::path& file, const std::string& incomingText);

// The title IDs that have a link in any of `files` (missing files are skipped).
std::set<std::string> ReadSonyLinkTitleIds(const std::vector<std::filesystem::path>& files);

// A links file the user (or a tool) can fill in: one "<title ID> <tab> <name> <tab>"
// line per game that still needs a link. The link goes at the end of its line -
// lines without one are ignored - so the same file can be read back as a links file.
std::string BuildSonyLinksNeededList(const std::vector<std::pair<std::string, std::string>>& titleIdsAndNames);

// Whether a certificate chain WinHTTP couldn't fully validate is nonetheless
// Sony's: the only problem Windows found is the root being unknown to it
// (CERT_TRUST_IS_UNTRUSTED_ROOT), and that root's SHA-256 is the one pinned.
// A chain with no problems at all passes too, in case Sony ever moves to a
// public CA. `rootSha256Hex` is lower-case hex.
bool IsPinnedSonyChain(unsigned long trustErrorStatus, const std::string& rootSha256Hex);

}  // namespace gth::network
