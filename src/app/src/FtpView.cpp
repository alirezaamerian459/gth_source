#include "FtpView.h"

#include <shlobj.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>

#include "FtpLayout.h"
#include "gth/core/ftp/FtpConnection.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {
constexpr int kMargin = 24;
// Mirror OnCreate's local captionHeight/editHeight - relayoutMitmAndCdnSections()
// needs these same values to recompute the MITM/CDN sections' Y-coordinates
// from scratch (given a variable instance count), but lives outside
// OnCreate's scope so can't reach its locals directly.
constexpr int kFieldCaptionHeight = ftplayout::kFieldCaptionHeight;
constexpr int kFieldEditHeight = ftplayout::kFieldEditHeight;
// Height of one dynamically-added MitmInstance row (controls + gap to the
// next row).
constexpr int kMitmInstanceRowHeight = 40;

// Hosting a folder over FTP without picking one first needs *some* real
// directory to serve from - this is that fallback (created on demand),
// living next to the app's own data directory (%LOCALAPPDATA%\
// GameTransferHub, same convention ServiceContainer's defaultDataDirectory
// uses for the database/logs) rather than forcing the user to browse for
// one just to test that a console can connect at all.
std::wstring defaultShareFolder() {
    char* buffer = nullptr;
    size_t len = 0;
    std::filesystem::path base;
    if (_dupenv_s(&buffer, &len, "LOCALAPPDATA") == 0 && buffer != nullptr) {
        base = std::filesystem::path(buffer) / L"GameTransferHub" / L"Shared";
        free(buffer);
    } else {
        base = std::filesystem::current_path() / L"GameTransferHub_Shared";
    }

    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base.wstring();
}
}  // namespace

