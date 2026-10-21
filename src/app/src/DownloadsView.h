#pragma once

#include <filesystem>
#include <vector>

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "SharedArchiveState.h"
#include "Theme.h"
#include "gth/application/indexing/DownloadHistory.h"

// Every download Update All Games / Sync with Sony / the guided link collector's auto-download
// have ever done, one row per game, newest first - real per-game numbers (size, how long it
// took, the resulting speed) read back from the history file UpdatesView appends to, rather than
// only the narrative text sony-automation-log.txt keeps. What's downloading *right now*, and
// what's queued behind it waiting for a turn (both from shared state Updates keeps), are shown
// above the history, refreshed every second while this tab is open. Select the live row or a
// queued one and press Stop to cancel the current download or drop a queued one before it
// starts - a history row has nothing left to stop, so Stop is a no-op on those. This tab never
// starts a download itself, and never touches Sony or the archive directly - Stop only ever
// asks Updates to cancel or drop something it already owns.
class DownloadsView : public CWindowImpl<DownloadsView> {
public:
    DECLARE_WND_CLASS(L"GthDownloadsView")

    BEGIN_MSG_MAP(DownloadsView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        COMMAND_ID_HANDLER(IDC_DOWNLOADS_REFRESH_BUTTON, OnRefreshClick)
        COMMAND_ID_HANDLER(IDC_DOWNLOADS_OPEN_FILE_BUTTON, OnOpenFileClick)
        COMMAND_ID_HANDLER(IDC_DOWNLOADS_STOP_BUTTON, OnStopClick)
    END_MSG_MAP()

    void init(std::filesystem::path historyPath, SharedArchiveState& shared) {
        historyPath_ = std::move(historyPath);
        shared_ = &shared;
    }
    void refresh();

private:
    // What a row actually is, so Stop knows what pressing it should do - kept in lockstep with
    // the ListView's rows (same index) rather than trying to recover it from displayed text.
    enum class RowKind { Live, Queued, History };
    struct RowMeta {
        RowKind kind;
        std::string key;  // titleId for Live (matches shared_->liveDownloads), gameVariantId for Queued
    };

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnRefreshClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnOpenFileClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnStopClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);

    std::filesystem::path historyPath_;
    SharedArchiveState* shared_ = nullptr;
    std::vector<RowMeta> rowMeta_;

    CStatic titleLabel_;
    ModernButton refreshButton_;
    ModernButton openFileButton_;
    ModernButton stopButton_;
    CStatic summaryLabel_;
    CListViewCtrl listView_;
};
