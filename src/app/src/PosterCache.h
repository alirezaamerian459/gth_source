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
// title, PSPrices art once enableWebFallback resolves it, package art as an immediate offline
// fallback, and a placeholder until either one succeeds. Every
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

    // The tile for `titleId` or, when a title ID is unavailable, the plain game title. PSPrices is
    // queued first and runs off the UI thread; package art may be shown immediately while that
    // lookup is in flight. When PSPrices returns, its poster replaces the fallback and becomes the
    // only disk-cached source for future launches. Every later call is a plain map lookup.
    // `posterService` may be null (nothing to resolve from yet); `gameVariantId` is unused then.
    HBITMAP posterFor(const std::string& titleId, const std::string& title, const std::string& platform,
                       gth::application::IGamePosterService* posterService, const std::string& gameVariantId);

private:
    struct WebRequest {
        std::string cacheKey;
        std::string title;
    };

    HBITMAP buildFromPng(const std::string& pngBytes);
    HBITMAP placeholderFor(const std::string& platform);

    std::string readDiskCache(const std::string& cacheKey) const;
    void writeDiskCache(const std::string& cacheKey, const std::string& pngBytes) const;

    // Moves anything the background worker has finished into byTitleId_ - called at the top of
    // every posterFor() so a completed fetch is visible before that call's own cache lookup.
    void drainPendingWebResults();
    // Queues titleId/title for a background web lookup, unless it's already been queued this
    // session - never blocks. No-op if enableWebFallback was never called.
    void queueWebFetch(const std::string& cacheKey, const std::string& title);
    void workerLoop();

    ULONG_PTR gdiplusToken_ = 0;
    std::unordered_map<std::string, HBITMAP> byTitleId_;
    std::unordered_map<std::string, HBITMAP> placeholdersByPlatform_;
    std::wstring cacheDir_;

    // UI-thread-only state (posterFor/subscribe/enableWebFallback all run there).
    gth::core::ICoverArtClient* webClient_ = nullptr;
    std::unordered_set<std::string> requestedCacheKeys_;
    std::vector<HWND> subscribers_;

    // Shared with the background worker.
    std::jthread worker_;
    std::atomic<bool> running_{false};
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<WebRequest> requestQueue_;
    std::mutex resultsMutex_;
    std::vector<std::pair<std::string, std::string>> pendingResults_;  // cache key -> raw image bytes
};
