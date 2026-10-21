#include "StorageView.h"

#include <algorithm>

#include "gth/core/enums/StorageStatus.h"
#include "gth/core/enums/StorageType.h"

namespace {
constexpr int kTopAreaHeight = 116;
constexpr int kMargin = 24;

// The health estimate with a plain rating beside it ("87%  Good"). The percentage comes from the disk's own SMART
// data (see SmartHealth.h); where a disk can't be read that way, Windows' Healthy / Warning / Unhealthy shows up as
// 100 / 50 / 10.
std::wstring healthText(const std::optional<int>& percent) {
    if (!percent) return L"Unknown";
    const wchar_t* rating = *percent >= 90 ? L"Excellent" : *percent >= 70 ? L"Good" : *percent >= 40 ? L"Fair" : *percent >= 20 ? L"Poor" : L"Critical";
    return std::to_wstring(*percent) + L"%  " + rating;
}

std::wstring temperatureText(const std::optional<int>& celsius) {
    if (!celsius) return L"Unknown";
    wchar_t buffer[32];
    swprintf_s(buffer, L"%d°C", *celsius);
    return buffer;
}

std::wstring formatSizeBytes(unsigned long long bytes) {
    if (bytes == 0) return L"-";
    wchar_t buffer[48];
    const double gb = static_cast<double>(bytes) / 1024.0 / 1024.0 / 1024.0;
    if (gb >= 1024.0) {
        swprintf_s(buffer, L"%.2f TB", gb / 1024.0);
    } else {
        swprintf_s(buffer, L"%.1f GB", gb);
    }
    return buffer;
}

std::wstring getEditText(CEdit& edit) {
    const int len = edit.GetWindowTextLength();
    std::wstring text(static_cast<size_t>(len), L'\0');
    if (len > 0) edit.GetWindowText(text.data(), len + 1);
    return text;
}

// Only runs while a background rescan is in flight (started in
// OnRescanClick, killed once it's picked up completion) - this app doesn't
// use WM_TIMER for anything else, so a short-lived one here doesn't
// establish a new polling convention, just bridges one background thread's
// result back onto the UI thread.
constexpr UINT_PTR kRescanPollTimerId = 1;
constexpr UINT kRescanPollIntervalMs = 200;

// Keeps capacity/free-space/health/temperature current without the user
// having to click Rescan - runs for the lifetime of the view, started in
// OnCreate and stopped in OnDestroy. startRescan() is a no-op while a scan
// is already in flight, so this can never pile up overlapping background
// rescans if one run takes longer than 5 seconds.
constexpr UINT_PTR kAutoRefreshTimerId = 2;
constexpr UINT kAutoRefreshIntervalMs = 5000;
}  // namespace