LRESULT FtpView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    scroll_.attach(m_hWnd);
    HFONT g_normalFont = theme::BodyFont();

    CRect titleRect(kMargin, 24, kMargin + 200, 56);
    titleLabel_.Create(m_hWnd, titleRect, L"FTP Tools", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    CRect advancedToolsRect(kMargin + 210, 27, kMargin + 400, 55);
    advancedToolsButton_.Create(m_hWnd, advancedToolsRect, L"More network tools  \x25B8",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                IDC_FTP_ADVANCED_TOOLS_BUTTON);
    advancedToolsButton_.SetFont(theme::BodyFont());
    advancedToolsButton_.setKind(ModernButton::Kind::Subtle);

    int x = kMargin;
    const int inputTop = 62;
    const int captionHeight = 18;
    const int editTop = inputTop + captionHeight;
    const int editHeight = 24;

    auto makeField = [&](CStatic& caption, CEdit& edit, const wchar_t* label, int width, bool password = false) {
        CRect capRect(x, inputTop, x + width, inputTop + captionHeight);
        caption.Create(m_hWnd, capRect, label, WS_CHILD | WS_VISIBLE);
        caption.SetFont(g_normalFont);

        CRect editRect(x, editTop, x + width, editTop + editHeight);
        DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
        if (password) style |= ES_PASSWORD;
        edit.Create(m_hWnd, editRect, L"", style);
        edit.SetFont(g_normalFont);

        x += width + 10;
    };

    makeField(nameCaption_, nameEdit_, L"Name", 110);
    makeField(hostCaption_, hostEdit_, L"Host", 130);
    makeField(portCaption_, portEdit_, L"Port", 55);

    CRect protoCapRect(x, inputTop, x + 80, inputTop + captionHeight);
    protocolCaption_.Create(m_hWnd, protoCapRect, L"Protocol", WS_CHILD | WS_VISIBLE);
    protocolCaption_.SetFont(g_normalFont);
    CRect protoRect(x, editTop, x + 80, editTop + 100);
    protocolCombo_.Create(m_hWnd, protoRect, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST);
    protocolCombo_.SetFont(g_normalFont);
    protocolCombo_.AddString(L"FTP");
    protocolCombo_.AddString(L"FTPS");
    protocolCombo_.AddString(L"SFTP");
    protocolCombo_.SetCurSel(0);
    ::SetWindowTheme(protocolCombo_.m_hWnd, L"DarkMode_CFD", nullptr);
    x += 90;

    makeField(userCaption_, userEdit_, L"Username", 110);
    makeField(passCaption_, passEdit_, L"Password", 110, true);

    CRect addRect(x, editTop, x + 110, editTop + editHeight);
    addButton_.Create(m_hWnd, addRect, L"Add Connection", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                       IDC_FTP_ADD_BUTTON);
    addButton_.SetFont(g_normalFont);
    addButton_.setKind(ModernButton::Kind::Primary);

    const int row2 = editTop + editHeight + 12;
    makeField(pathCaption_, pathEdit_, L"Remote Base Path", 200);
    pathEdit_.SetWindowText(L"/");

    const int row3 = row2 + editHeight + captionHeight + 12;
    CRect connectRect(kMargin, row3, kMargin + 110, row3 + editHeight);
    connectButton_.Create(m_hWnd, connectRect, L"Connect", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                           IDC_FTP_CONNECT_BUTTON);
    connectButton_.SetFont(g_normalFont);
    connectButton_.setKind(ModernButton::Kind::Primary);

    CRect disconnectRect(kMargin + 122, row3, kMargin + 232, row3 + editHeight);
    disconnectButton_.Create(m_hWnd, disconnectRect, L"Disconnect", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                              0, IDC_FTP_DISCONNECT_BUTTON);
    disconnectButton_.SetFont(g_normalFont);
    disconnectButton_.setKind(ModernButton::Kind::Subtle);

    CRect removeRect(kMargin + 244, row3, kMargin + 354, row3 + editHeight);
    removeButton_.Create(m_hWnd, removeRect, L"Remove", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                          IDC_FTP_REMOVE_BUTTON);
    removeButton_.SetFont(g_normalFont);
    removeButton_.setKind(ModernButton::Kind::Danger);

    CRect statusDotRect(kMargin + 366, row3 + 3, kMargin + 386, row3 + editHeight);
    statusDot_.Create(m_hWnd, statusDotRect, L"", WS_CHILD | WS_VISIBLE);

    CRect statusRect(kMargin + 388, row3 + 3, kMargin + 900, row3 + editHeight);
    statusLabel_.Create(m_hWnd, statusRect, L"", WS_CHILD | WS_VISIBLE);
    statusLabel_.SetFont(g_normalFont);

    CRect clientRect;
    GetClientRect(&clientRect);
    const int connListTop = row3 + editHeight + 8;
    const int connListBottom = connListTop + 150;
    CRect connListRect(kMargin, connListTop, clientRect.right - kMargin, connListBottom);
    connectionsListView_.Create(m_hWnd, connListRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                                 0, IDC_FTP_CONNECTIONS_LISTVIEW);
    connectionsListView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    connectionsListView_.SetFont(g_normalFont);
    theme::ApplyListViewTheme(connectionsListView_);
    connectionsListView_.InsertColumn(0, L"Name", LVCFMT_LEFT, 140);
    connectionsListView_.InsertColumn(1, L"Host", LVCFMT_LEFT, 150);
    connectionsListView_.InsertColumn(2, L"Port", LVCFMT_LEFT, 60);
    connectionsListView_.InsertColumn(3, L"Protocol", LVCFMT_LEFT, 70);
    connectionsListView_.InsertColumn(4, L"Username", LVCFMT_LEFT, 120);

    // "Host a Server" section: lets this machine act as the FTP server
    // instead of (or in addition to) the client above - point it at a local
    // folder and the other laptop connects to it using this same tab's
    // client panel, no username/password needed.
    const int hostTop = connListBottom + 20;
    CRect hostSectionCapRect(kMargin, hostTop, kMargin + 300, hostTop + 20);
    hostSectionCaption_.Create(m_hWnd, hostSectionCapRect, L"Host a Server", WS_CHILD | WS_VISIBLE);
    hostSectionCaption_.SetFont(theme::BodyFontBold());

    const int hostRowTop = hostTop + 26;
    const int hostEditTop = hostRowTop + captionHeight;

    CRect folderCapRect(kMargin, hostRowTop, kMargin + 200, hostRowTop + captionHeight);
    hostFolderCaption_.Create(m_hWnd, folderCapRect, L"Shared Folder", WS_CHILD | WS_VISIBLE);
    hostFolderCaption_.SetFont(g_normalFont);
    CRect folderEditRect(kMargin, hostEditTop, kMargin + 340, hostEditTop + editHeight);
    hostFolderEdit_.Create(m_hWnd, folderEditRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    hostFolderEdit_.SetFont(g_normalFont);

    CRect browseFolderRect(kMargin + 350, hostEditTop, kMargin + 440, hostEditTop + editHeight);
    hostBrowseButton_.Create(m_hWnd, browseFolderRect, L"Browse...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                              0, IDC_FTP_HOST_BROWSE_BUTTON);
    hostBrowseButton_.SetFont(g_normalFont);
    hostBrowseButton_.setKind(ModernButton::Kind::Subtle);

    CRect hostPortCapRect(kMargin + 452, hostRowTop, kMargin + 512, hostRowTop + captionHeight);
    hostPortCaption_.Create(m_hWnd, hostPortCapRect, L"Port", WS_CHILD | WS_VISIBLE);
    hostPortCaption_.SetFont(g_normalFont);
    CRect hostPortEditRect(kMargin + 452, hostEditTop, kMargin + 512, hostEditTop + editHeight);
    hostPortEdit_.Create(m_hWnd, hostPortEditRect, L"2121", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    hostPortEdit_.SetFont(g_normalFont);

    CRect hostStartRect(kMargin + 524, hostEditTop, kMargin + 604, hostEditTop + editHeight);
    hostStartButton_.Create(m_hWnd, hostStartRect, L"Start", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                             IDC_FTP_HOST_START_BUTTON);
    hostStartButton_.SetFont(g_normalFont);
    hostStartButton_.setKind(ModernButton::Kind::Primary);

    CRect hostStopRect(kMargin + 616, hostEditTop, kMargin + 696, hostEditTop + editHeight);
    hostStopButton_.Create(m_hWnd, hostStopRect, L"Stop", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                            IDC_FTP_HOST_STOP_BUTTON);
    hostStopButton_.SetFont(g_normalFont);
    hostStopButton_.setKind(ModernButton::Kind::Danger);

    CRect hostCopyAddressRect(kMargin + 708, hostEditTop, kMargin + 828, hostEditTop + editHeight);
    hostCopyAddressButton_.Create(m_hWnd, hostCopyAddressRect, L"Copy Address",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                   IDC_FTP_HOST_COPY_ADDRESS_BUTTON);
    hostCopyAddressButton_.SetFont(g_normalFont);
    hostCopyAddressButton_.setKind(ModernButton::Kind::Secondary);

    const int hostStatusTop = hostEditTop + editHeight + 8;
    const int hostStatusHeight = 54;
    CRect hostStatusDotRect(kMargin, hostStatusTop + 2, kMargin + 130, hostStatusTop + 20);
    hostStatusDot_.Create(m_hWnd, hostStatusDotRect, L"Server", WS_CHILD | WS_VISIBLE);

    CRect hostStatusRect(kMargin, hostStatusTop + 20, clientRect.right - kMargin, hostStatusTop + hostStatusHeight);
    hostStatusLabel_.Create(m_hWnd, hostStatusRect, L"Server not running.", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX);
    hostStatusLabel_.SetFont(g_normalFont);

    browseListTop_ = hostStatusTop + hostStatusHeight + 40;

    CRect browseCapRect(kMargin, browseListTop_ - 46, kMargin + 400, browseListTop_ - 26);
    browseCaption_.Create(m_hWnd, browseCapRect, L"Remote Directory", WS_CHILD | WS_VISIBLE);
    browseCaption_.SetFont(g_normalFont);

    CRect localCapRect(kMargin + 300, browseListTop_ - 46, kMargin + 420, browseListTop_ - 28);
    localPathCaption_.Create(m_hWnd, localCapRect, L"Local Path", WS_CHILD | WS_VISIBLE);
    localPathCaption_.SetFont(g_normalFont);
    CRect localEditRect(kMargin + 300, browseListTop_ - 26, kMargin + 560, browseListTop_ - 2);
    localPathEdit_.Create(m_hWnd, localEditRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    localPathEdit_.SetFont(g_normalFont);

    CRect uploadRect(kMargin + 570, browseListTop_ - 26, kMargin + 660, browseListTop_ - 2);
    uploadButton_.Create(m_hWnd, uploadRect, L"Upload", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                          IDC_FTP_UPLOAD_BUTTON);
    uploadButton_.SetFont(g_normalFont);
    uploadButton_.setKind(ModernButton::Kind::Primary);

    CRect downloadRect(kMargin + 672, browseListTop_ - 26, kMargin + 762, browseListTop_ - 2);
    downloadButton_.Create(m_hWnd, downloadRect, L"Download", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                            IDC_FTP_DOWNLOAD_BUTTON);
    downloadButton_.SetFont(g_normalFont);
    downloadButton_.setKind(ModernButton::Kind::Secondary);

    CRect refreshRect(kMargin + 774, browseListTop_ - 26, kMargin + 864, browseListTop_ - 2);
    refreshBrowseButton_.Create(m_hWnd, refreshRect, L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                 IDC_FTP_REFRESH_BUTTON);
    refreshBrowseButton_.SetFont(g_normalFont);
    refreshBrowseButton_.setKind(ModernButton::Kind::Subtle);

    const int browseListBottom = browseListTop_ + 170;
    CRect browseListRect(kMargin, browseListTop_, clientRect.right - kMargin, browseListBottom);
    browseListView_.Create(m_hWnd, browseListRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                            0, IDC_FTP_BROWSE_LISTVIEW);
    browseListView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    browseListView_.SetFont(g_normalFont);
    theme::ApplyListViewTheme(browseListView_);
    browseListView_.InsertColumn(0, L"Name", LVCFMT_LEFT, 300);
    browseListView_.InsertColumn(1, L"Type", LVCFMT_LEFT, 100);
    browseListView_.InsertColumn(2, L"Size", LVCFMT_LEFT, 120);

    // Accelerator section (mirror candidates + LAN caching proxy) - stacked
    // below the FTP browse pane on this same page rather than a separate
    // sidebar tab, see FtpView's class comment.
    const int accelTop = browseListBottom + 28;
    advancedToolsTop_ = accelTop;
    CRect accelCapRect(kMargin, accelTop, kMargin + 300, accelTop + 24);
    acceleratorSectionCaption_.Create(m_hWnd, accelCapRect, L"Accelerator", WS_CHILD | WS_VISIBLE);
    acceleratorSectionCaption_.SetFont(theme::TitleFont());

    CRect accelDotRect(kMargin + 140, accelTop + 4, kMargin + 300, accelTop + 22);
    acceleratorSectionDot_.Create(m_hWnd, accelDotRect, L"CDN Status", WS_CHILD | WS_VISIBLE);

    int ax = kMargin;
    const int mirrorInputTop = accelTop + 34;
    const int mirrorEditTop = mirrorInputTop + captionHeight;

    auto makeMirrorField = [&](CStatic& caption, CEdit& edit, const wchar_t* label, int width) {
        CRect capRect(ax, mirrorInputTop, ax + width, mirrorInputTop + captionHeight);
        caption.Create(m_hWnd, capRect, label, WS_CHILD | WS_VISIBLE);
        caption.SetFont(g_normalFont);

        CRect editRect(ax, mirrorEditTop, ax + width, mirrorEditTop + editHeight);
        edit.Create(m_hWnd, editRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
        edit.SetFont(g_normalFont);

        ax += width + 10;
    };

    makeMirrorField(mirrorHostCaption_, mirrorHostEdit_, L"Host Pattern (e.g. *.example.com)", 260);
    makeMirrorField(mirrorIpCaption_, mirrorIpEdit_, L"Mirror IP", 130);
    makeMirrorField(mirrorLabelCaption_, mirrorLabelEdit_, L"Label", 130);

    CRect addMirrorRect(ax, mirrorEditTop, ax + 130, mirrorEditTop + editHeight);
    addMirrorButton_.Create(m_hWnd, addMirrorRect, L"Add Mirror", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                             IDC_ACCEL_ADD_MIRROR_BUTTON);
    addMirrorButton_.SetFont(g_normalFont);
    addMirrorButton_.setKind(ModernButton::Kind::Primary);

    const int mirrorRow2 = mirrorEditTop + editHeight + 12;
    CRect measureRect(kMargin, mirrorRow2, kMargin + 130, mirrorRow2 + editHeight);
    measureButton_.Create(m_hWnd, measureRect, L"Measure All", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                           IDC_ACCEL_MEASURE_BUTTON);
    measureButton_.SetFont(g_normalFont);
    measureButton_.setKind(ModernButton::Kind::Secondary);

    CRect removeMirrorRect(kMargin + 142, mirrorRow2, kMargin + 252, mirrorRow2 + editHeight);
    removeMirrorButton_.Create(m_hWnd, removeMirrorRect, L"Remove", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                0, IDC_ACCEL_REMOVE_MIRROR_BUTTON);
    removeMirrorButton_.SetFont(g_normalFont);
    removeMirrorButton_.setKind(ModernButton::Kind::Danger);

    CRect mirrorsStatusRect(kMargin + 264, mirrorRow2 + 3, kMargin + 900, mirrorRow2 + editHeight);
    mirrorsStatusLabel_.Create(m_hWnd, mirrorsStatusRect, L"", WS_CHILD | WS_VISIBLE);
    mirrorsStatusLabel_.SetFont(g_normalFont);

    const int mirrorsListTop = mirrorRow2 + editHeight + 8;
    const int mirrorsListBottom = mirrorsListTop + 130;
    CRect mirrorsListRect(kMargin, mirrorsListTop, clientRect.right - kMargin, mirrorsListBottom);
    mirrorsListView_.Create(m_hWnd, mirrorsListRect, L"",
                             WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 0,
                             IDC_ACCEL_MIRRORS_LISTVIEW);
    mirrorsListView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    mirrorsListView_.SetFont(g_normalFont);
    theme::ApplyListViewTheme(mirrorsListView_);
    mirrorsListView_.InsertColumn(0, L"Host Pattern", LVCFMT_LEFT, 220);
    mirrorsListView_.InsertColumn(1, L"Mirror IP", LVCFMT_LEFT, 130);
    mirrorsListView_.InsertColumn(2, L"Label", LVCFMT_LEFT, 150);
    mirrorsListView_.InsertColumn(3, L"Last RTT", LVCFMT_LEFT, 90);

    // "LAN Console Proxy" section: point a console's network proxy setting at this machine's address:port to get
    // console-tracking, CDN URL logging, response caching, CDN-mirror substitution and local-library serving all
    // from one listener - see WinsockCachingProxyServer. Its own position is decided by relayoutMitmAndCdnSections()
    // (an admin sees it here, under the mirror list; a signed-out user, who has none of the sections above, sees it
    // right under the page header) - the controls are created here and moved into place by refresh() at the end of
    // OnCreate.
    const int httpProxyTop = mirrorsListBottom + 20;
    CRect httpProxySectionCapRect(kMargin, httpProxyTop, kMargin + 300, httpProxyTop + 20);
    httpProxySectionCaption_.Create(m_hWnd, httpProxySectionCapRect, L"LAN Console Proxy", WS_CHILD | WS_VISIBLE);
    httpProxySectionCaption_.SetFont(theme::BodyFontBold());

    const int httpProxyRowTop = httpProxyTop + 26;
    const int httpProxyEditTop = httpProxyRowTop + captionHeight;

    CRect httpProxyIpLabelRect(kMargin, httpProxyRowTop, kMargin + 150, httpProxyRowTop + captionHeight);
    httpProxyIpLabel_.Create(m_hWnd, httpProxyIpLabelRect, L"Your PC IP Address", WS_CHILD | WS_VISIBLE);
    httpProxyIpLabel_.SetFont(g_normalFont);

    CRect httpProxyIpDisplayRect(kMargin, httpProxyEditTop, kMargin + 180, httpProxyEditTop + editHeight);
    httpProxyIpDisplay_.Create(m_hWnd, httpProxyIpDisplayRect, L"",
                                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    httpProxyIpDisplay_.SetFont(g_normalFont);
    {
        const auto detected = gth::network::NetworkInfoHelper::GetLocalNetworkInfo();
        httpProxyIpDisplay_.SetWindowText(detected.isValid ? detected.ipAddress.c_str() : L"127.0.0.1");
    }

    CRect httpProxyPortCapRect(kMargin + 192, httpProxyRowTop, kMargin + 252, httpProxyRowTop + captionHeight);
    httpProxyPortCaption_.Create(m_hWnd, httpProxyPortCapRect, L"Port", WS_CHILD | WS_VISIBLE);
    httpProxyPortCaption_.SetFont(g_normalFont);
    CRect httpProxyPortEditRect(kMargin + 192, httpProxyEditTop, kMargin + 252, httpProxyEditTop + editHeight);
    httpProxyPortEdit_.Create(m_hWnd, httpProxyPortEditRect, L"8080", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    httpProxyPortEdit_.SetFont(g_normalFont);

    CRect proxyStartRect(kMargin + 264, httpProxyEditTop, kMargin + 344, httpProxyEditTop + editHeight);
    proxyStartButton_.Create(m_hWnd, proxyStartRect, L"Start", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                             IDC_PROXY_START_BUTTON);
    proxyStartButton_.SetFont(g_normalFont);
    proxyStartButton_.setKind(ModernButton::Kind::Primary);

    CRect proxyStopRect(kMargin + 356, httpProxyEditTop, kMargin + 436, httpProxyEditTop + editHeight);
    proxyStopButton_.Create(m_hWnd, proxyStopRect, L"Stop", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                            IDC_PROXY_STOP_BUTTON);
    proxyStopButton_.SetFont(g_normalFont);
    proxyStopButton_.setKind(ModernButton::Kind::Danger);

    // Second field row: the on-disk response cache (shared by every "+ Add Console" instance too).
    const int cacheRowTop = httpProxyEditTop + editHeight + ftplayout::kProxyRowGap;
    const int cacheEditTop = cacheRowTop + captionHeight;

    CRect cacheFolderCapRect(kMargin, cacheRowTop, kMargin + 200, cacheRowTop + captionHeight);
    cacheFolderCaption_.Create(m_hWnd, cacheFolderCapRect, L"Cache Folder (optional)", WS_CHILD | WS_VISIBLE);
    cacheFolderCaption_.SetFont(g_normalFont);
    CRect cacheFolderEditRect(kMargin, cacheEditTop, kMargin + 340, cacheEditTop + editHeight);
    cacheFolderEdit_.Create(m_hWnd, cacheFolderEditRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    cacheFolderEdit_.SetFont(g_normalFont);

    CRect cacheBrowseRect(kMargin + 350, cacheEditTop, kMargin + 440, cacheEditTop + editHeight);
    cacheBrowseButton_.Create(m_hWnd, cacheBrowseRect, L"Browse...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                               0, IDC_ACCEL_CACHE_BROWSE_BUTTON);
    cacheBrowseButton_.SetFont(g_normalFont);
    cacheBrowseButton_.setKind(ModernButton::Kind::Subtle);

    CRect clearCacheRect(kMargin + 452, cacheEditTop, kMargin + 562, cacheEditTop + editHeight);
    clearCacheButton_.Create(m_hWnd, clearCacheRect, L"Clear Cache", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                              0, IDC_ACCEL_CLEAR_CACHE_BUTTON);
    clearCacheButton_.SetFont(g_normalFont);
    clearCacheButton_.setKind(ModernButton::Kind::Subtle);

    const int httpProxyStatusTop = cacheEditTop + editHeight + ftplayout::kProxyFieldGap;
    const int httpProxyStatusHeight = ftplayout::kProxyStatusHeight;
    CRect httpProxyStatusDotRect(kMargin, httpProxyStatusTop + 2, kMargin + 150, httpProxyStatusTop + 20);
    httpProxyStatusDot_.Create(m_hWnd, httpProxyStatusDotRect, L"Proxy", WS_CHILD | WS_VISIBLE);

    CRect httpProxyStatusRect(kMargin, httpProxyStatusTop + 20, clientRect.right - kMargin, httpProxyStatusTop + httpProxyStatusHeight);
    httpProxyStatusLabel_.Create(m_hWnd, httpProxyStatusRect, L"Proxy not running. Configure console proxy to: [Your IP]:8080", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX);
    httpProxyStatusLabel_.SetFont(g_normalFont);

    // Adds another independent MITM listener below (own IP/port/Start/Stop/
    // status) for a second, third, ... console - see MitmInstance. Exact
    // position doesn't matter here since relayoutMitmAndCdnSections() (run
    // via refresh() at the end of OnCreate) immediately places this and
    // everything below it correctly.
    CRect addMitmRect(kMargin, httpProxyStatusTop + httpProxyStatusHeight + 12, kMargin + 130,
                       httpProxyStatusTop + httpProxyStatusHeight + 40);
    addMitmInstanceButton_.Create(m_hWnd, addMitmRect, L"+ Add Console", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   0, IDC_MITM_ADD_BUTTON);
    addMitmInstanceButton_.SetFont(g_normalFont);
    addMitmInstanceButton_.setKind(ModernButton::Kind::Subtle);

    // CDN URL Log section - like PSX Download Helper
    const int cdnTop = httpProxyStatusTop + httpProxyStatusHeight + 28;
    // 300px clipped "CDN URL Log" to "CDN URL Loa" under the title font -
    // same section-caption pattern as the other sections, just needed more
    // room than they did.
    CRect cdnCapRect(kMargin, cdnTop, kMargin + 400, cdnTop + 24);
    cdnSectionCaption_.Create(m_hWnd, cdnCapRect, L"CDN URL Log", WS_CHILD | WS_VISIBLE);
    cdnSectionCaption_.SetFont(theme::TitleFont());

    const int cdnRowTop = cdnTop + 34;
    const int cdnEditTop = cdnRowTop + captionHeight;

    CRect cdnUrlEditRect(kMargin, cdnEditTop, kMargin + 500, cdnEditTop + editHeight);
    cdnUrlEdit_.Create(m_hWnd, cdnUrlEditRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    cdnUrlEdit_.SetFont(g_normalFont);

    CRect addCdnRect(kMargin + 512, cdnEditTop, kMargin + 612, cdnEditTop + editHeight);
    cdnUrlAddButton_.Create(m_hWnd, addCdnRect, L"Add URL", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                             IDC_CDN_URL_ADD_BUTTON);
    cdnUrlAddButton_.SetFont(g_normalFont);
    cdnUrlAddButton_.setKind(ModernButton::Kind::Primary);

    // CDN URL list
    const int cdnListTop = cdnEditTop + editHeight + 12;
    const int cdnListBottom = cdnListTop + 150;
    CRect cdnListRect(kMargin, cdnListTop, clientRect.right - kMargin, cdnListBottom);
    cdnUrlListView_.Create(m_hWnd, cdnListRect, L"",
                            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 0,
                            IDC_CDN_URL_LISTVIEW);
    cdnUrlListView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    cdnUrlListView_.SetFont(g_normalFont);
    theme::ApplyListViewTheme(cdnUrlListView_);
    cdnUrlListView_.InsertColumn(0, L"URL", LVCFMT_LEFT, 280);
    cdnUrlListView_.InsertColumn(1, L"Mark", LVCFMT_LEFT, 120);
    cdnUrlListView_.InsertColumn(2, L"Local Path", LVCFMT_LEFT, 150);
    cdnUrlListView_.InsertColumn(3, L"Source", LVCFMT_LEFT, 70);
    cdnUrlListView_.InsertColumn(4, L"Captured", LVCFMT_LEFT, 100);

    // Mark and local path fields
    const int markRowTop = cdnListBottom + 12;
    const int markEditTop = markRowTop + captionHeight;
    CRect markCapRect(kMargin, markRowTop, kMargin + 80, markRowTop + captionHeight);
    cdnUrlMarkCaption_.Create(m_hWnd, markCapRect, L"Mark", WS_CHILD | WS_VISIBLE);
    cdnUrlMarkCaption_.SetFont(g_normalFont);
    CRect markEditRect(kMargin, markEditTop, kMargin + 300, markEditTop + editHeight);
    cdnUrlMarkEdit_.Create(m_hWnd, markEditRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0,
                           IDC_CDN_URL_MARK_EDIT);
    cdnUrlMarkEdit_.SetFont(g_normalFont);

    CRect pathCapRect(kMargin + 312, markRowTop, kMargin + 412, markRowTop + captionHeight);
    cdnUrlLocalPathCaption_.Create(m_hWnd, pathCapRect, L"Local Path", WS_CHILD | WS_VISIBLE);
    cdnUrlLocalPathCaption_.SetFont(g_normalFont);
    CRect pathEditRect(kMargin + 312, markEditTop, kMargin + 650, markEditTop + editHeight);
    cdnUrlLocalPathEdit_.Create(m_hWnd, pathEditRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0,
                                IDC_CDN_URL_LOCAL_PATH_EDIT);
    cdnUrlLocalPathEdit_.SetFont(g_normalFont);

    CRect browseCdnRect(kMargin + 662, markEditTop, kMargin + 752, markEditTop + editHeight);
    cdnUrlBrowseButton_.Create(m_hWnd, browseCdnRect, L"Browse...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                IDC_CDN_URL_BROWSE_BUTTON);
    cdnUrlBrowseButton_.SetFont(g_normalFont);
    cdnUrlBrowseButton_.setKind(ModernButton::Kind::Subtle);

    CRect removeCdnRect(kMargin + 764, markEditTop, kMargin + 854, markEditTop + editHeight);
    cdnUrlRemoveButton_.Create(m_hWnd, removeCdnRect, L"Remove", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                IDC_CDN_URL_REMOVE_BUTTON);
    cdnUrlRemoveButton_.SetFont(g_normalFont);
    cdnUrlRemoveButton_.setKind(ModernButton::Kind::Danger);

    // Copies the URL selected in the log above (see OnCopyUrlClick) - was
    // wired to a message handler but never actually Create()'d, so there
    // was no way to get a captured URL out of the list at all.
    CRect copyCdnRect(kMargin + 866, markEditTop, kMargin + 956, markEditTop + editHeight);
    cdnUrlCopyButton_.Create(m_hWnd, copyCdnRect, L"Copy URL", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                              IDC_CDN_URL_COPY_BUTTON);
    cdnUrlCopyButton_.SetFont(g_normalFont);
    cdnUrlCopyButton_.setKind(ModernButton::Kind::Subtle);

    // Layout baselines for applyAccessControl()/relayoutMitmAndCdnSections():
    // "full" is exactly where OnCreate just put the LAN Console Proxy section;
    // "compact" is right under the page header, matching where section 1
    // (the FTP connection manager) currently starts - a signed-out user has
    // no connection manager, host or mirror sections, so the proxy (which is
    // for everyone) takes the top of the page instead. Content height itself
    // isn't captured here anymore - relayoutMitmAndCdnSections() (run via
    // refresh() immediately below) computes and sets it fresh, since it now
    // depends on mitmInstances_.size() too.
    httpProxyTopFull_ = httpProxyTop;
    httpProxyTopCompact_ = inputTop;

    refresh();
    SetTimer(kLiveRefreshTimerId, kLiveRefreshIntervalMs, nullptr);
    return 0;
}

LRESULT FtpView::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (wParam != kLiveRefreshTimerId) return 0;

    refreshCdnUrlsList();

    if (acceleratorService_) {
        const bool running = acceleratorService_->isRunning();
        if (running != lastKnownProxyRunning_) {
            lastKnownProxyRunning_ = running;
            if (!running) {
                // The proxy died (or was never actually reachable) after the
                // Start click set the dot green -- without this, green was
                // permanent regardless of what happened afterward.
                httpProxyStatusDot_.setState(StatusDot::State::Warning);
                httpProxyStatusLabel_.SetWindowText(
                    L"Proxy stopped unexpectedly. Click Start to restart it.");
            }
        }
    }

    // Same "did it die unexpectedly" check as above, per extra console.
    for (auto& instance : mitmInstances_) {
        if (!instance->service) continue;
        const bool running = instance->service->isRunning();
        if (running != instance->lastKnownRunning) {
            instance->lastKnownRunning = running;
            if (!running) {
                instance->statusDot.setState(StatusDot::State::Warning);
                instance->statusLabel.SetWindowText(L"Stopped unexpectedly.");
            }
        }
    }
    return 0;
}

LRESULT FtpView::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
    if (advancedToolsButton_.IsWindow()) {
        relayoutMitmAndCdnSections();
    }
    scroll_.onSize();
    return 0;
}

LRESULT FtpView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorStatic(wParam); }
LRESULT FtpView::OnCtlColorEdit(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorEdit(wParam); }

std::wstring FtpView::getEditText(CEdit& edit) {
    const int len = edit.GetWindowTextLength();
    std::wstring text(static_cast<size_t>(len), L'\0');
    if (len > 0) edit.GetWindowText(text.data(), len + 1);
    return text;
}

void FtpView::applyAccessControl(bool isAdmin) {
    SetControlsVisible(
        {
            &nameCaption_, &nameEdit_, &hostCaption_, &hostEdit_, &portCaption_, &portEdit_, &protocolCaption_,
            &protocolCombo_, &userCaption_, &userEdit_, &passCaption_, &passEdit_, &pathCaption_, &pathEdit_,
            &addButton_, &connectButton_, &disconnectButton_, &removeButton_, &statusDot_, &statusLabel_,
            &connectionsListView_,

            &hostSectionCaption_, &hostFolderCaption_, &hostFolderEdit_, &hostBrowseButton_, &hostPortCaption_,
            &hostPortEdit_, &hostStartButton_, &hostStopButton_, &hostCopyAddressButton_, &hostStatusDot_,
            &hostStatusLabel_,

            &browseCaption_, &localPathCaption_, &localPathEdit_, &uploadButton_, &downloadButton_,
            &refreshBrowseButton_, &browseListView_,

            &acceleratorSectionCaption_, &acceleratorSectionDot_, &mirrorHostCaption_, &mirrorHostEdit_,
            &mirrorIpCaption_, &mirrorIpEdit_, &mirrorLabelCaption_, &mirrorLabelEdit_, &addMirrorButton_,
            &measureButton_, &removeMirrorButton_, &mirrorsStatusLabel_, &mirrorsListView_,
            // The LAN Console Proxy section (httpProxySectionCaption_ ... httpProxyStatusLabel_) is deliberately NOT
            // here: it is for every user, and is only moved (relayoutMitmAndCdnSections) when the admin-only
            // sections around it are hidden.
        },
        isAdmin);

    sections45Compact_ = !isAdmin;
    relayoutMitmAndCdnSections();
    applyAdvancedToolsVisibility();
    Invalidate();
}

// Absolute-position layout for everything from the LAN Console Proxy section
// onward, mirroring the arithmetic OnCreate used to lay these controls out
// the first time (same kMargin/kFieldCaptionHeight/kFieldEditHeight
// constants), except every Y-coordinate is recomputed fresh here rather
// than baked into one-time local variables - required because two things
// can now change independently at runtime: admin state (which of
// httpProxyTopFull_/httpProxyTopCompact_ is the base) and
// mitmInstances_.size() (how many extra rows sit between the original
// section and the CDN URL Log). A delta-shift from a cached "old position"
// (the previous approach) can't account for the section's height itself
// changing, only its position - hence recomputing absolute targets and
// MoveWindow-ing everything to them on every call.
void FtpView::relayoutMitmAndCdnSections() {
    CRect clientRect;
    GetClientRect(&clientRect);

    const int mitmTop = sections45Compact_ ? httpProxyTopCompact_ : httpProxyTopFull_;

    const auto moveTo = [this](CWindow& ctrl, int left, int top, int width, int height) {
        if (ctrl.IsWindow()) ctrl.MoveWindow(left, top, width, height, FALSE);
    };

    // Original section's own rows (caption, IP/port fields + Start/Stop, then Cache Folder + Browse/Clear Cache) -
    // matches OnCreate's httpProxyRowTop/httpProxyEditTop/cacheRowTop/cacheEditTop chain exactly (see FtpLayout.h).
    const int rowTop = mitmTop + ftplayout::kProxyCaptionRow;
    const int editTop = rowTop + kFieldCaptionHeight;
    moveTo(httpProxySectionCaption_, kMargin, mitmTop, 400, 20);
    moveTo(httpProxyIpLabel_, kMargin, rowTop, 150, kFieldCaptionHeight);
    moveTo(httpProxyIpDisplay_, kMargin, editTop, 180, kFieldEditHeight);
    moveTo(httpProxyPortCaption_, kMargin + 192, rowTop, 60, kFieldCaptionHeight);
    moveTo(httpProxyPortEdit_, kMargin + 192, editTop, 60, kFieldEditHeight);
    moveTo(proxyStartButton_, kMargin + 264, editTop, 80, kFieldEditHeight);
    moveTo(proxyStopButton_, kMargin + 356, editTop, 80, kFieldEditHeight);

    const int cacheRowTop = editTop + kFieldEditHeight + ftplayout::kProxyRowGap;
    const int cacheEditTop = cacheRowTop + kFieldCaptionHeight;
    moveTo(cacheFolderCaption_, kMargin, cacheRowTop, 200, kFieldCaptionHeight);
    moveTo(cacheFolderEdit_, kMargin, cacheEditTop, 340, kFieldEditHeight);
    moveTo(cacheBrowseButton_, kMargin + 350, cacheEditTop, 90, kFieldEditHeight);
    moveTo(clearCacheButton_, kMargin + 452, cacheEditTop, 110, kFieldEditHeight);

    const int statusTop = cacheEditTop + kFieldEditHeight + ftplayout::kProxyFieldGap;
    constexpr int kStatusHeight = ftplayout::kProxyStatusHeight;
    moveTo(httpProxyStatusDot_, kMargin, statusTop + 2, 150, 18);
    moveTo(httpProxyStatusLabel_, kMargin, statusTop + 20, clientRect.right - kMargin - kMargin, kStatusHeight - 20);

    // Dynamic instance rows, each aligned to the same IP/Port columns as
    // the row above so the lack of a per-row caption still reads clearly.
    int instanceRowTop = statusTop + kStatusHeight + 12;
    for (auto& instance : mitmInstances_) {
        moveTo(instance->ipEdit, kMargin, instanceRowTop, 180, kFieldEditHeight);
        moveTo(instance->portEdit, kMargin + 192, instanceRowTop, 60, kFieldEditHeight);
        moveTo(instance->startButton, kMargin + 264, instanceRowTop, 70, kFieldEditHeight);
        moveTo(instance->stopButton, kMargin + 340, instanceRowTop, 70, kFieldEditHeight);
        moveTo(instance->removeButton, kMargin + 416, instanceRowTop, 30, kFieldEditHeight);
        moveTo(instance->statusDot, kMargin + 456, instanceRowTop + 3, 90, 18);
        moveTo(instance->statusLabel, kMargin + 552, instanceRowTop + 3, clientRect.right - kMargin - (kMargin + 552),
               18);
        instanceRowTop += kMitmInstanceRowHeight;
    }

    const int addButtonTop = instanceRowTop + 4;
    constexpr int kAddButtonHeight = 28;
    moveTo(addMitmInstanceButton_, kMargin, addButtonTop, 130, kAddButtonHeight);

    // CDN URL Log section - same chain OnCreate uses, just based on cdnTop
    // computed from the (now variable-height) MITM section above instead
    // of a fixed offset.
    const int cdnTop = addButtonTop + kAddButtonHeight + 20;
    moveTo(cdnSectionCaption_, kMargin, cdnTop, 400, 24);
    const int cdnRowTop = cdnTop + 34;
    const int cdnEditTop = cdnRowTop + kFieldCaptionHeight;
    moveTo(cdnUrlEdit_, kMargin, cdnEditTop, 500, kFieldEditHeight);
    moveTo(cdnUrlAddButton_, kMargin + 512, cdnEditTop, 100, kFieldEditHeight);

    const int cdnListTop = cdnEditTop + kFieldEditHeight + 12;
    constexpr int kCdnListHeight = 150;
    const int cdnListBottom = cdnListTop + kCdnListHeight;
    moveTo(cdnUrlListView_, kMargin, cdnListTop, clientRect.right - kMargin - kMargin, kCdnListHeight);

    const int markRowTop = cdnListBottom + 12;
    const int markEditTop = markRowTop + kFieldCaptionHeight;
    moveTo(cdnUrlMarkCaption_, kMargin, markRowTop, 80, kFieldCaptionHeight);
    moveTo(cdnUrlMarkEdit_, kMargin, markEditTop, 300, kFieldEditHeight);
    moveTo(cdnUrlLocalPathCaption_, kMargin + 312, markRowTop, 100, kFieldCaptionHeight);
    moveTo(cdnUrlLocalPathEdit_, kMargin + 312, markEditTop, 338, kFieldEditHeight);
    moveTo(cdnUrlBrowseButton_, kMargin + 662, markEditTop, 90, kFieldEditHeight);
    moveTo(cdnUrlRemoveButton_, kMargin + 764, markEditTop, 90, kFieldEditHeight);
    moveTo(cdnUrlCopyButton_, kMargin + 866, markEditTop, 90, kFieldEditHeight);

    if (advancedToolsExpanded_) {
        scroll_.setContentHeight(markEditTop + kFieldEditHeight + kMargin);
    } else {
        scroll_.setContentHeight(sections45Compact_ ? 92 : std::max(100, advancedToolsTop_));
    }
    applyAdvancedToolsVisibility();
}

void FtpView::applyAdvancedToolsVisibility() {
    if (!advancedToolsButton_.IsWindow()) return;
    advancedToolsButton_.SetWindowText(advancedToolsExpanded_ ? L"Hide network tools  \x25BE"
                                                             : L"More network tools  \x25B8");
    const bool showAdminTools = advancedToolsExpanded_ && !sections45Compact_;
    const bool showSharedTools = advancedToolsExpanded_;
    const auto setVisible = [](CWindow& control, bool visible) {
        if (control.IsWindow()) control.ShowWindow(visible ? SW_SHOW : SW_HIDE);
    };
    for (CWindow* control : {static_cast<CWindow*>(&acceleratorSectionCaption_),
                             static_cast<CWindow*>(&acceleratorSectionDot_),
                             static_cast<CWindow*>(&mirrorHostCaption_), static_cast<CWindow*>(&mirrorHostEdit_),
                             static_cast<CWindow*>(&mirrorIpCaption_), static_cast<CWindow*>(&mirrorIpEdit_),
                             static_cast<CWindow*>(&mirrorLabelCaption_), static_cast<CWindow*>(&mirrorLabelEdit_),
                             static_cast<CWindow*>(&addMirrorButton_), static_cast<CWindow*>(&measureButton_),
                             static_cast<CWindow*>(&removeMirrorButton_), static_cast<CWindow*>(&mirrorsStatusLabel_),
                             static_cast<CWindow*>(&mirrorsListView_)}) {
        setVisible(*control, showAdminTools);
    }
    for (CWindow* control : {static_cast<CWindow*>(&httpProxySectionCaption_),
                             static_cast<CWindow*>(&httpProxyIpLabel_), static_cast<CWindow*>(&httpProxyIpDisplay_),
                             static_cast<CWindow*>(&httpProxyPortCaption_), static_cast<CWindow*>(&httpProxyPortEdit_),
                             static_cast<CWindow*>(&proxyStartButton_), static_cast<CWindow*>(&proxyStopButton_),
                             static_cast<CWindow*>(&cacheFolderCaption_), static_cast<CWindow*>(&cacheFolderEdit_),
                             static_cast<CWindow*>(&cacheBrowseButton_), static_cast<CWindow*>(&clearCacheButton_),
                             static_cast<CWindow*>(&httpProxyStatusDot_), static_cast<CWindow*>(&httpProxyStatusLabel_),
                             static_cast<CWindow*>(&addMitmInstanceButton_), static_cast<CWindow*>(&cdnSectionCaption_),
                             static_cast<CWindow*>(&cdnUrlEdit_), static_cast<CWindow*>(&cdnUrlAddButton_),
                             static_cast<CWindow*>(&cdnUrlListView_), static_cast<CWindow*>(&cdnUrlMarkCaption_),
                             static_cast<CWindow*>(&cdnUrlMarkEdit_), static_cast<CWindow*>(&cdnUrlLocalPathCaption_),
                             static_cast<CWindow*>(&cdnUrlLocalPathEdit_), static_cast<CWindow*>(&cdnUrlBrowseButton_),
                             static_cast<CWindow*>(&cdnUrlRemoveButton_), static_cast<CWindow*>(&cdnUrlCopyButton_)}) {
        setVisible(*control, showSharedTools);
    }
    for (auto& instance : mitmInstances_) {
        for (CWindow* control : {static_cast<CWindow*>(&instance->ipEdit), static_cast<CWindow*>(&instance->portEdit),
                                 static_cast<CWindow*>(&instance->startButton), static_cast<CWindow*>(&instance->stopButton),
                                 static_cast<CWindow*>(&instance->removeButton), static_cast<CWindow*>(&instance->statusDot),
                                 static_cast<CWindow*>(&instance->statusLabel)}) {
            setVisible(*control, showSharedTools);
        }
    }
}

LRESULT FtpView::OnAdvancedToolsClick(WORD, WORD, HWND, BOOL&) {
    advancedToolsExpanded_ = !advancedToolsExpanded_;
    relayoutMitmAndCdnSections();
    scroll_.onSize();
    return 0;
}

void FtpView::refresh() {
    applyAccessControl(adminAuthService_ && adminAuthService_->isSignedIn());

    // Re-applies in case the Light/Dark toggle fired while this tab wasn't
    // visible - list view colors are baked-in control state, not something
    // repainted automatically when theme::IsDarkMode() flips.
    if (connectionsListView_.IsWindow()) theme::ApplyListViewTheme(connectionsListView_);
    if (browseListView_.IsWindow()) theme::ApplyListViewTheme(browseListView_);
    if (mirrorsListView_.IsWindow()) theme::ApplyListViewTheme(mirrorsListView_);
    if (cdnUrlListView_.IsWindow()) theme::ApplyListViewTheme(cdnUrlListView_);

    refreshConnectionsList();
    refreshBrowseList();
    refreshHostStatus();
    refreshMirrorsList();
    refreshAcceleratorStatus();
    refreshCdnUrlsList();
}

void FtpView::refreshConnectionsList() {
    if (!connectionService_ || !connectionsListView_.IsWindow()) return;

    connectionsListView_.DeleteAllItems();
    lastConnections_ = connectionService_->getAllConnections();

    int row = 0;
    for (const auto& connection : lastConnections_) {
        connectionsListView_.InsertItem(row, Utf8ToWide(connection.name).c_str());
        connectionsListView_.SetItemText(row, 1, Utf8ToWide(connection.host).c_str());
        connectionsListView_.SetItemText(row, 2, std::to_wstring(connection.port).c_str());
        connectionsListView_.SetItemText(row, 3, Utf8ToWide(gth::core::toString(connection.protocol)).c_str());
        connectionsListView_.SetItemText(row, 4, Utf8ToWide(connection.username).c_str());
        row++;
    }
}

void FtpView::refreshBrowseList() {
    if (!transferService_ || !browseListView_.IsWindow()) return;

    if (statusDot_.IsWindow()) {
        statusDot_.setState(transferService_->isConnected() ? StatusDot::State::On : StatusDot::State::Off);
    }

    browseListView_.DeleteAllItems();
    lastEntries_.clear();

    if (!transferService_->isConnected()) return;

    lastEntries_ = transferService_->browse(currentBrowsePath_);
    int row = 0;
    for (const auto& entry : lastEntries_) {
        browseListView_.InsertItem(row, Utf8ToWide(entry.name).c_str());
        browseListView_.SetItemText(row, 1, entry.isDirectory ? L"Folder" : L"File");
        browseListView_.SetItemText(row, 2, std::to_wstring(entry.sizeBytes).c_str());
        row++;
    }
}

void FtpView::refreshHostStatus() {
    if (!ftpServerService_ || !hostStatusLabel_.IsWindow()) return;

    if (hostStatusDot_.IsWindow()) {
        hostStatusDot_.setState(ftpServerService_->isRunning() ? StatusDot::State::On : StatusDot::State::Off);
    }

    if (!ftpServerService_->isRunning()) {
        // Not running yet - still show the address the server *would*
        // bind to (this machine's LAN IP + whatever port is currently
        // typed into hostPortEdit_), so there's always something to give a
        // console/other PC instead of only finding out after clicking
        // Start. One port here is enough for several consoles at once -
        // WinsockFtpServer accepts and services each client connection on
        // its own thread, so this isn't "one console per port."
        const std::wstring portText = getEditText(hostPortEdit_);
        int previewPort = 2121;
        try {
            previewPort = portText.empty() ? 2121 : std::stoi(portText);
        } catch (...) {
            // Leave previewPort at the default - an invalid port here is
            // just a preview, OnHostStartClick is what actually validates
            // it when the user clicks Start.
        }

        const auto info = gth::network::NetworkInfoHelper::GetLocalNetworkInfo(previewPort);
        if (info.isValid) {
            lastHostAddress_ = info.connectionUrl;
            hostStatusLabel_.SetWindowText(
                std::format(L"Not running. When started, consoles connect to: {}", lastHostAddress_).c_str());
        } else {
            lastHostAddress_.clear();
            hostStatusLabel_.SetWindowText(L"Not running. Could not determine this machine's LAN IP.");
        }
        return;
    }

    const auto addresses = ftpServerService_->listenAddresses();
    const int port = ftpServerService_->port();

    std::wstring text =
        L"Running. On the other laptop's FTP tab, add a connection with protocol FTP and no username/password to:\r\n";
    if (addresses.empty()) {
        lastHostAddress_.clear();
        text += L"No LAN interface detected.";
    } else {
        lastHostAddress_ = std::format(L"{}:{}", Utf8ToWide(addresses[0]), port);
        for (size_t i = 0; i < addresses.size(); ++i) {
            text += std::format(L"{}:{}", Utf8ToWide(addresses[i]), port);
            if (i + 1 < addresses.size()) text += L"   ";
        }
    }
    hostStatusLabel_.SetWindowText(text.c_str());
}

std::string FtpView::selectedConnectionId() {
    const int sel = connectionsListView_.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= lastConnections_.size()) return "";
    return lastConnections_[static_cast<size_t>(sel)].connectionId;
}

LRESULT FtpView::OnAddClick(WORD, WORD, HWND, BOOL&) {
    if (!connectionService_) return 0;

    const std::wstring name = getEditText(nameEdit_);
    const std::wstring host = getEditText(hostEdit_);
    const std::wstring portText = getEditText(portEdit_);
    const std::wstring user = getEditText(userEdit_);
    const std::wstring pass = getEditText(passEdit_);
    const std::wstring path = getEditText(pathEdit_);

    if (name.empty() || host.empty()) {
        statusLabel_.SetWindowText(L"Name and host are required.");
        return 0;
    }

    int port = 21;
    try {
        port = portText.empty() ? 21 : std::stoi(portText);
    } catch (...) {
        statusLabel_.SetWindowText(L"Port must be a number.");
        return 0;
    }

    gth::core::FtpProtocol protocol = gth::core::FtpProtocol::Ftp;
    const int protoSel = protocolCombo_.GetCurSel();
    if (protoSel == 1) protocol = gth::core::FtpProtocol::Ftps;
    else if (protoSel == 2) protocol = gth::core::FtpProtocol::Sftp;

    connectionService_->addConnection(WideToUtf8(name), WideToUtf8(host), port, protocol, WideToUtf8(user),
                                       WideToUtf8(pass), path.empty() ? "/" : WideToUtf8(path));

    nameEdit_.SetWindowText(L"");
    hostEdit_.SetWindowText(L"");
    portEdit_.SetWindowText(L"");
    userEdit_.SetWindowText(L"");
    passEdit_.SetWindowText(L"");
    pathEdit_.SetWindowText(L"/");

    statusLabel_.SetWindowText(L"Connection added.");
    refreshConnectionsList();
    return 0;
}

LRESULT FtpView::OnConnectClick(WORD, WORD, HWND, BOOL&) {
    if (!transferService_) return 0;

    const auto id = selectedConnectionId();
    if (id.empty()) {
        statusLabel_.SetWindowText(L"Select a connection first.");
        return 0;
    }

    statusLabel_.SetWindowText(L"Connecting...");
    ::UpdateWindow(statusLabel_.m_hWnd);

    const auto result = transferService_->connectTo(id);
    if (!result.success) {
        statusDot_.setState(StatusDot::State::Warning);
        statusLabel_.SetWindowText(std::format(L"Connect failed: {}", Utf8ToWide(result.errorMessage)).c_str());
        return 0;
    }

    currentBrowsePath_ = "/";
    statusLabel_.SetWindowText(L"Connected.");
    refreshBrowseList();
    return 0;
}

LRESULT FtpView::OnDisconnectClick(WORD, WORD, HWND, BOOL&) {
    if (!transferService_) return 0;
    transferService_->disconnect();
    statusLabel_.SetWindowText(L"Disconnected.");
    refreshBrowseList();
    return 0;
}

LRESULT FtpView::OnRemoveClick(WORD, WORD, HWND, BOOL&) {
    if (!connectionService_) return 0;

    const auto id = selectedConnectionId();
    if (id.empty()) {
        statusLabel_.SetWindowText(L"Select a connection first.");
        return 0;
    }

    connectionService_->removeConnection(id);
    statusLabel_.SetWindowText(L"Connection removed.");
    refreshConnectionsList();
    return 0;
}

LRESULT FtpView::OnUploadClick(WORD, WORD, HWND, BOOL&) {
    if (!transferService_ || !transferService_->isConnected()) {
        statusLabel_.SetWindowText(L"Connect to a server first.");
        return 0;
    }

    const std::wstring localPath = getEditText(localPathEdit_);
    if (localPath.empty()) {
        statusLabel_.SetWindowText(L"Enter a local file path to upload.");
        return 0;
    }

    // Remote destination is the current browse directory plus the local
    // file's own name - matches how FileZilla-style clients default an
    // upload target.
    const std::wstring fileName = localPath.substr(localPath.find_last_of(L"\\/") + 1);
    std::string remotePath = currentBrowsePath_;
    if (remotePath.back() != '/') remotePath += "/";
    remotePath += WideToUtf8(fileName);

    statusLabel_.SetWindowText(L"Uploading...");
    ::UpdateWindow(statusLabel_.m_hWnd);

    const auto result = transferService_->upload(WideToUtf8(localPath), remotePath, nullptr, nullptr);
    const std::wstring statusText = result.success ? L"Upload complete."
                                                   : std::format(L"Upload failed: {}", Utf8ToWide(result.errorMessage));
    statusLabel_.SetWindowText(statusText.c_str());
    refreshBrowseList();
    return 0;
}

LRESULT FtpView::OnDownloadClick(WORD, WORD, HWND, BOOL&) {
    if (!transferService_ || !transferService_->isConnected()) {
        statusLabel_.SetWindowText(L"Connect to a server first.");
        return 0;
    }

    const int sel = browseListView_.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= lastEntries_.size()) {
        statusLabel_.SetWindowText(L"Select a remote file first.");
        return 0;
    }
    const auto& entry = lastEntries_[static_cast<size_t>(sel)];
    if (entry.isDirectory) {
        statusLabel_.SetWindowText(L"Select a file, not a folder.");
        return 0;
    }

    const std::wstring localDir = getEditText(localPathEdit_);
    if (localDir.empty()) {
        statusLabel_.SetWindowText(L"Enter a local destination folder path.");
        return 0;
    }

    std::string remotePath = currentBrowsePath_;
    if (remotePath.back() != '/') remotePath += "/";
    remotePath += entry.name;

    std::wstring localPath = localDir;
    if (localPath.back() != L'\\' && localPath.back() != L'/') localPath += L"\\";
    localPath += Utf8ToWide(entry.name);

    statusLabel_.SetWindowText(L"Downloading...");
    ::UpdateWindow(statusLabel_.m_hWnd);

    const auto result = transferService_->download(remotePath, WideToUtf8(localPath), nullptr, nullptr);
    const std::wstring statusText = result.success ? L"Download complete."
                                                   : std::format(L"Download failed: {}", Utf8ToWide(result.errorMessage));
    statusLabel_.SetWindowText(statusText.c_str());
    return 0;
}

LRESULT FtpView::OnRefreshBrowseClick(WORD, WORD, HWND, BOOL&) {
    refreshBrowseList();
    statusLabel_.SetWindowText(L"Refreshed.");
    return 0;
}

LRESULT FtpView::OnHostBrowseClick(WORD, WORD, HWND, BOOL&) {
    // BIF_NEWDIALOGSTYLE (the resizable, Explorer-style folder picker) needs
    // COM initialized on this thread or SHBrowseForFolderW silently falls
    // back to the old-style dialog / can fail outright - nothing else in
    // this app initializes COM, so do it locally around this one call.
    const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitializedHere = SUCCEEDED(comResult);

    std::array<wchar_t, MAX_PATH> displayName{};
    BROWSEINFOW bi{};
    bi.hwndOwner = m_hWnd;
    bi.pszDisplayName = displayName.data();
    bi.lpszTitle = L"Choose a folder to share over FTP";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
    if (pidl != nullptr) {
        std::array<wchar_t, MAX_PATH> path{};
        if (::SHGetPathFromIDListW(pidl, path.data())) {
            hostFolderEdit_.SetWindowText(path.data());
        }
        ::CoTaskMemFree(pidl);
    }

    if (comInitializedHere) ::CoUninitialize();
    return 0;
}

LRESULT FtpView::OnHostStartClick(WORD, WORD, HWND, BOOL&) {
    if (!ftpServerService_) return 0;

    // A folder is only required because the server has to serve *some*
    // real directory - it's not required in the sense of "you must
    // deliberately choose one before connecting a console." Fall back to
    // an auto-created default instead of blocking Start, so "just get the
    // console connected" doesn't need a folder picked first.
    std::wstring folder = getEditText(hostFolderEdit_);
    if (folder.empty()) {
        folder = defaultShareFolder();
        hostFolderEdit_.SetWindowText(folder.c_str());
    }

    const std::wstring portText = getEditText(hostPortEdit_);
    int port = 2121;
    try {
        port = portText.empty() ? 2121 : std::stoi(portText);
    } catch (...) {
        hostStatusLabel_.SetWindowText(L"Port must be a number.");
        return 0;
    }

    ftpServerService_->start(WideToUtf8(folder), port);
    refreshHostStatus();
    return 0;
}

LRESULT FtpView::OnHostStopClick(WORD, WORD, HWND, BOOL&) {
    if (!ftpServerService_) return 0;
    ftpServerService_->stop();
    refreshHostStatus();
    return 0;
}

void FtpView::refreshMirrorsList() {
    if (!acceleratorService_ || !mirrorsListView_.IsWindow()) return;

    mirrorsListView_.DeleteAllItems();
    lastMirrors_ = acceleratorService_->getAllMirrors();

    int row = 0;
    for (const auto& mirror : lastMirrors_) {
        mirrorsListView_.InsertItem(row, Utf8ToWide(mirror.hostPattern).c_str());
        mirrorsListView_.SetItemText(row, 1, Utf8ToWide(mirror.ip).c_str());
        mirrorsListView_.SetItemText(row, 2, Utf8ToWide(mirror.label).c_str());
        const std::wstring rttText = mirror.lastRttMs ? std::format(L"{} ms", *mirror.lastRttMs) : L"Not measured";
        mirrorsListView_.SetItemText(row, 3, rttText.c_str());
        row++;
    }
}

void FtpView::refreshAcceleratorStatus() {
    if (!acceleratorService_ || !httpProxyStatusLabel_.IsWindow()) return;

    const bool running = acceleratorService_->isRunning();
    if (httpProxyStatusDot_.IsWindow()) {
        httpProxyStatusDot_.setState(running ? StatusDot::State::On : StatusDot::State::Off);
    }
    if (acceleratorSectionDot_.IsWindow()) {
        acceleratorSectionDot_.setState(running ? StatusDot::State::On : StatusDot::State::Off);
    }

    // A blank Cache Folder means the running instance was started with caching disabled (see
    // OnProxyStartClick/OnMitmInstanceButtonClick) - cacheStats() would just read back zeroes in
    // that case, which could be misread as "caching is on but empty" rather than "caching is off".
    std::wstring cacheLine;
    if (getEditText(cacheFolderEdit_).empty()) {
        cacheLine = L"\r\nCaching disabled (no cache folder set).";
    } else {
        const auto stats = acceleratorService_->cacheStats();
        cacheLine = std::format(L"\r\nCache: {} files, {} MB - {} hits / {} misses.", stats.entryCount,
                                stats.totalBytes / (1024 * 1024), stats.hitCount, stats.missCount);
    }

    if (!running) {
        httpProxyStatusLabel_.SetWindowText(
            std::format(L"Proxy not running. Configure console proxy to: [Your IP]:8080{}", cacheLine).c_str());
        return;
    }

    const auto addresses = acceleratorService_->listenAddresses();
    const int port = acceleratorService_->port();

    std::wstring text = L"Running. Configure console proxy to:\r\n";
    if (addresses.empty()) {
        text += L"No LAN interface detected.";
    } else {
        for (size_t i = 0; i < addresses.size(); ++i) {
            text += std::format(L"{}:{}", Utf8ToWide(addresses[i]), port);
            if (i + 1 < addresses.size()) text += L"   ";
        }
    }
    text += cacheLine;
    httpProxyStatusLabel_.SetWindowText(text.c_str());
}

std::string FtpView::selectedMirrorId() {
    const int sel = mirrorsListView_.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= lastMirrors_.size()) return "";
    return lastMirrors_[static_cast<size_t>(sel)].mirrorId;
}

LRESULT FtpView::OnAddMirrorClick(WORD, WORD, HWND, BOOL&) {
    if (!acceleratorService_) return 0;

    const std::wstring host = getEditText(mirrorHostEdit_);
    const std::wstring ip = getEditText(mirrorIpEdit_);
    const std::wstring label = getEditText(mirrorLabelEdit_);

    if (host.empty() || ip.empty()) {
        mirrorsStatusLabel_.SetWindowText(L"Host pattern and mirror IP are required.");
        return 0;
    }

    acceleratorService_->addMirror(WideToUtf8(host), WideToUtf8(ip), WideToUtf8(label));

    mirrorHostEdit_.SetWindowText(L"");
    mirrorIpEdit_.SetWindowText(L"");
    mirrorLabelEdit_.SetWindowText(L"");

    mirrorsStatusLabel_.SetWindowText(L"Mirror added.");
    refreshMirrorsList();
    return 0;
}

LRESULT FtpView::OnRemoveMirrorClick(WORD, WORD, HWND, BOOL&) {
    if (!acceleratorService_) return 0;

    const auto id = selectedMirrorId();
    if (id.empty()) {
        mirrorsStatusLabel_.SetWindowText(L"Select a mirror first.");
        return 0;
    }

    acceleratorService_->removeMirror(id);
    mirrorsStatusLabel_.SetWindowText(L"Mirror removed.");
    refreshMirrorsList();
    return 0;
}

LRESULT FtpView::OnMeasureClick(WORD, WORD, HWND, BOOL&) {
    if (!acceleratorService_) return 0;

    mirrorsStatusLabel_.SetWindowText(L"Measuring...");
    ::UpdateWindow(mirrorsStatusLabel_.m_hWnd);

    acceleratorService_->measureMirrors();
    mirrorsStatusLabel_.SetWindowText(L"Measured.");
    refreshMirrorsList();
    return 0;
}

LRESULT FtpView::OnCacheBrowseClick(WORD, WORD, HWND, BOOL&) {
    const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitializedHere = SUCCEEDED(comResult);

    std::array<wchar_t, MAX_PATH> displayName{};
    BROWSEINFOW bi{};
    bi.hwndOwner = m_hWnd;
    bi.pszDisplayName = displayName.data();
    bi.lpszTitle = L"Choose a folder for the accelerator cache";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
    if (pidl != nullptr) {
        std::array<wchar_t, MAX_PATH> path{};
        if (::SHGetPathFromIDListW(pidl, path.data())) {
            cacheFolderEdit_.SetWindowText(path.data());
        }
        ::CoTaskMemFree(pidl);
    }

    if (comInitializedHere) ::CoUninitialize();
    return 0;
}

LRESULT FtpView::OnClearCacheClick(WORD, WORD, HWND, BOOL&) {
    if (!acceleratorService_) return 0;
    acceleratorService_->clearCache();
    refreshAcceleratorStatus();
    return 0;
}

LRESULT FtpView::OnCopyUrlClick(WORD, WORD, HWND, BOOL&) {
    // Copies the URL selected in the captured CDN URL Log -- what a user
    // clicking "Copy" next to that list actually means. This used to copy
    // whatever was typed into the separate manual-add textbox instead,
    // which is a different field entirely and left the list itself with no
    // way to get a URL out of it.
    const int sel = cdnUrlListView_.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= cdnUrls_.size()) {
        mirrorsStatusLabel_.SetWindowText(L"Select a URL in the list first.");
        return 0;
    }
    copyTextToClipboard(Utf8ToWide(cdnUrls_[sel].url));
    mirrorsStatusLabel_.SetWindowText(L"URL copied to clipboard.");
    return 0;
}

LRESULT FtpView::OnCdnUrlListDblClick(int, LPNMHDR pnmh, BOOL&) {
    // Double-click-to-copy needs no extra screen real estate and doesn't
    // depend on the Copy URL button being within the visible window width
    // (it sits well to the right of the list, past 850px from the margin,
    // which a normal-width window can clip without any horizontal scroll
    // to reach it) -- this is the reliable path to actually copying a URL.
    auto* info = reinterpret_cast<NMITEMACTIVATE*>(pnmh);
    const int sel = info->iItem;
    if (sel < 0 || static_cast<size_t>(sel) >= cdnUrls_.size()) return 0;

    copyTextToClipboard(Utf8ToWide(cdnUrls_[sel].url));
    mirrorsStatusLabel_.SetWindowText(L"URL copied to clipboard.");
    return 0;
}

LRESULT FtpView::OnHostCopyAddressClick(WORD, WORD, HWND, BOOL&) {
    if (lastHostAddress_.empty()) {
        hostStatusLabel_.SetWindowText(L"No address to copy - LAN IP could not be determined.");
        return 0;
    }
    copyTextToClipboard(lastHostAddress_);
    hostStatusLabel_.SetWindowText(std::format(L"Copied to clipboard: {}", lastHostAddress_).c_str());
    return 0;
}

void FtpView::copyTextToClipboard(const std::wstring& text) {
    if (text.empty() || !::OpenClipboard(m_hWnd)) return;

    ::EmptyClipboard();
    const size_t len = text.length() + 1;
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, len * sizeof(wchar_t));
    if (!hMem) {
        ::CloseClipboard();
        return;
    }

    wchar_t* pMem = static_cast<wchar_t*>(::GlobalLock(hMem));
    if (!pMem) {
        ::GlobalFree(hMem);
        ::CloseClipboard();
        return;
    }

    wcscpy_s(pMem, len, text.c_str());
    ::GlobalUnlock(hMem);

    if (!::SetClipboardData(CF_UNICODETEXT, hMem)) {
        ::GlobalFree(hMem);
    }
    ::CloseClipboard();
}

void FtpView::refreshCdnUrlsList() {
    if (!cdnUrlService_ || !cdnUrlListView_.IsWindow()) return;

    auto fresh = cdnUrlService_->getAll();

    // The proxy thread only appends/updates rows now and then; rebuilding an identical list every
    // timer tick would just drop the user's selection and scroll position for nothing.
    bool unchanged = fresh.size() == cdnUrls_.size();
    for (size_t i = 0; unchanged && i < fresh.size(); ++i) {
        unchanged = fresh[i].cdnUrlId == cdnUrls_[i].cdnUrlId && fresh[i].url == cdnUrls_[i].url &&
                    fresh[i].markText == cdnUrls_[i].markText && fresh[i].localPath == cdnUrls_[i].localPath;
    }
    if (unchanged) return;

    const int topIndex = cdnUrlListView_.GetTopIndex();
    suppressCdnEdit_ = true;
    cdnUrlListView_.SetRedraw(FALSE);
    cdnUrlListView_.DeleteAllItems();
    cdnUrls_ = std::move(fresh);

    int row = 0;
    int reselect = -1;
    for (const auto& url : cdnUrls_) {
        cdnUrlListView_.InsertItem(row, Utf8ToWide(url.url).c_str());
        cdnUrlListView_.SetItemText(row, 1, Utf8ToWide(url.markText).c_str());
        cdnUrlListView_.SetItemText(row, 2, Utf8ToWide(url.localPath).c_str());

        // Source column: show "Local" if localPath is set, "Download" otherwise
        const std::wstring source = url.localPath.empty() ? L"Download" : L"Local";
        cdnUrlListView_.SetItemText(row, 3, source.c_str());

        std::wstring date = Utf8ToWide(url.capturedAt.substr(0, 10));
        cdnUrlListView_.SetItemText(row, 4, date.c_str());
        if (!editingCdnUrlId_.empty() && url.cdnUrlId == editingCdnUrlId_) reselect = row;
        row++;
    }

    if (reselect >= 0) {
        cdnUrlListView_.SetItemState(reselect, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    } else {
        editingCdnUrlId_.clear();  // the row being edited was removed
    }
    if (topIndex > 0 && !cdnUrls_.empty()) {
        cdnUrlListView_.EnsureVisible(std::min<int>(static_cast<int>(cdnUrls_.size()) - 1,
                                                    topIndex + cdnUrlListView_.GetCountPerPage() - 1), FALSE);
    }
    cdnUrlListView_.SetRedraw(TRUE);
    suppressCdnEdit_ = false;
}

LRESULT FtpView::OnAddCdnUrlClick(WORD, WORD, HWND, BOOL&) {
    if (!cdnUrlService_) return 0;

    const std::wstring urlText = getEditText(cdnUrlEdit_);
    if (urlText.empty()) {
        mirrorsStatusLabel_.SetWindowText(L"Enter a URL first.");
        return 0;
    }

    cdnUrlService_->captureUrl(WideToUtf8(urlText), "", true);
    cdnUrlEdit_.SetWindowText(L"");
    mirrorsStatusLabel_.SetWindowText(L"URL captured.");
    refreshCdnUrlsList();
    return 0;
}

LRESULT FtpView::OnCdnUrlBrowseClick(WORD, WORD, HWND, BOOL&) {
    const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitializedHere = SUCCEEDED(comResult);

    std::array<wchar_t, MAX_PATH> displayName{};
    BROWSEINFOW bi{};
    bi.hwndOwner = m_hWnd;
    bi.pszDisplayName = displayName.data();
    bi.lpszTitle = L"Choose the game folder (or a .pkg file) to serve for this URL";
    bi.ulFlags = BIF_BROWSEINCLUDEFILES | BIF_NEWDIALOGSTYLE;

    if (editingCdnUrlId_.empty()) {
        mirrorsStatusLabel_.SetWindowText(L"Select a URL in the list first.");
        if (comInitializedHere) ::CoUninitialize();
        return 0;
    }

    LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
    bool picked = false;
    if (pidl != nullptr) {
        std::array<wchar_t, MAX_PATH> path{};
        if (::SHGetPathFromIDListW(pidl, path.data())) {
            cdnUrlLocalPathEdit_.SetWindowText(path.data());
            picked = true;
        }
        ::CoTaskMemFree(pidl);
    }

    if (comInitializedHere) ::CoUninitialize();
    if (picked) saveCdnUrlEdits();
    return 0;
}

LRESULT FtpView::OnRemoveCdnUrlClick(WORD, WORD, HWND, BOOL&) {
    if (!cdnUrlService_) return 0;

    const int sel = cdnUrlListView_.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= cdnUrls_.size()) {
        mirrorsStatusLabel_.SetWindowText(L"Select a URL first.");
        return 0;
    }

    cdnUrlService_->removeUrl(cdnUrls_[sel].cdnUrlId);
    mirrorsStatusLabel_.SetWindowText(L"URL removed.");
    refreshCdnUrlsList();
    return 0;
}

LRESULT FtpView::OnProxyStartClick(WORD, WORD, HWND, BOOL&) {
    if (!acceleratorService_) {
        httpProxyStatusDot_.setState(StatusDot::State::Warning);
        httpProxyStatusLabel_.SetWindowText(L"ERROR: Proxy service not initialized. App restart required.");
        return 0;
    }

    // The cache folder is optional - console-tracking, CDN URL logging, mirror
    // substitution and local-library serving all work without one. Leaving it
    // blank just means responses are never written to disk (cacheEnabled below
    // follows suit), not that the proxy refuses to start.
    const std::wstring folder = getEditText(cacheFolderEdit_);

    const std::wstring portText = getEditText(httpProxyPortEdit_);
    int port = 8080;
    try {
        port = portText.empty() ? 8080 : std::stoi(portText);
    } catch (...) {
        httpProxyStatusLabel_.SetWindowText(L"Port must be a valid number (1-65535).");
        httpProxyStatusDot_.setState(StatusDot::State::Warning);
        return 0;
    }

    if (port < 1 || port > 65535) {
        httpProxyStatusLabel_.SetWindowText(L"Port must be between 1 and 65535.");
        httpProxyStatusDot_.setState(StatusDot::State::Warning);
        return 0;
    }

    // The IP field is user-editable input now, not an auto-detected
    // display - fill it only if it's genuinely empty (e.g. detection
    // failed at OnCreate and the user hasn't typed one either). Whatever
    // ends up there, respected as-is, becomes what's shown to the user as
    // "point your console at this address".
    if (httpProxyIpDisplay_.GetWindowTextLength() == 0) {
        const auto detected = gth::network::NetworkInfoHelper::GetLocalNetworkInfo();
        httpProxyIpDisplay_.SetWindowText(detected.isValid ? detected.ipAddress.c_str() : L"127.0.0.1");
    }

    httpProxyStatusLabel_.SetWindowText(L"Starting proxy...");
    ::UpdateWindow(httpProxyStatusLabel_.m_hWnd);

    acceleratorService_->stop();
    Sleep(200);
    acceleratorService_->start(WideToUtf8(folder), port, /*cacheEnabled=*/!folder.empty());
    Sleep(200);

    lastKnownProxyRunning_ = acceleratorService_->isRunning();
    if (!lastKnownProxyRunning_) {
        httpProxyStatusDot_.setState(StatusDot::State::Warning);
        httpProxyStatusLabel_.SetWindowText(
            L"Proxy failed. Check: firewall, antivirus, port conflicts. Try ports: 80, 443, 5000, 9999");
        return 0;
    }

    refreshAcceleratorStatus();
    return 0;
}

LRESULT FtpView::OnProxyStopClick(WORD, WORD, HWND, BOOL&) {
    if (!acceleratorService_) return 0;

    acceleratorService_->stop();
    lastKnownProxyRunning_ = false;
    httpProxyStatusDot_.setState(StatusDot::State::Off);
    httpProxyStatusLabel_.SetWindowText(L"Proxy stopped. URLs will no longer be captured.");
    return 0;
}

LRESULT FtpView::OnCdnUrlSelectionChanged(int, LPNMHDR pnmh, BOOL&) {
    auto* info = reinterpret_cast<NMLISTVIEW*>(pnmh);
    if (suppressCdnEdit_ || !(info->uChanged & LVIF_STATE)) return 0;

    // Only react to "an item became selected" or "nothing is selected any more" - a selection
    // change arrives as a deselect notification followed by a select notification.
    const int sel = cdnUrlListView_.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= cdnUrls_.size()) {
        if (info->uNewState & LVIS_SELECTED) return 0;
        editingCdnUrlId_.clear();
        suppressCdnEdit_ = true;
        cdnUrlMarkEdit_.SetWindowText(L"");
        cdnUrlLocalPathEdit_.SetWindowText(L"");
        suppressCdnEdit_ = false;
        return 0;
    }

    const auto& selectedUrl = cdnUrls_[sel];
    if (selectedUrl.cdnUrlId == editingCdnUrlId_) return 0;

    editingCdnUrlId_ = selectedUrl.cdnUrlId;
    suppressCdnEdit_ = true;
    cdnUrlMarkEdit_.SetWindowText(Utf8ToWide(selectedUrl.markText).c_str());
    cdnUrlLocalPathEdit_.SetWindowText(Utf8ToWide(selectedUrl.localPath).c_str());
    suppressCdnEdit_ = false;
    return 0;
}

// Saves only when the user leaves a box (EN_KILLFOCUS) - saving on every EN_CHANGE keystroke would
// write half-typed paths to the database (and the proxy reads them live).
LRESULT FtpView::OnCdnUrlMarkChanged(WORD wNotifyCode, WORD, HWND, BOOL& bHandled) {
    if (wNotifyCode == EN_KILLFOCUS) saveCdnUrlEdits();
    bHandled = FALSE;
    return 0;
}

LRESULT FtpView::OnCdnUrlLocalPathChanged(WORD wNotifyCode, WORD, HWND, BOOL& bHandled) {
    if (wNotifyCode == EN_KILLFOCUS) saveCdnUrlEdits();
    bHandled = FALSE;
    return 0;
}

void FtpView::saveCdnUrlEdits() {
    if (!cdnUrlService_ || suppressCdnEdit_ || editingCdnUrlId_.empty()) return;

    size_t idx = cdnUrls_.size();
    for (size_t i = 0; i < cdnUrls_.size(); ++i) {
        if (cdnUrls_[i].cdnUrlId == editingCdnUrlId_) {
            idx = i;
            break;
        }
    }
    if (idx == cdnUrls_.size()) return;

    auto& entry = cdnUrls_[idx];
    const std::string newMark = WideToUtf8(getEditText(cdnUrlMarkEdit_));
    std::string newPath = WideToUtf8(getEditText(cdnUrlLocalPathEdit_));
    // Drop stray whitespace and the quotes Explorer's "Copy as path" adds.
    while (!newPath.empty() && (newPath.front() == ' ' || newPath.front() == '"')) newPath.erase(newPath.begin());
    while (!newPath.empty() && (newPath.back() == ' ' || newPath.back() == '"')) newPath.pop_back();

    const bool markChanged = newMark != entry.markText;
    const bool pathChanged = newPath != entry.localPath;
    if (!markChanged && !pathChanged) return;

    if (markChanged) {
        cdnUrlService_->updateMark(entry.cdnUrlId, newMark);
        entry.markText = newMark;
    }
    if (pathChanged) {
        cdnUrlService_->updateLocalPath(entry.cdnUrlId, newPath);
        entry.localPath = newPath;
        suppressCdnEdit_ = true;
        cdnUrlLocalPathEdit_.SetWindowText(Utf8ToWide(newPath).c_str());
        suppressCdnEdit_ = false;
    }

    const int row = static_cast<int>(idx);
    cdnUrlListView_.SetItemText(row, 1, Utf8ToWide(entry.markText).c_str());
    cdnUrlListView_.SetItemText(row, 2, Utf8ToWide(entry.localPath).c_str());
    cdnUrlListView_.SetItemText(row, 3, entry.localPath.empty() ? L"Download" : L"Local");

    if (!pathChanged) {
        mirrorsStatusLabel_.SetWindowText(L"Mark saved.");
    } else if (newPath.empty()) {
        mirrorsStatusLabel_.SetWindowText(L"Local path cleared - this URL will download from the network again.");
    } else if (entry.url.rfind("https://", 0) == 0) {
        mirrorsStatusLabel_.SetWindowText(
            L"Saved, but this is an HTTPS tunnel entry - the proxy can't see inside HTTPS, so only plain http:// URLs can be served from disk.");
    } else {
        std::error_code ec;
        const bool exists = std::filesystem::exists(std::filesystem::path(Utf8ToWide(newPath)), ec);
        mirrorsStatusLabel_.SetWindowText(exists ? L"Local path saved - this URL will now be served from disk."
                                                 : L"Saved, but that path doesn't exist on this PC - it will download from the network until it does.");
    }
}

LRESULT FtpView::OnAddMitmInstanceClick(WORD, WORD, HWND, BOOL&) {
    addMitmInstance();
    return 0;
}

// Resolves which MitmInstance/button by searching for whichever entry owns
// wID's idBase, rather than assuming index == (wID - IDC_MITM_INSTANCE_FIRST)
// / 3 - a removal from the middle of mitmInstances_ leaves gaps in the ID
// range without renumbering the survivors, so position in the vector no
// longer maps directly to a fixed ID offset.
LRESULT FtpView::OnMitmInstanceButtonClick(WORD, WORD wID, HWND, BOOL&) {
    for (size_t i = 0; i < mitmInstances_.size(); ++i) {
        MitmInstance& instance = *mitmInstances_[i];
        if (wID == instance.idBase) {  // Start
            const std::wstring portText = getEditText(instance.portEdit);
            int port = 8080;
            try {
                port = portText.empty() ? 8080 : std::stoi(portText);
            } catch (...) {
                instance.statusDot.setState(StatusDot::State::Warning);
                instance.statusLabel.SetWindowText(L"Port must be a number (1-65535).");
                return 0;
            }
            if (port < 1 || port > 65535) {
                instance.statusDot.setState(StatusDot::State::Warning);
                instance.statusLabel.SetWindowText(L"Port must be 1-65535.");
                return 0;
            }
            if (instance.ipEdit.GetWindowTextLength() == 0) {
                const auto detected = gth::network::NetworkInfoHelper::GetLocalNetworkInfo();
                instance.ipEdit.SetWindowText(detected.isValid ? detected.ipAddress.c_str() : L"127.0.0.1");
            }

            // Shares the same (optional) cache folder as the original instance - there is only
            // one game library/cache on this PC regardless of how many consoles are connected.
            const std::wstring cacheFolder = getEditText(cacheFolderEdit_);
            gth::core::AcceleratorServerConfig config;
            config.cacheDir = WideToUtf8(cacheFolder);
            config.port = port;
            config.cacheEnabled = !cacheFolder.empty();

            instance.service->stop();
            Sleep(200);
            instance.service->start(config);
            Sleep(200);
            instance.lastKnownRunning = instance.service->isRunning();
            if (instance.lastKnownRunning) {
                instance.statusDot.setState(StatusDot::State::On);
                instance.statusLabel.SetWindowText(std::format(L"Running :{}", port).c_str());
            } else {
                instance.statusDot.setState(StatusDot::State::Warning);
                instance.statusLabel.SetWindowText(L"Failed to start (port in use?).");
            }
            return 0;
        }
        if (wID == instance.idBase + 1) {  // Stop
            instance.service->stop();
            instance.lastKnownRunning = false;
            instance.statusDot.setState(StatusDot::State::Off);
            instance.statusLabel.SetWindowText(L"Stopped.");
            return 0;
        }
        if (wID == instance.idBase + 2) {  // Remove
            removeMitmInstance(i);
            return 0;
        }
    }
    return 0;
}

void FtpView::addMitmInstance() {
    if (!downloadAcceleratorServerFactory_) return;

    // Technical ceiling from the reserved ID range (see ControlIds.h) vs.
    // the admin-configured practical limit (Settings > Console Limits,
    // admin-only) - whichever is smaller wins. A fresh install with no
    // admin having ever changed it defaults to AppSettings::maxConsoles's
    // own default (4), not the technical ceiling, so "unlimited by
    // default" never happens.
    constexpr int kTechnicalMaxMitmInstances = (IDC_MITM_INSTANCE_LAST - IDC_MITM_INSTANCE_FIRST + 1) / 3;
    int maxMitmInstances = kTechnicalMaxMitmInstances;
    if (appSettingsService_) {
        maxMitmInstances = std::min(kTechnicalMaxMitmInstances, appSettingsService_->getSettings().maxConsoles);
    }

    if (static_cast<int>(mitmInstances_.size()) >= maxMitmInstances) {
        httpProxyStatusLabel_.SetWindowText(
            L"Console limit reached. An admin can raise it in Settings > Console Limits.");
        return;
    }

    // First free ID slot - a prior removal from the middle can leave gaps,
    // so this isn't simply mitmInstances_.size().
    int idBase = IDC_MITM_INSTANCE_FIRST;
    for (int slot = 0; slot < maxMitmInstances; ++slot) {
        const int candidate = IDC_MITM_INSTANCE_FIRST + slot * 3;
        const bool inUse = std::any_of(mitmInstances_.begin(), mitmInstances_.end(),
                                        [candidate](const auto& inst) { return inst->idBase == candidate; });
        if (!inUse) {
            idBase = candidate;
            break;
        }
    }

    // Suggest one past the highest port already in use across the original
    // section and every existing instance, so consoles get distinct ports
    // by default without the user having to work it out themselves.
    int suggestedPort = 8080;
    auto considerPort = [&suggestedPort](const std::wstring& text) {
        try {
            const int p = std::stoi(text);
            if (p > suggestedPort) suggestedPort = p;
        } catch (...) {
        }
    };
    considerPort(getEditText(httpProxyPortEdit_));
    for (auto& inst : mitmInstances_) considerPort(getEditText(inst->portEdit));
    ++suggestedPort;

    auto instance = std::make_unique<MitmInstance>();
    instance->idBase = idBase;
    instance->service = downloadAcceleratorServerFactory_->create();

    HFONT normalFont = theme::BodyFont();
    // relayoutMitmAndCdnSections() (called below) immediately places every
    // one of these correctly - the exact rect passed to Create() only
    // needs to be valid, not final.
    CRect placeholder(kMargin, 0, kMargin + 40, 20);

    instance->ipEdit.Create(m_hWnd, placeholder, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    instance->ipEdit.SetFont(normalFont);
    {
        const auto detected = gth::network::NetworkInfoHelper::GetLocalNetworkInfo();
        instance->ipEdit.SetWindowText(detected.isValid ? detected.ipAddress.c_str() : L"127.0.0.1");
    }

    instance->portEdit.Create(m_hWnd, placeholder, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    instance->portEdit.SetFont(normalFont);
    instance->portEdit.SetWindowText(std::to_wstring(suggestedPort).c_str());

    instance->startButton.Create(m_hWnd, placeholder, L"Start", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                  idBase);
    instance->startButton.SetFont(normalFont);
    instance->startButton.setKind(ModernButton::Kind::Primary);

    instance->stopButton.Create(m_hWnd, placeholder, L"Stop", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                 idBase + 1);
    instance->stopButton.SetFont(normalFont);
    instance->stopButton.setKind(ModernButton::Kind::Danger);

    instance->removeButton.Create(m_hWnd, placeholder, L"x", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                   idBase + 2);
    instance->removeButton.SetFont(normalFont);
    instance->removeButton.setKind(ModernButton::Kind::Subtle);

    instance->statusDot.Create(m_hWnd, placeholder, L"Status", WS_CHILD | WS_VISIBLE);

    instance->statusLabel.Create(m_hWnd, placeholder, L"Not running", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX);
    instance->statusLabel.SetFont(normalFont);

    mitmInstances_.push_back(std::move(instance));
    relayoutMitmAndCdnSections();
    Invalidate();
}

void FtpView::removeMitmInstance(size_t index) {
    if (index >= mitmInstances_.size()) return;

    MitmInstance& instance = *mitmInstances_[index];
    if (instance.service) instance.service->stop();

    for (CWindow* ctrl : std::initializer_list<CWindow*>{&instance.ipEdit, &instance.portEdit, &instance.startButton,
                                                           &instance.stopButton, &instance.removeButton,
                                                           &instance.statusDot, &instance.statusLabel}) {
        if (ctrl->IsWindow()) ctrl->DestroyWindow();
    }

    mitmInstances_.erase(mitmInstances_.begin() + static_cast<std::ptrdiff_t>(index));
    relayoutMitmAndCdnSections();
    Invalidate();
}
