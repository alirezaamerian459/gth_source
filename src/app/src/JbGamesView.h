#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "PosterCache.h"
#include "Theme.h"
#include "gth/application/indexing/IGamePosterService.h"
#include "gth/application/storage/IStorageService.h"

// A local-only view for libraries stored below folders named "hacked" or
// "p30day". It
// discovers existing files on registered local drives and presents them with
// the same poster grid used by GamesView. It never moves, modifies, or
// downloads files.
class JbGamesView : public CWindowImpl<JbGamesView> {
public:
    DECLARE_WND_CLASS(L"GthJbGamesView")

    BEGIN_MSG_MAP(JbGamesView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(PosterCache::kPosterReadyMessage, OnPosterReady)
        COMMAND_ID_HANDLER(IDC_JB_SCAN_BUTTON, OnScanClick)
    END_MSG_MAP()

    void init(gth::application::IStorageService& storageService, PosterCache& posterCache,
              gth::application::IGamePosterService& posterService) {
        storageService_ = &storageService;
        posterCache_ = &posterCache;
        posterService_ = &posterService;
    }

    void refresh();

private:
    struct Entry {
        std::wstring title;
        std::string titleId;
        std::string platform;
        std::filesystem::path path;
        std::wstring details;
    };

    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) {
        return HandleEraseBkgndWithContentBg(m_hWnd, wParam);
    }
    LRESULT OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&);
    LRESULT OnScanClick(WORD, WORD, HWND, BOOL&);
    LRESULT OnPosterReady(UINT, WPARAM, LPARAM, BOOL&) {
        refresh();
        return 0;
    }

    void startScan();
    void renderEntries();

    gth::application::IStorageService* storageService_ = nullptr;
    PosterCache* posterCache_ = nullptr;
    gth::application::IGamePosterService* posterService_ = nullptr;
    CImageList posterImageList_;
    CStatic titleLabel_;
    ModernButton scanButton_;
    CStatic statusLabel_;
    CListViewCtrl listView_;

    std::thread scanThread_;
    std::atomic<bool> scanInProgress_{false};
    bool hasScanned_ = false;
    std::vector<Entry> entries_;
    int libraryFolderCount_ = 0;
    int storageCount_ = 0;
    int exfatStorageCount_ = 0;
    std::wstring scanError_;
};

