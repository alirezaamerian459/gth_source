#pragma once

#include <optional>
#include <string>
#include <vector>

namespace gth::core {

// One file of a package as Sony serves it.
struct ReleasePiece {
    std::string url;
    long long sizeBytes = 0;
    std::string sha256;  // lower-case hex, empty when Sony gave none
    // Where this piece starts inside the whole package ("fileOffset"); -1 when Sony didn't say. A PS4 update
    // is one file that Sony serves as consecutive pieces - these offsets are how that is checked.
    long long offsetBytes = -1;
};

// What the publisher's own update server says the newest release of a title
// is - the ground truth for "is my copy up to date", where the community
// tracker sites are only a mirror of it.
struct LatestRelease {
    // "01.500.000" for PS5, "01.36" for PS4 - as Sony writes it.
    std::string version;

    // PS5: the whole package's size in bytes. PS4: the update package's size.
    long long packageSizeBytes = 0;

    // PS5 only: the size of each split file of the whole package, in order.
    // A dump of that same package is made of exactly these files (or all but
    // a trailing metadata piece), so matching them pins down its version
    // without reading a byte of the game.
    std::vector<long long> pieceSizes;

    // PS5 only: the same pieces with where to fetch each and its SHA-256.
    // The last one is the "_sc.pkg" metadata piece.
    std::vector<ReleasePiece> pieces;
};

enum class LatestReleaseStatus {
    Found,
    // PS5 only: the per-title link to Sony's version file isn't known, so
    // there's nowhere to ask (the link can't be derived from the title ID).
    NoSonyLink,
    // PS4 only: Sony publishes no update for the title, so the game as
    // released is the latest.
    NoUpdatePublished,
    // Couldn't get an answer (offline, blocked, unexpected reply).
    Failed,
};

struct LatestReleaseResult {
    LatestReleaseStatus status = LatestReleaseStatus::Failed;
    LatestRelease release;   // valid when status == Found
    std::string detail;      // why, for NoSonyLink / Failed
};

class ILatestReleaseService {
public:
    virtual ~ILatestReleaseService() = default;

    // `platform` is "PS4" or "PS5"; anything else is reported as Failed with
    // an explanation. Never throws.
    virtual LatestReleaseResult latestRelease(const std::string& platform, const std::string& titleId) = 0;
};

}  // namespace gth::core