LRESULT StorageView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CRect titleRect(kMargin, 24, kMargin + 300, 60);
    titleLabel_.Create(m_hWnd, titleRect, L"Storage", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    CRect rescanRect(kMargin + 320, 28, kMargin + 420, 60);
    rescanButton_.Create(m_hWnd, rescanRect, L"Rescan", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                          IDC_RESCAN_BUTTON);
    rescanButton_.SetFont(theme::BodyFont());
    rescanButton_.setKind(ModernButton::Kind::Secondary);

    // Local drives are auto-detected by Rescan; this is for anything that
    // isn't - a network share (\\server\share) or any other path the
    // detector can't discover on its own.
    CRect addPathRect(kMargin, 70, kMargin + 420, 94);
    addPathEdit_.Create(m_hWnd, addPathRect, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 0,
                         IDC_STORAGE_ADD_PATH_EDIT);
    addPathEdit_.SetFont(theme::BodyFont());
    addPathEdit_.SendMessage(EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"D:\\Games or \\\\server\\share"));

    CRect addPathButtonRect(kMargin + 430, 70, kMargin + 550, 94);
    addPathButton_.Create(m_hWnd, addPathButtonRect, L"Add Storage", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                           0, IDC_STORAGE_ADD_BUTTON);
    addPathButton_.SetFont(theme::BodyFont());
    addPathButton_.setKind(ModernButton::Kind::Secondary);

    CRect addStatusRect(kMargin + 560, 74, kMargin + 960, 92);
    addStatusLabel_.Create(m_hWnd, addStatusRect, L"", WS_CHILD | WS_VISIBLE);
    addStatusLabel_.SetFont(theme::BodyFont());

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(kMargin, kTopAreaHeight, clientRect.right - kMargin, clientRect.bottom - kMargin);
    listView_.Create(m_hWnd, listRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 0,
                      IDC_STORAGE_LISTVIEW);
    listView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    listView_.SetFont(theme::BodyFont());
    theme::ApplyListViewTheme(listView_);

    listView_.InsertColumn(0, L"Name", LVCFMT_LEFT, 280);
    listView_.InsertColumn(1, L"Type", LVCFMT_LEFT, 80);
    listView_.InsertColumn(2, L"Path", LVCFMT_LEFT, 110);
    listView_.InsertColumn(3, L"Capacity", LVCFMT_LEFT, 85);
    listView_.InsertColumn(4, L"Free", LVCFMT_LEFT, 85);
    listView_.InsertColumn(5, L"Status", LVCFMT_LEFT, 70);
    listView_.InsertColumn(6, L"Health", LVCFMT_LEFT, 130);
    listView_.InsertColumn(7, L"Temp", LVCFMT_LEFT, 70);
    listView_.InsertColumn(8, L"Alert", LVCFMT_LEFT, 300);

    refresh();
    SetTimer(kAutoRefreshTimerId, kAutoRefreshIntervalMs);
    return 0;
}

LRESULT StorageView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    if (listView_.IsWindow()) {
        listView_.MoveWindow(kMargin, kTopAreaHeight, std::max(100, width - 2 * kMargin),
                              std::max(100, height - kTopAreaHeight - kMargin));
    }
    return 0;
}

LRESULT StorageView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    KillTimer(kRescanPollTimerId);
    KillTimer(kAutoRefreshTimerId);
    // A scan in flight is bounded (each drive probe has its own timeout) but
    // can still take a few seconds; joining here briefly delays shutdown
    // rather than risking a detached thread touching this object's members
    // after it's destroyed.
    if (rescanThread_.joinable()) rescanThread_.join();
    bHandled = FALSE;
    return 0;
}

LRESULT StorageView::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (wParam == kAutoRefreshTimerId) {
        if (IsWindowVisible()) startRescan();
        return 0;
    }

    if (wParam != kRescanPollTimerId) return 0;
    if (rescanInProgress_.load()) return 0;

    KillTimer(kRescanPollTimerId);
    if (rescanThread_.joinable()) rescanThread_.join();
    rescanButton_.EnableWindow(TRUE);
    rescanButton_.SetWindowText(L"Rescan");
    refresh();
    return 0;
}

LRESULT StorageView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorStatic(wParam); }

