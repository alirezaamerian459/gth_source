#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "PosterCache.h"
#include "SharedArchiveState.h"
#include "Theme.h"
#include "gth/application/indexing/IGameIndexer.h"
#include "gth/application/indexing/IGameLibraryService.h"
#include "gth/application/indexing/IGamePosterService.h"
#include "gth/application/storage/IStorageService.h"
#include "gth/core/index/ITitleLookupService.h"

// Posted from the lookup-test thread back to the UI thread; lParam is an owned
// std::wstring* holding the text to show.
constexpr UINT kTestLookupDoneMessage = WM_APP + 42;

// The library: scan the storages, browse what was found. Keeping games up to
// date - the Sony check, syncing, automation, versions - is the Updates tab.
class GamesView : public CWindowImpl<GamesView> {
public:
    DECLARE_WND_CLASS(L"GthGamesView")

    BEGIN_MSG_MAP(GamesView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(kTestLookupDoneMessage, OnTestLookupDone)
        MESSAGE_HANDLER(PosterCache::kPosterReadyMessage, OnPosterReady)
        COMMAND_ID_HANDLER(IDC_SCAN_GAMES_BUTTON, OnScanClick)
        COMMAND_ID_HANDLER(IDC_GAMES_TEST_LOOKUP_BUTTON, OnTestLookupClick)
    END_MSG_MAP()

    void init(gth::application::IStorageService& storageService, gth::application::IGameIndexer& indexer,
              gth::application::IGameLibraryService& library, gth::core::ITitleLookupService& titleLookup,
              SharedArchiveState& shared, PosterCache& posterCache, gth::application::IGamePosterService& posterService) {
        storageService_ = &storageService;
        indexer_ = &indexer;
        library_ = &library;
        titleLookup_ = &titleLookup;
        shared_ = &shared;
        posterCache_ = &posterCache;
        posterService_ = &posterService;
    }

    void refresh();

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnScanClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnTestLookupClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnTestLookupDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPosterReady(UINT, WPARAM, LPARAM, BOOL&) { refresh(); return 0; }

    // Buttons that start work are off while any job - this tab's or the
    // Updates tab's - is running.
    void setBusy(bool busy);
    bool anythingRunning() const;

    gth::application::IStorageService* storageService_ = nullptr;
    gth::application::IGameIndexer* indexer_ = nullptr;
    gth::application::IGameLibraryService* library_ = nullptr;
    gth::core::ITitleLookupService* titleLookup_ = nullptr;
    SharedArchiveState* shared_ = nullptr;
    PosterCache* posterCache_ = nullptr;
    gth::application::IGamePosterService* posterService_ = nullptr;
    CImageList posterImageList_;

    // scanStorage() walks the whole storage tree - for a multi-TB library
    // that can take a long time. Running it on the UI thread would leave the
    // window unable to pump messages for that whole stretch, which is
    // exactly what Windows flags as "Not Responding" (and what looks like
    // the app being "stuck" to the user). Run it on this background thread
    // instead; a short-lived poll timer notices completion and updates the
    // UI on the UI thread.
    std::thread scanThread_;
    std::atomic<bool> scanInProgress_{false};

    // "Test Online Lookup": asks the tracker sites about two known titles and
    // shows exactly what came back (or why it didn't), independent of any
    // scan. Shares the lookup service with the scan thread, which isn't safe
    // to use from two threads, so the two never run at the same time.
    std::thread testThread_;
    std::atomic<bool> testInProgress_{false};

    // Written by the background thread before scanInProgress_.store(false),
    // read on the UI thread only after scanInProgress_.load() observes that
    // store - the atomic flag is the handoff, so no extra locking needed.
    int scanTotalGamesAdded_ = 0;
    int scanTotalVersionsAdded_ = 0;
    int scanTotalVersionsUpdated_ = 0;
    int scanStoragesScanned_ = 0;
    int scanLookupsResolved_ = 0;
    int scanLookupsNotFound_ = 0;
    int scanLookupsFailed_ = 0;
    int scanReleasesNamedFromFiles_ = 0;
    int scanReleasesNotEligible_ = 0;
    int scanVariantsFailed_ = 0;
    std::wstring scanFirstVariantError_;
    std::wstring scanLookupFailure_;
    std::wstring scanLastError_;

    void startScan();

    CStatic titleLabel_;
    ModernButton scanButton_;
    ModernButton testLookupButton_;
    CStatic statusLabel_;
    CListViewCtrl listView_;
};
