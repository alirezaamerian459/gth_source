#include "DownloadsView.h"

#include <shellapi.h>

#include <array>
#include <fstream>

#pragma comment(lib, "shell32.lib")

namespace {
constexpr int kMargin = 24;
constexpr int kRowHeight = 28;
constexpr int kMainRowTop = 24;
constexpr int kSummaryTop = kMainRowTop + kRowHeight + 12;
constexpr int kListTop = kSummaryTop + kRowHeight + 8;

// While a download is running, its row's own text changes several times a second (Updates
// throttles those to ~2/sec - see UpdatesView::onProgress); a plain human glance at this tab
// doesn't need to be that current, just current enough to look alive.
constexpr UINT_PTR kLiveRefreshTimerId = 1;
constexpr UINT kLiveRefreshTickMs = 1000;

std::wstring formatSize(long long bytes) {
    std::array<wchar_t, 48> buffer{};
    if (bytes >= 1024LL * 1024 * 1024) {
        swprintf_s(buffer.data(), buffer.size(), L"%.2f GB", bytes / 1024.0 / 1024.0 / 1024.0);
    } else {
        swprintf_s(buffer.data(), buffer.size(), L"%.1f MB", bytes / 1024.0 / 1024.0);
    }
    return buffer.data();
}

std::wstring formatDuration(long long seconds) {
    if (seconds <= 0) return L"-";
    if (seconds >= 3600) return std::to_wstring(seconds / 3600) + L"h " + std::to_wstring((seconds % 3600) / 60) + L"m";
    if (seconds >= 60) return std::to_wstring(seconds / 60) + L"m " + std::to_wstring(seconds % 60) + L"s";
    return std::to_wstring(seconds) + L"s";
}

// Only meaningful when both are known: a batch download's per-game duration isn't tracked (the
// batch's total time covers every game in it, not one), so bytesDownloaded alone would otherwise
// read as an implausible speed.
std::wstring formatSpeed(long long bytes, long long seconds) {
    if (bytes <= 0 || seconds <= 0) return L"-";
    return formatSize(static_cast<long long>(bytes / static_cast<double>(seconds))) + L"/s";
}

std::wstring formatWhen(std::time_t when) {
    std::tm local{};
    localtime_s(&local, &when);
    std::array<wchar_t, 32> buffer{};
    wcsftime(buffer.data(), buffer.size(), L"%Y-%m-%d %H:%M", &local);
    return buffer.data();
}

std::wstring kindText(gth::application::SyncKind kind) {
    using gth::application::SyncKind;
    switch (kind) {
        case SyncKind::NothingToDo: return L"Already current";
        case SyncKind::CompleteMetadata: return L"Metadata";
        case SyncKind::Update: return L"Update";
        case SyncKind::Unavailable: return L"Unavailable";
    }
    return L"Unavailable";
}

std::wstring outcomeText(gth::application::DownloadHistoryOutcome outcome) {
    using gth::application::DownloadHistoryOutcome;
    switch (outcome) {
        case DownloadHistoryOutcome::Success: return L"Done";
        case DownloadHistoryOutcome::Failed: return L"FAILED";
        case DownloadHistoryOutcome::Cancelled: return L"Cancelled";
        case DownloadHistoryOutcome::SkippedNeedsUpdate: return L"Not downloaded";
    }
    return L"FAILED";
}
}  // namespace

