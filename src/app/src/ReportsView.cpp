#include "ReportsView.h"

#include <algorithm>

namespace {
constexpr int kTopAreaHeight = 96;
constexpr int kMargin = 24;

void addMetricRow(CListViewCtrl& listView, int row, const wchar_t* metric, long long value) {
    listView.InsertItem(row, metric);
    listView.SetItemText(row, 1, std::to_wstring(value).c_str());
}
}  // namespace

LRESULT ReportsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CRect titleRect(kMargin, 24, kMargin + 260, 56);
    titleLabel_.Create(m_hWnd, titleRect, L"Reports - Archive Health", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    CRect refreshRect(kMargin + 280, 28, kMargin + 400, 60);
    refreshButton_.Create(m_hWnd, refreshRect, L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                           IDC_REFRESH_REPORT_BUTTON);
    refreshButton_.SetFont(theme::BodyFont());
    refreshButton_.setKind(ModernButton::Kind::Subtle);

    CRect generatedRect(kMargin, 62, kMargin + 500, 82);
    generatedAtLabel_.Create(m_hWnd, generatedRect, L"", WS_CHILD | WS_VISIBLE);
    generatedAtLabel_.SetFont(theme::SmallFont());

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(kMargin, kTopAreaHeight, clientRect.right - kMargin, clientRect.bottom - kMargin);
    listView_.Create(m_hWnd, listRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 0,
                      IDC_REPORT_LISTVIEW);
    listView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    listView_.SetFont(theme::BodyFont());
    theme::ApplyListViewTheme(listView_);

    listView_.InsertColumn(0, L"Metric", LVCFMT_LEFT, 280);
    listView_.InsertColumn(1, L"Value", LVCFMT_LEFT, 120);

    refresh();
    return 0;
}

LRESULT ReportsView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    if (listView_.IsWindow()) {
        listView_.MoveWindow(kMargin, kTopAreaHeight, std::max(100, width - 2 * kMargin),
                              std::max(100, height - kTopAreaHeight - kMargin));
    }
    return 0;
}

LRESULT ReportsView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    HDC hdc = reinterpret_cast<HDC>(wParam);
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, reinterpret_cast<HWND>(lParam) == generatedAtLabel_.m_hWnd ? theme::TextSecondary()
                                                                                    : theme::TextPrimary());
    HBRUSH pageBrush = theme::ContentBgBrush();
    return reinterpret_cast<LRESULT>(pageBrush);
}

void ReportsView::refresh() {
    if (!reportService_ || !listView_.IsWindow()) return;

    theme::ApplyListViewTheme(listView_);
    const auto report = reportService_->generate();

    generatedAtLabel_.SetWindowText((L"Generated at " + Utf8ToWide(report.generatedAt)).c_str());

    listView_.DeleteAllItems();
    int row = 0;
    addMetricRow(listView_, row++, L"Storages (total)", report.totalStorages);
    addMetricRow(listView_, row++, L"Storages online", report.onlineStorages);
    addMetricRow(listView_, row++, L"Storages offline", report.offlineStorages);
    addMetricRow(listView_, row++, L"Games (total)", report.totalGames);
    addMetricRow(listView_, row++, L"Game variants (total)", report.totalVariants);
    addMetricRow(listView_, row++, L"Game variants missing", report.missingVariants);
    addMetricRow(listView_, row++, L"Game versions (total)", report.totalVersions);
    addMetricRow(listView_, row++, L"Game versions missing", report.missingVersions);
    addMetricRow(listView_, row++, L"Updates available", report.updatesAvailable);
    addMetricRow(listView_, row++, L"Update jobs pending", report.pendingUpdateJobs);
    addMetricRow(listView_, row++, L"Update jobs failed", report.failedUpdateJobs);
    addMetricRow(listView_, row++, L"Consoles (total)", report.totalConsoles);
    addMetricRow(listView_, row++, L"Consoles online", report.onlineConsoles);
    addMetricRow(listView_, row++, L"Consoles offline", report.offlineConsoles);
    addMetricRow(listView_, row++, L"Transfer jobs active", report.activeTransferJobs);
    addMetricRow(listView_, row++, L"Transfer jobs completed", report.completedTransferJobs);
    addMetricRow(listView_, row++, L"Transfer jobs failed", report.failedTransferJobs);
}

LRESULT ReportsView::OnRefreshClick(WORD, WORD, HWND, BOOL&) {
    refresh();
    return 0;
}
