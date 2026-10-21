#include "DashboardView.h"

namespace {
std::wstring formatGb(long long bytes) {
    wchar_t buf[64];
    swprintf_s(buf, L"%.2f GB", bytes / 1024.0 / 1024.0 / 1024.0);
    return buf;
}

constexpr int kCardWidth = 220;
constexpr int kCardHeight = 108;
constexpr int kCardGap = 20;
}  // namespace

LRESULT DashboardView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    const int left = theme::kMargin;
    int top = theme::kMargin;

    // _U_RECT (the type Create() actually takes) only binds to a named RECT
    // lvalue, not a temporary - so every rect below needs a local variable,
    // not an inline CRect(...) at the call site.
    CRect rc(left, top, left + 400, top + 44);
    titleLabel_.Create(m_hWnd, rc, L"Dashboard", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());
    top += 68;

    cardsTop_ = top;

    auto makeCard = [&](int x, CStatic& caption, CStatic& value, const wchar_t* captionText) {
        CRect captionRect(x + 20, top + 18, x + kCardWidth - 16, top + 38);
        caption.Create(m_hWnd, captionRect, captionText, WS_CHILD | WS_VISIBLE);
        caption.SetFont(theme::SmallFont());

        CRect valueRect(x + 20, top + 40, x + kCardWidth - 16, top + 72);
        value.Create(m_hWnd, valueRect, L"0", WS_CHILD | WS_VISIBLE);
        value.SetFont(theme::SectionFont());
    };

    makeCard(left, storagesCaption_, storagesValue_, L"Storages");
    CRect onlineOfflineRect(left + 20, top + 76, left + kCardWidth - 16, top + 96);
    onlineOfflineValue_.Create(m_hWnd, onlineOfflineRect, L"", WS_CHILD | WS_VISIBLE);
    onlineOfflineValue_.SetFont(theme::SmallFont());

    makeCard(left + (kCardWidth + kCardGap), capacityCaption_, capacityValue_, L"Total Capacity");
    makeCard(left + 2 * (kCardWidth + kCardGap), freeCaption_, freeValue_, L"Free Space");

    top += kCardHeight + 32;
    CRect scanButtonRect(left, top, left + 190, top + 36);
    scanButton_.Create(m_hWnd, scanButtonRect, L"Scan Local Storage",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_SCAN_BUTTON);
    scanButton_.SetFont(theme::BodyFont());
    scanButton_.setKind(ModernButton::Kind::Primary);

    CRect statusRect(left, top + 48, left + 600, top + 68);
    statusLabel_.Create(m_hWnd, statusRect, L"", WS_CHILD | WS_VISIBLE);
    statusLabel_.SetFont(theme::BodyFont());

    // --- Network Connection section ---
    top += 80;
    CRect networkLabelRect(left, top, left + 400, top + 44);
    networkLabel_.Create(m_hWnd, networkLabelRect, L"Network Connection", WS_CHILD | WS_VISIBLE);
    networkLabel_.SetFont(theme::TitleFont());
    top += 48;

    CRect ipCaptionRect(left + 20, top + 18, left + kCardWidth - 16, top + 38);
    ipAddressCaption_.Create(m_hWnd, ipCaptionRect, L"IP Address", WS_CHILD | WS_VISIBLE);
    ipAddressCaption_.SetFont(theme::SmallFont());

    CRect ipValueRect(left + 20, top + 40, left + kCardWidth - 16, top + 72);
    ipAddressValue_.Create(m_hWnd, ipValueRect, L"--", WS_CHILD | WS_VISIBLE);
    ipAddressValue_.SetFont(theme::SectionFont());

    CRect portCaptionRect(left + (kCardWidth + kCardGap) + 20, top + 18,
                           left + (kCardWidth + kCardGap) + kCardWidth - 16, top + 38);
    portCaption_.Create(m_hWnd, portCaptionRect, L"Port", WS_CHILD | WS_VISIBLE);
    portCaption_.SetFont(theme::SmallFont());

    CRect portValueRect(left + (kCardWidth + kCardGap) + 20, top + 40,
                         left + (kCardWidth + kCardGap) + kCardWidth - 16, top + 72);
    portValue_.Create(m_hWnd, portValueRect, L"5000", WS_CHILD | WS_VISIBLE);
    portValue_.SetFont(theme::SectionFont());

    const int networkCardTop = top;
    top += kCardHeight + 32;

    CRect copyButtonRect(left, top, left + 190, top + 36);
    copyIPButton_.Create(m_hWnd, copyButtonRect, L"Copy Connection Info",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_COPY_IP_BUTTON);
    copyIPButton_.SetFont(theme::BodyFont());
    copyIPButton_.setKind(ModernButton::Kind::Secondary);

    CRect networkStatusRect(left, top + 48, left + 600, top + 68);
    networkStatusLabel_.Create(m_hWnd, networkStatusRect, L"", WS_CHILD | WS_VISIBLE);
    networkStatusLabel_.SetFont(theme::BodyFont());

    networkCardsTop_ = networkCardTop;

    refresh();
    refreshNetworkInfo();
    return 0;
}