LRESULT DownloadsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CRect titleRect(kMargin, kMainRowTop - 2, kMargin + 170, kMainRowTop + 36);
    titleLabel_.Create(m_hWnd, titleRect, L"Downloads", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    int x = kMargin + 190;
    const auto placeButton = [&](ModernButton& button, const wchar_t* text, int width, UINT id, ModernButton::Kind kind) {
        CRect rect(x, kMainRowTop, x + width, kMainRowTop + kRowHeight);
        button.Create(m_hWnd, rect, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, id);
        button.SetFont(theme::BodyFont());
        button.setKind(kind);
        x += width + 8;
    };
    placeButton(refreshButton_, L"Refresh", 90, IDC_DOWNLOADS_REFRESH_BUTTON, ModernButton::Kind::Secondary);
    placeButton(openFileButton_, L"Open the history file", 160, IDC_DOWNLOADS_OPEN_FILE_BUTTON, ModernButton::Kind::Subtle);
    placeButton(stopButton_, L"Stop selected", 110, IDC_DOWNLOADS_STOP_BUTTON, ModernButton::Kind::Danger);

    CRect summaryRect(kMargin, kSummaryTop, kMargin + 900, kSummaryTop + kRowHeight);
    summaryLabel_.Create(m_hWnd, summaryRect, L"", WS_CHILD | WS_VISIBLE);
    summaryLabel_.SetFont(theme::BodyFont());

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(kMargin, kListTop, clientRect.right - kMargin, clientRect.bottom - kMargin);
    listView_.Create(m_hWnd, listRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 0,
                      IDC_DOWNLOADS_LISTVIEW);
    listView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    listView_.SetFont(theme::BodyFont());
    theme::ApplyListViewTheme(listView_);

    listView_.InsertColumn(0, L"When", LVCFMT_LEFT, 130);
    listView_.InsertColumn(1, L"Game", LVCFMT_LEFT, 240);
    listView_.InsertColumn(2, L"Type", LVCFMT_LEFT, 100);
    listView_.InsertColumn(3, L"Status", LVCFMT_LEFT, 100);
    listView_.InsertColumn(4, L"Size", LVCFMT_LEFT, 90);
    listView_.InsertColumn(5, L"Duration", LVCFMT_LEFT, 90);
    listView_.InsertColumn(6, L"Speed", LVCFMT_LEFT, 90);
    listView_.InsertColumn(7, L"Details", LVCFMT_LEFT, 400);

    refresh();
    SetTimer(kLiveRefreshTimerId, kLiveRefreshTickMs);
    return 0;
}

LRESULT DownloadsView::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
    KillTimer(kLiveRefreshTimerId);
    return 0;
}

LRESULT DownloadsView::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (wParam != kLiveRefreshTimerId) return 0;
    // Skip the work while this tab isn't the one on screen - MainFrame::showView already
    // refreshes it once on the way in, so nothing is missed by waiting.
    if (::IsWindowVisible(m_hWnd)) refresh();
    return 0;
}

LRESULT DownloadsView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    if (listView_.IsWindow()) {
        listView_.MoveWindow(kMargin, kListTop, std::max(100, width - 2 * kMargin), std::max(100, height - kListTop - kMargin));
    }
    return 0;
}

LRESULT DownloadsView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorStatic(wParam); }

LRESULT DownloadsView::OnRefreshClick(WORD, WORD, HWND, BOOL&) {
    refresh();
    return 0;
}

LRESULT DownloadsView::OnStopClick(WORD, WORD, HWND, BOOL&) {
    if (!shared_) return 0;

    const int selected = listView_.GetNextItem(-1, LVNI_SELECTED);
    if (selected < 0 || static_cast<size_t>(selected) >= rowMeta_.size()) {
        summaryLabel_.SetWindowText(L"Select a downloading or queued row first, then press Stop.");
        return 0;
    }

    const auto& meta = rowMeta_[static_cast<size_t>(selected)];
    if (meta.kind == RowKind::Live) {
        // UpdatesView::startSync's shouldCancel callback checks this from the sync's own
        // background thread - this is the only piece of this tab that isn't UI-thread-only.
        shared_->cancelRequested.store(true);
        summaryLabel_.SetWindowText(L"Stopping the current download...");
    } else if (meta.kind == RowKind::Queued) {
        auto& queue = shared_->queuedDownloads;
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (it->gameVariantId == meta.key) {
                queue.erase(it);
                break;
            }
        }
        refresh();
        return 0;
    } else {
        summaryLabel_.SetWindowText(L"That download already finished - there's nothing to stop.");
        return 0;
    }

    refresh();
    return 0;
}