void StorageView::refresh() {
    if (!service_ || !listView_.IsWindow()) return;

    theme::ApplyListViewTheme(listView_);
    listView_.DeleteAllItems();
    lastRows_.clear();
    const auto storages = service_->getAllStorages();

    int row = 0;
    for (const auto& s : storages) {
        listView_.InsertItem(row, Utf8ToWide(s.name).c_str());
        listView_.SetItemText(row, 1, Utf8ToWide(gth::core::toString(s.type)).c_str());
        listView_.SetItemText(row, 2, Utf8ToWide(s.rootPath).c_str());

        wchar_t buf[64];
        swprintf_s(buf, L"%.2f GB", s.totalCapacityBytes / 1024.0 / 1024.0 / 1024.0);
        listView_.SetItemText(row, 3, buf);
        swprintf_s(buf, L"%.2f GB", s.freeSpaceBytes / 1024.0 / 1024.0 / 1024.0);
        listView_.SetItemText(row, 4, buf);

        listView_.SetItemText(row, 5, Utf8ToWide(gth::core::toString(s.status)).c_str());

        // A drive letter over several disks shows its worst health and hottest disk on this row; the disks follow.
        listView_.SetItemText(row, 6, healthText(s.healthPercent).c_str());
        listView_.SetItemText(row, 7, temperatureText(s.temperatureCelsius).c_str());
        listView_.SetItemText(row, 8, s.healthAlertReason ? Utf8ToWide(*s.healthAlertReason).c_str() : L"");

        lastRows_.push_back({s.healthAlertReason.has_value()});
        row++;

        // Every physical disk behind a drive that combines several, each with its own health and temperature.
        const auto disks = service_->getPhysicalDisks(s.rootPath);
        if (disks.size() < 2) continue;
        for (const auto& disk : disks) {
            std::wstring label = L"      Disk " + std::to_wstring(disk.diskNumber);
            if (!disk.model.empty()) label += L"  -  " + Utf8ToWide(disk.model);
            listView_.InsertItem(row, label.c_str());
            listView_.SetItemText(row, 1, L"Disk");
            listView_.SetItemText(row, 3, formatSizeBytes(disk.sizeBytes).c_str());
            listView_.SetItemText(row, 6, healthText(disk.healthPercent).c_str());
            listView_.SetItemText(row, 7, temperatureText(disk.temperatureCelsius).c_str());
            listView_.SetItemText(row, 8, Utf8ToWide(disk.healthDetail).c_str());  // why it isn't 100%

            const bool inTrouble = (disk.healthPercent && *disk.healthPercent < 100) ||
                                   (disk.temperatureCelsius && *disk.temperatureCelsius > gth::core::kOverheatThresholdCelsius);
            lastRows_.push_back({inTrouble});
            row++;
        }
    }
}

LRESULT StorageView::OnRescanClick(WORD, WORD, HWND, BOOL&) {
    startRescan();
    return 0;
}

LRESULT StorageView::OnAddStorageClick(WORD, WORD, HWND, BOOL&) {
    if (!service_) return 0;

    const std::string path = WideToUtf8(getEditText(addPathEdit_));
    const auto result = service_->addManualStorage(path);

    if (result.success) {
        addPathEdit_.SetWindowText(L"");
        addStatusLabel_.SetWindowText(L"Added.");
        refresh();
    } else {
        addStatusLabel_.SetWindowText(Utf8ToWide(result.errorMessage).c_str());
    }
    return 0;
}

void StorageView::startRescan() {
    if (!service_ || rescanInProgress_.load()) return;

    if (rescanThread_.joinable()) rescanThread_.join();  // previous run's thread, already finished

    rescanInProgress_.store(true);
    rescanButton_.EnableWindow(FALSE);
    rescanButton_.SetWindowText(L"Scanning...");

    rescanThread_ = std::thread([this] {
        service_->detectAndSyncLocalStorages();
        rescanInProgress_.store(false);
    });
    SetTimer(kRescanPollTimerId, kRescanPollIntervalMs);
}

// Paints a flagged storage's row with a red tint (light red background,
// dark red text) instead of just relying on the Alert column's text -
// lastRows_ is kept parallel to the ListView's rows so this can look
// the flag up by row index without a round-trip through the service.
LRESULT StorageView::OnListViewCustomDraw(int, LPNMHDR pnmh, BOOL&) {
    auto* customDraw = reinterpret_cast<LPNMLVCUSTOMDRAW>(pnmh);

    switch (customDraw->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT: {
            const size_t index = customDraw->nmcd.dwItemSpec;
            const bool flagged = index < lastRows_.size() && lastRows_[index].flagged;
            if (flagged) {
                customDraw->clrTextBk = RGB(253, 231, 228);
                customDraw->clrText = theme::Danger();
            }
            return CDRF_NEWFONT;
        }
        default:
            return CDRF_DODEFAULT;
    }
}