LRESULT DashboardView::OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) {
    HDC hdc = reinterpret_cast<HDC>(wParam);
    CRect clientRect;
    GetClientRect(&clientRect);

    // Paint the whole background, then draw the three metric cards as
    // rounded, bordered panels on top - card grouping reads more like a
    // modern dashboard than three bare labels floating on the page.
    HBRUSH bg = ::CreateSolidBrush(theme::ContentBg());
    ::FillRect(hdc, &clientRect, bg);
    ::DeleteObject(bg);

    for (int i = 0; i < 3; i++) {
        CRect cardRect(theme::kMargin + i * (kCardWidth + kCardGap), cardsTop_,
                        theme::kMargin + i * (kCardWidth + kCardGap) + kCardWidth, cardsTop_ + kCardHeight);
        theme::FillRoundRect(hdc, cardRect, 10, theme::SidebarBg(), theme::Border());
    }

    for (int i = 0; i < 2; i++) {
        CRect cardRect(theme::kMargin + i * (kCardWidth + kCardGap), networkCardsTop_,
                        theme::kMargin + i * (kCardWidth + kCardGap) + kCardWidth, networkCardsTop_ + kCardHeight);
        theme::FillRoundRect(hdc, cardRect, 10, theme::SidebarBg(), theme::Border());
    }

    return 1;
}

LRESULT DashboardView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    // NULL_BRUSH would skip erasing the control's background entirely, so a
    // shorter new string (e.g. going from "Scanning..." to a shorter
    // result) leaves stale glyphs behind from the old text - every branch
    // here must return a real, opaque brush matching whatever's actually
    // behind that control (the white page or a gray card).
    HBRUSH cardBrush = theme::SidebarBgBrush();
    HBRUSH pageBrush = theme::ContentBgBrush();

    HDC hdc = reinterpret_cast<HDC>(wParam);
    HWND ctrl = reinterpret_cast<HWND>(lParam);
    ::SetBkMode(hdc, TRANSPARENT);

    const bool isCaption = ctrl == storagesCaption_.m_hWnd || ctrl == capacityCaption_.m_hWnd ||
                            ctrl == freeCaption_.m_hWnd || ctrl == onlineOfflineValue_.m_hWnd ||
                            ctrl == ipAddressCaption_.m_hWnd || ctrl == portCaption_.m_hWnd;
    const bool isInCard = isCaption || ctrl == storagesValue_.m_hWnd || ctrl == capacityValue_.m_hWnd ||
                           ctrl == freeValue_.m_hWnd || ctrl == ipAddressValue_.m_hWnd ||
                           ctrl == portValue_.m_hWnd;

    ::SetTextColor(hdc, isCaption ? theme::TextSecondary() : theme::TextPrimary());
    return reinterpret_cast<LRESULT>(isInCard ? cardBrush : pageBrush);
}

void DashboardView::refresh() {
    if (!service_) return;
    const auto summary = service_->getSummary();

    storagesValue_.SetWindowText(std::to_wstring(summary.totalStorages).c_str());

    wchar_t buf[128];
    swprintf_s(buf, L"%d online / %d offline", summary.onlineStorages, summary.offlineStorages);
    onlineOfflineValue_.SetWindowText(buf);

    capacityValue_.SetWindowText(formatGb(summary.totalCapacityBytes).c_str());
    freeValue_.SetWindowText(formatGb(summary.freeSpaceBytes).c_str());
}

LRESULT DashboardView::OnScanClick(WORD, WORD, HWND, BOOL&) {
    if (!service_) return 0;

    scanButton_.EnableWindow(FALSE);
    statusLabel_.SetWindowText(L"Scanning...");
    ::UpdateWindow(statusLabel_.m_hWnd);

    const auto result = service_->detectAndSyncLocalStorages();
    refresh();

    wchar_t buf[256];
    swprintf_s(buf, L"Scan complete: %d added, %d updated, %d marked offline.", result.added, result.updated,
               result.markedOffline);
    statusLabel_.SetWindowText(buf);
    scanButton_.EnableWindow(TRUE);
    return 0;
}

LRESULT DashboardView::OnCopyIPClick(WORD, WORD, HWND, BOOL&) {
    const auto networkInfo = gth::network::NetworkInfoHelper::GetLocalNetworkInfo(5000);
    if (networkInfo.isValid) {
        copyToClipboard(networkInfo.connectionUrl);
        networkStatusLabel_.SetWindowText(L"Connection info copied to clipboard.");
    } else {
        networkStatusLabel_.SetWindowText(L"Failed to get network information.");
    }
    return 0;
}

void DashboardView::refreshNetworkInfo() {
    const auto networkInfo = gth::network::NetworkInfoHelper::GetLocalNetworkInfo(5000);

    if (networkInfo.isValid) {
        ipAddressValue_.SetWindowText(networkInfo.ipAddress.c_str());
        portValue_.SetWindowText(std::to_wstring(networkInfo.port).c_str());
        networkStatusLabel_.SetWindowText(L"Ready to connect.");
    } else {
        ipAddressValue_.SetWindowText(L"Error");
        portValue_.SetWindowText(L"--");
        networkStatusLabel_.SetWindowText(L"Could not determine network information.");
    }
}

void DashboardView::copyToClipboard(const std::wstring& text) {
    if (text.empty()) return;
    if (!::OpenClipboard(m_hWnd)) return;

    ::EmptyClipboard();
    const size_t len = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, len);
    if (!hMem) {
        ::CloseClipboard();
        return;
    }

    void* pData = ::GlobalLock(hMem);
    if (pData) {
        memcpy(pData, text.c_str(), len);
        ::GlobalUnlock(hMem);
        ::SetClipboardData(CF_UNICODETEXT, hMem);
    } else {
        ::GlobalFree(hMem);
    }

    ::CloseClipboard();
}
