#pragma once

#include <chrono>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gth::application {

enum class SyncKind {
    NothingToDo,        // the archive already is Sony's latest, complete package
    CompleteMetadata,   // the files are Sony's latest, but the ~85 MB metadata piece isn't there yet
    Update,             // the files differ from Sony's latest package
    Unavailable,        // can't be synced (no Sony link, not a PS5 package, offline, ...)
};

enum class SyncMode {
    MetadataOnly,  // only ever add the small metadata piece
    Full,          // also bring an out-of-date package up to date (gigabytes)
};

struct SyncPlan {
    SyncKind kind = SyncKind::Unavailable;
    std::string detail;             // plain-language explanation
    std::string titleId;
    std::string latestVersion;
    long long bytesToDownload = 0;  // what is certain to be fetched; an update can turn out to need more
    std::string storageId;
};

struct SyncProgress {
    std::string step;          // e.g. "Downloading piece 3 of 12"
    long long bytesDone = 0;   // of the current file
    long long bytesTotal = 0;

    // Which game this is about, and where it stands in the queue (0 when it isn't part of one).
    std::string titleId;
    int gameIndex = 0;         // 1-based
    int gameCount = 0;
};

struct SyncResult {
    SyncKind kind = SyncKind::Unavailable;  // what the game needed
    bool success = false;                   // it needed something and that was done, or nothing was needed
    bool cancelled = false;
    bool skippedNeedsUpdate = false;        // out of date, but the mode didn't allow updating
    bool transient = false;                 // it failed in a way that can pass by itself (network, a file in use)
    std::string message;
    std::string titleId;
    std::string storageId;  // the storage to rescan so the library sees the change
    int filesDownloaded = 0;
    long long bytesDownloaded = 0;
};

// What happened to one game in a batch - only the games that were worked on or
// need attention are listed, so a report can say what changed.
struct SyncGameOutcome {
    std::string titleId;
    SyncKind kind = SyncKind::Unavailable;
    bool success = false;
    bool skippedNeedsUpdate = false;
    std::string message;
    long long bytesDownloaded = 0;
};

// One game waiting in the download line.
struct SyncQueueItem {
    std::string titleId;
    SyncKind kind = SyncKind::Unavailable;
    long long bytesToDownload = 0;  // at least this much; an update can turn out to need more
};

struct SyncCallbacks {
    std::function<void(const SyncProgress&)> onProgress;
    std::function<bool()> shouldCancel;  // polled between and during downloads

    // runAll only: the games that will be worked on, in order, known before the
    // first download starts; then each game's outcome as soon as it is done.
    std::function<void(const std::vector<SyncQueueItem>&)> onQueue;
    std::function<void(const SyncGameOutcome&)> onGameDone;

    // runAll only: games that failed in a way that can pass by itself (a dropped connection, a download that
    // didn't match Sony's checksum) get another go once the rest of the line is done - up to `retryRounds`
    // times, waiting `retryPause` (times the round number) before each. 0 rounds: a failed game just stays failed.
    int retryRounds = 0;
    std::chrono::seconds retryPause{60};
};

struct SyncBatchResult {
    SyncBatchResult() = default;
    SyncBatchResult(const SyncBatchResult&) = default;
    SyncBatchResult& operator=(const SyncBatchResult&) = default;
    SyncBatchResult(SyncBatchResult&&) noexcept = default;
    SyncBatchResult& operator=(SyncBatchResult&&) noexcept = default;

    int completed = 0;      // metadata piece added
    int updated = 0;        // package brought up to date
    int alreadyFine = 0;
    int needUpdate = 0;     // out of date, but the mode didn't allow updating
    int unavailable = 0;
    int failed = 0;
    int recoveredOnRetry = 0;  // failed the first time, worked on a later round (counted in completed/updated too)
    bool cancelled = false;
    long long bytesDownloaded = 0;
    std::string firstFailure;
    std::set<std::string> storageIds;  // storages that changed and should be rescanned
    std::vector<SyncGameOutcome> games;
};

// PS4 games are kept current differently: Sony publishes one cumulative update file per title (served as
// consecutive pieces when it is big), which is downloaded, checked against Sony's SHA-256 piece by piece, joined
// into that one .pkg and saved in an "Updates" folder beside the game's own files.
//
// Keeps PS5 games in the archive in step with what Sony serves: adds the
// metadata piece their downloads lack (so the title and version can be read
// from the files themselves) and, when asked, replaces the pieces of a
// package that has fallen behind. Everything fetched is checked against
// Sony's published SHA-256 before it touches the archive.
class IPackageSync {
public:
    virtual ~IPackageSync() = default;

    // What would be done for one game. Asks Sony; changes nothing.
    virtual SyncPlan plan(const std::string& gameVariantId) = 0;

    virtual SyncResult run(const std::string& gameVariantId, SyncMode mode, const SyncCallbacks& callbacks = {}) = 0;

    // The download line: works out which games need something, then goes
    // through them one after another until the line is empty (or it is cancelled).
    virtual SyncBatchResult runAll(SyncMode mode, const SyncCallbacks& callbacks = {}) = 0;

    // The "_sc.pkg" metadata piece of every PS5 game in the library that has one on disk: (title ID, full path).
    // Its param.json states the game's Sony version link, so these files are a way to learn links with no help.
    virtual std::vector<std::pair<std::string, std::string>> metadataPieceFiles() { return {}; }
};

}  // namespace gth::application
