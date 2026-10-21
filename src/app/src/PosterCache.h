#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "WtlCommon.h"
#include "gth/application/indexing/IGamePosterService.h"
#include "gth/core/network/ICoverArtClient.h"

// Box-art tiles for the Games/Updates poster grid: a themed rounded-corner square bitmap per
// title, real box art once IGamePosterService can supply it (or, failing that, a keyless web
// lookup by title text - see enableWebFallback), a placeholder until either one succeeds. Every
// bitmap this hands out is owned by the cache and reused for the app's whole lifetime - views
// never delete them, just like theme::*Brush()'s already-established "intentionally leaked for
// the process lifetime" convention.
class PosterCache {
public:
    static constexpr int kTileWidth = 128;
    static constexpr int kTileHeight = 128;

    // Posted to every subscribe()'d window once a background web-fallback fetch finishes (whether
    // it found art or not) - the handler should just call its view's own refresh(), which re-asks
    // posterFor() for every visible tile and so picks up whatever's now cached. wParam/lParam unused.
    static constexpr UINT kPosterReadyMessage = WM_APP + 52;

    PosterCache();
    ~PosterCache();
    PosterCache(const PosterCache&) = delete;
    PosterCache& operator=(const PosterCache&) = delete;

    // Turns on the web fallback and starts its single background worker thread (idle until
    // something is actually queued - see posterFor). Call once, after every view that will show
    // posters has already subscribe()'d (the worker only ever reads the subscriber list, so there's
    // no need to guard it with a lock as long as nothing subscribes after this starts).
    void enableWebFallback(gth::core::ICoverArtClient& client);

    // Registers a window to be notified (see kPosterReadyMessage) when a web-fallback fetch this
    // cache started completes. Called once per view, before enableWebFallback (see its own note).
    void subscribe(HWND hwnd);

    // The tile for `titleId` (a game with no titleId, or no title text to search by, always gets
    // `platform`'s placeholder tile). The first call for a given titleId does real work - a disk-
    // cache read, or (a cache miss) reading the game's own package file(s) through `posterService`;
    // if that also comes up empty and the web fallback is enabled, this returns the placeholder
    // immediately and queues a background web lookup by `title` rather than blocking the
    // UI thread on a network call - a later posterFor() call for the same titleId then picks up
    // whatever that lookup found (see kPosterReadyMessage). Every later call once a titleId is
    // fully resolved (real art either way, or confirmed unavailable) is a plain map lookup.
    // `posterService` may be null (nothing to resolve from yet); `gameVariantId` is unused then.
    HBITMAP posterFor(const std::string& titleId, const std::string& title, const std::string& platform,
                       gth::application::IGamePosterService* posterService, const std::string& gameVariantId);

private:
    struct WebRequest {
        std::string titleId;
        std::string title;
    };

    HBITMAP buildFromPng(const std::string& pngBytes);
    HBITMAP placeholderFor(const std::string& platform);

    std::string readDiskCache(const std::string& titleId) const;
    void writeDiskCache(const std::string& titleId, const std::string& pngBytes) const;

    // Moves anything the background worker has finished into byTitleId_ - called at the top of
    // every posterFor() so a completed fetch is visible before that call's own cache lookup.
    void drainPendingWebResults();
    // Queues titleId/title for a background web lookup, unless it's already been queued this
    // session - never blocks. No-op if enableWebFallback was never called.
    void queueWebFetch(const std::string& titleId, const std::string& title);
    void workerLoop();

    ULONG_PTR gdiplusToken_ = 0;
    std::unordered_map<std::string, HBITMAP> byTitleId_;
    std::unordered_map<std::string, HBITMAP> placeholdersByPlatform_;
    std::wstring cacheDir_;

    // UI-thread-only state (posterFor/subscribe/enableWebFallback all run there).
    gth::core::ICoverArtClient* webClient_ = nullptr;
    std::unordered_set<std::string> requestedTitleIds_;
    std::vector<HWND> subscribers_;

    // Shared with the background worker.
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<WebRequest> requestQueue_;
    std::mutex resultsMutex_;
    std::vector<std::pair<std::string, std::string>> pendingResults_;  // titleId -> raw image bytes
};