LRESULT DownloadsView::OnOpenFileClick(WORD, WORD, HWND, BOOL&) {
    std::error_code ec;
    if (!std::filesystem::exists(historyPath_, ec)) {
        std::filesystem::create_directories(historyPath_.parent_path(), ec);
        std::ofstream(historyPath_, std::ios::binary)
            << "# Download history: when, title ID, game, type, status, bytes, duration (s), message - one line per "
               "download. Read by the Downloads tab; safe to archive or clear by hand.\n";
    }
    ::ShellExecuteW(m_hWnd, L"open", L"notepad.exe", (L"\"" + historyPath_.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    return 0;
}

void DownloadsView::refresh() {
    if (!listView_.IsWindow()) return;

    const auto entries = gth::application::ReadDownloadHistory(historyPath_);
    const auto live = shared_ ? shared_->liveDownloads : std::unordered_map<std::string, LiveDownloadRow>{};
    const auto& queued = shared_ ? shared_->queuedDownloads : std::deque<QueuedDownloadRow>{};

    listView_.SetRedraw(FALSE);
    listView_.DeleteAllItems();
    rowMeta_.clear();

    int row = 0;
    for (const auto& [titleId, info] : live) {
        listView_.InsertItem(row, L"Now");
        listView_.SetItemText(row, 1, Utf8ToWide(info.title.empty() ? titleId : info.title).c_str());
        listView_.SetItemText(row, 2, L"-");
        listView_.SetItemText(row, 3, L"Downloading...");
        listView_.SetItemText(row, 4, L"-");
        listView_.SetItemText(row, 5, L"-");
        listView_.SetItemText(row, 6, L"-");
        listView_.SetItemText(row, 7, info.statusText.c_str());
        rowMeta_.push_back({RowKind::Live, titleId});
        ++row;
    }

    for (const auto& item : queued) {
        listView_.InsertItem(row, L"Queued");
        listView_.SetItemText(row, 1, Utf8ToWide(item.title.empty() ? item.titleId : item.title).c_str());
        listView_.SetItemText(row, 2, L"-");
        listView_.SetItemText(row, 3, L"Waiting...");
        listView_.SetItemText(row, 4, L"-");
        listView_.SetItemText(row, 5, L"-");
        listView_.SetItemText(row, 6, L"-");
        listView_.SetItemText(row, 7, L"Waiting for the current download to finish - select and press Stop to drop it.");
        rowMeta_.push_back({RowKind::Queued, item.gameVariantId});
        ++row;
    }

    long long totalBytes = 0;
    int failed = 0;
    for (const auto& entry : entries) {
        totalBytes += entry.bytesDownloaded;
        if (entry.outcome == gth::application::DownloadHistoryOutcome::Failed) ++failed;

        const std::wstring game = entry.title.empty() ? Utf8ToWide(entry.titleId) : Utf8ToWide(entry.title);
        listView_.InsertItem(row, formatWhen(entry.when).c_str());
        listView_.SetItemText(row, 1, game.c_str());
        listView_.SetItemText(row, 2, kindText(entry.kind).c_str());
        listView_.SetItemText(row, 3, outcomeText(entry.outcome).c_str());
        listView_.SetItemText(row, 4, entry.bytesDownloaded > 0 ? formatSize(entry.bytesDownloaded).c_str() : L"-");
        listView_.SetItemText(row, 5, formatDuration(entry.durationSeconds).c_str());
        listView_.SetItemText(row, 6, formatSpeed(entry.bytesDownloaded, entry.durationSeconds).c_str());
        listView_.SetItemText(row, 7, Utf8ToWide(entry.message).c_str());
        rowMeta_.push_back({RowKind::History, {}});
        ++row;
    }

    listView_.SetRedraw(TRUE);
    listView_.Invalidate();

    std::wstring summary;
    if (!live.empty()) summary = std::to_wstring(live.size()) + L" downloading right now. ";
    if (!queued.empty()) summary += std::to_wstring(queued.size()) + L" queued. ";
    summary += std::to_wstring(entries.size()) + L" download(s) recorded, " + formatSize(totalBytes) + L" total";
    if (failed > 0) summary += L", " + std::to_wstring(failed) + L" failed";
    summary += L".";
    if (entries.empty() && live.empty() && queued.empty()) {
        summary = L"No downloads recorded yet - they'll show up here as Update All Games, Sync with Sony, "
                  L"or the guided link collector's auto-download do their work.";
    }
    summaryLabel_.SetWindowText(summary.c_str());
}
