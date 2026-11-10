#include "NetworkView.h"

#include <algorithm>

#include "gth/core/network/NetworkInterfaceInfo.h"

namespace {
constexpr int kTopAreaHeight = 76;
constexpr int kMargin = 24;
}  // namespace

LRESULT NetworkView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    HFONT g_normalFont = theme::BodyFont();

    CRect titleRect(kMargin, 24, kMargin + 200, 60);
    titleLabel_.Create(m_hWnd, titleRect, L"Network Interfaces", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    CRect refreshRect(kMargin + 220, 28, kMargin + 340, 60);
    refreshButton_.Create(m_hWnd, refreshRect, L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                           IDC_REFRESH_NETWORK_BUTTON);
    refreshButton_.SetFont(g_normalFont);
    refreshButton_.setKind(ModernButton::Kind::Secondary);

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(kMargin, kTopAreaHeight, clientRect.right - kMargin, clientRect.bottom - kMargin);
    listView_.Create(m_hWnd, listRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 0,
                      IDC_NETWORK_LISTVIEW);
    listView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    listView_.SetFont(g_normalFont);
    theme::ApplyListViewTheme(listView_);

    listView_.InsertColumn(0, L"Name", LVCFMT_LEFT, 220);
    listView_.InsertColumn(1, L"Type", LVCFMT_LEFT, 110);
    listView_.InsertColumn(2, L"Status", LVCFMT_LEFT, 70);
    listView_.InsertColumn(3, L"IPv4", LVCFMT_LEFT, 130);
    listView_.InsertColumn(4, L"Gateway", LVCFMT_LEFT, 130);
    listView_.InsertColumn(5, L"MAC", LVCFMT_LEFT, 140);

    refresh();
    return 0;
}

LRESULT NetworkView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    if (listView_.IsWindow()) {
        listView_.MoveWindow(kMargin, kTopAreaHeight, std::max(100, width - 2 * kMargin),
                              std::max(100, height - kTopAreaHeight - kMargin));
    }
    return 0;
}

LRESULT NetworkView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorStatic(wParam); }

void NetworkView::refresh() {
    if (!detector_ || !listView_.IsWindow()) return;

    theme::ApplyListViewTheme(listView_);
    listView_.DeleteAllItems();

    int row = 0;
    for (const auto& iface : detector_->detect()) {
        listView_.InsertItem(row, Utf8ToWide(iface.name).c_str());
        listView_.SetItemText(row, 1, Utf8ToWide(gth::core::toString(iface.type)).c_str());
        listView_.SetItemText(row, 2, Utf8ToWide(gth::core::toString(iface.status)).c_str());
        listView_.SetItemText(row, 3, Utf8ToWide(iface.ipv4).c_str());
        listView_.SetItemText(row, 4, Utf8ToWide(iface.gateway).c_str());
        listView_.SetItemText(row, 5, Utf8ToWide(iface.macAddress).c_str());
        row++;
    }
}

LRESULT NetworkView::OnRefreshClick(WORD, WORD, HWND, BOOL&) {
    refresh();
    return 0;
}
