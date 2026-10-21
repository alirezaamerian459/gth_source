#include "MainFrame.h"

#include <dwmapi.h>

#include <set>

#include "SidebarTabs.h"
#include "gth/logging/Logging.h"

#pragma comment(lib, "dwmapi.lib")

namespace {
constexpr int kDashboardIndex = kSidebarDashboardIndex;
constexpr int kGamesIndex = 1;
constexpr int kUpdatesIndex = 2;
constexpr int kDownloadsIndex = 3;
constexpr int kStorageIndex = 4;
constexpr int kConsolesIndex = 5;
constexpr int kTransfersIndex = 6;
constexpr int kFtpIndex = 7;
constexpr int kNetworkIndex = 8;
constexpr int kLogsIndex = 9;
constexpr int kReportsIndex = 10;
constexpr int kEnterpriseIndex = 11;
constexpr int kSettingsIndex = kSidebarSettingsIndex;
constexpr int kSidebarWidth = 216;
constexpr int kSidebarItemHeight = 40;
constexpr int kSidebarPadding = 8;
// Two lines of small text beneath the nav list - the credit/rights footer.
constexpr int kFooterHeight = 34;

// DWMWA_USE_IMMERSIVE_DARK_MODE - present in the Windows 11 SDK's dwmapi.h,
// but guard it anyway in case an older SDK header is ever picked up (the
// value has been stable at 20 since it shipped in 20H1).
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
}  // namespace

MainFrame::MainFrame(gth::application::IStorageService& storageService, gth::application::IGameIndexer& gameIndexer,
                      gth::application::IGameLibraryService& gameLibrary,
                      gth::application::IGamePosterService& gamePosterService,
                      gth::application::IUpdateDetector& updateDetector,
                      gth::application::IUpdateJobService& updateJobService,
                      gth::core::INetworkInterfaceDetector& networkInterfaceDetector,
                      gth::application::IConsoleManager& consoleManager,
                      gth::application::ITransferQueueService& transferQueue,
                      gth::application::IReportService& reportService, const std::filesystem::path& logDirectory,
                      gth::application::ICentralServerService& centralServerService,
                      gth::application::IAgentService& agentService,
                      gth::application::IFtpConnectionService& ftpConnectionService,
                      gth::application::IFtpTransferService& ftpTransferService,
                      gth::application::IFtpServerService& ftpServerService,
                      gth::application::ICdnUrlService& cdnUrlService,
                      gth::application::IDownloadAcceleratorService& downloadAcceleratorService,
                      gth::application::IAppSettingsService& appSettingsService,
                      gth::application::IAdminAuthService& adminAuthService,
                      gth::acceleratorserver::IDownloadAcceleratorServerFactory& downloadAcceleratorServerFactory,
                      gth::core::IFirewallService& firewallService,
                      gth::core::ITitleLookupService& titleLookupService,
                      gth::application::IUpdateChecker& updateChecker, std::filesystem::path sonyLinksPath,
                      gth::application::IPackageSync& packageSync, std::filesystem::path sonyAutomationPath,
                      gth::core::ICoverArtClient& coverArtClient)
    : storageService_(storageService),
      gameIndexer_(gameIndexer),
      gameLibrary_(gameLibrary),
      gamePosterService_(gamePosterService),
      updateDetector_(updateDetector),
      updateJobService_(updateJobService),
      networkInterfaceDetector_(networkInterfaceDetector),
      consoleManager_(consoleManager),
      transferQueue_(transferQueue),
      reportService_(reportService),
      logDirectory_(logDirectory),
      centralServerService_(centralServerService),
      agentService_(agentService),
      ftpConnectionService_(ftpConnectionService),
      ftpTransferService_(ftpTransferService),
      ftpServerService_(ftpServerService),
      cdnUrlService_(cdnUrlService),
      downloadAcceleratorService_(downloadAcceleratorService),
      appSettingsService_(appSettingsService),
      adminAuthService_(adminAuthService),
      downloadAcceleratorServerFactory_(downloadAcceleratorServerFactory),
      firewallService_(firewallService),
      titleLookupService_(titleLookupService),
      updateChecker_(updateChecker),
      sonyLinksPath_(std::move(sonyLinksPath)),
      packageSync_(packageSync),
      sonyAutomationPath_(std::move(sonyAutomationPath)),
      coverArtClient_(coverArtClient) {}

LRESULT MainFrame::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    // The version tag exists because this project has accumulated several
    // stale duplicate copies (old zip extractions, an abandoned parallel
    // "gth_updated" folder) that look identical to the real one at a
    // glance - showing the version in the title bar/taskbar lets whoever's
    // running it confirm at a glance which build they actually launched,
    // instead of guessing from file paths. Bump this string, and the
    // FILEVERSION/ProductVersion strings in resources/AppResources.rc,
    // together whenever the app's version changes.
    SetWindowText(L"Dr Game Transfer Hub v6.9.0");
    applyRoundedCorners();

    // Load the persisted theme before any child window paints for the first
    // time, so nothing has to be re-painted with the wrong colors a moment
    // later.
    const auto initialSettings = appSettingsService_.getSettings();
    theme::SetDarkMode(initialSettings.theme == gth::core::ThemeMode::Dark);
    applyThemeToWindow();

    CRect clientRect;
    GetClientRect(&clientRect);

    CRect sidebarRect(0, 0, kSidebarWidth, clientRect.Height() - kFooterHeight);
    sidebar_.Create(m_hWnd, sidebarRect, L"",
                     WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT |
                         WS_VSCROLL,
                     0, IDC_SIDEBAR_LIST);

    CRect footerRect(0, clientRect.Height() - kFooterHeight, kSidebarWidth, clientRect.Height());
    footer_.Create(m_hWnd, footerRect,
                   L"Developed by Alireza Amerian\r\n\x00A9 Dr Game. All rights reserved.",
                   WS_CHILD | WS_VISIBLE | SS_CENTER);
    footer_.SetFont(theme::SmallFont());

    // Not const: _U_RECT (what Create() actually takes) binds to RECT&, which
    // cannot bind to a const lvalue either.
    CRect contentRect(kSidebarWidth, 0, clientRect.right, clientRect.bottom);

    dashboardView_.init(storageService_);
    dashboardView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    gamesView_.init(storageService_, gameIndexer_, gameLibrary_, titleLookupService_, sharedState_, posterCache_,
                     gamePosterService_);
    gamesView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    updatesView_.init(gameLibrary_, gameIndexer_, updateChecker_, packageSync_, storageService_, sonyLinksPath_,
                      sonyAutomationPath_, sharedState_, posterCache_, gamePosterService_);
    updatesView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    // Both poster-grid views have subscribed to posterCache_ by now (each does so in its own
    // OnCreate, which Create() above already ran synchronously) - safe to start the background
    // fallback worker.
    posterCache_.enableWebFallback(coverArtClient_);
    downloadsView_.init(sonyLinksPath_.parent_path() / "download-history.tsv", sharedState_);
    downloadsView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    storageView_.init(storageService_);
    storageView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    consolesView_.init(consoleManager_);
    consolesView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    transfersView_.init(transferQueue_);
    transfersView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    ftpView_.init(ftpConnectionService_, ftpTransferService_, ftpServerService_, downloadAcceleratorService_, cdnUrlService_, adminAuthService_, downloadAcceleratorServerFactory_, appSettingsService_);
    ftpView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    networkView_.init(networkInterfaceDetector_);
    networkView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    logsView_.init(logDirectory_);
    logsView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    reportsView_.init(reportService_);
    reportsView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    enterpriseView_.init(centralServerService_, agentService_);
    enterpriseView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    settingsView_.init(appSettingsService_, adminAuthService_, firewallService_, [this] { onSettingsChanged(); });
    settingsView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    comingSoonView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);

    ::ShowWindow(dashboardView_.m_hWnd, SW_HIDE);
    ::ShowWindow(gamesView_.m_hWnd, SW_HIDE);
    ::ShowWindow(updatesView_.m_hWnd, SW_HIDE);
    ::ShowWindow(downloadsView_.m_hWnd, SW_HIDE);
    ::ShowWindow(storageView_.m_hWnd, SW_HIDE);
    ::ShowWindow(consolesView_.m_hWnd, SW_HIDE);
    ::ShowWindow(transfersView_.m_hWnd, SW_HIDE);
    ::ShowWindow(ftpView_.m_hWnd, SW_HIDE);
    ::ShowWindow(networkView_.m_hWnd, SW_HIDE);
    ::ShowWindow(logsView_.m_hWnd, SW_HIDE);
    ::ShowWindow(reportsView_.m_hWnd, SW_HIDE);
    ::ShowWindow(enterpriseView_.m_hWnd, SW_HIDE);
    ::ShowWindow(settingsView_.m_hWnd, SW_HIDE);
    ::ShowWindow(comingSoonView_.m_hWnd, SW_HIDE);

    populateSidebar(kDashboardIndex);
    showView(kDashboardIndex);

    PostMessage(kAutoAllowFirewallMessage);
    return 0;
}

// Windows Firewall blocks other devices from reaching the FTP server, proxy
// and accelerator unless this exact program has an allow rule and nothing
// blocks it - which used to mean the only fix was turning the firewall off.
// Sort that out quietly at every launch (a portable copy extracted to a new
// folder is a new program to Windows and needs its own rules), before the
// user can start any server. Skipped only when it's already fine or the user
// removed the rules on purpose; the outcome is logged and shown in Settings.
LRESULT MainFrame::OnAutoAllowFirewall(UINT, WPARAM, LPARAM, BOOL&) {
    const auto result = firewallService_.allowThisAppIfNeeded();
    if (!result) return 0;

    if (result->success) {
        GTH_LOG_INFO("Firewall: {}", result->message);
    } else {
        GTH_LOG_WARN("Firewall: couldn't allow this app automatically - {}", result->message);
    }
    settingsView_.refresh();
    return 0;
}

LRESULT MainFrame::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
    layout();
    return 0;
}

LRESULT MainFrame::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    ::PostQuitMessage(0);
    bHandled = FALSE;
    return 0;
}

LRESULT MainFrame::OnCtlColorListBox(UINT, WPARAM wParam, LPARAM, BOOL&) {
    HBRUSH sidebarBrush = theme::SidebarBgBrush();
    HDC hdc = reinterpret_cast<HDC>(wParam);
    ::SetBkMode(hdc, TRANSPARENT);
    return reinterpret_cast<LRESULT>(sidebarBrush);
}

LRESULT MainFrame::OnMeasureItem(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
    mis->itemHeight = kSidebarItemHeight;
    return 1;
}

// Custom-draws each sidebar row so the selected item gets a rounded,
// accent-tinted "pill" behind it - the same visual language Windows 11's
// own Settings app uses for its navigation pane, rather than the flat
// system-blue highlight bar a stock owner-draw-less listbox would show.
LRESULT MainFrame::OnDrawItem(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
    if (dis->CtlID != IDC_SIDEBAR_LIST || dis->itemID == static_cast<UINT>(-1)) return 0;

    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    CRect itemRect(dis->rcItem);

    HBRUSH bg = ::CreateSolidBrush(theme::SidebarBg());
    ::FillRect(dis->hDC, &itemRect, bg);
    ::DeleteObject(bg);

    CRect textRect = itemRect;
    if (selected) {
        CRect pillRect = itemRect;
        pillRect.DeflateRect(kSidebarPadding, 4);
        theme::FillRoundRect(dis->hDC, pillRect, 8, theme::AccentSoft());
    }
    textRect.left += kSidebarPadding + 12;

    wchar_t text[128]{};
    sidebar_.GetText(static_cast<int>(dis->itemID), text);

    HGDIOBJ oldFont = ::SelectObject(dis->hDC, theme::BodyFontBold());
    ::SetBkMode(dis->hDC, TRANSPARENT);
    ::SetTextColor(dis->hDC, selected ? theme::Accent() : theme::TextPrimary());
    ::DrawText(dis->hDC, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::SelectObject(dis->hDC, oldFont);

    return 1;
}

// Only the footer_ static is ever a direct child of the frame itself (every view's own statics are
// parented to that view, which handles WM_CTLCOLORSTATIC on its own) - so this paints it to match
// the sidebar it sits under rather than the frame's own (unthemed) default background.
LRESULT MainFrame::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) {
    HDC hdc = reinterpret_cast<HDC>(wParam);
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, theme::TextSecondary());
    return reinterpret_cast<LRESULT>(theme::SidebarBgBrush());
}

void MainFrame::applyRoundedCorners() {
    // Windows 11 only (silently ignored/no-op on Windows 10, which is fine -
    // DWM there just leaves the window's default square corners alone).
    const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    ::DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}

LRESULT MainFrame::OnSidebarSelChange(WORD, WORD, HWND, BOOL&) {
    const int sel = sidebar_.GetCurSel();
    if (sel >= 0 && sel < static_cast<int>(visibleLogicalIndices_.size())) showView(visibleLogicalIndices_[static_cast<size_t>(sel)]);
    return 0;
}

// Rebuilds the sidebar listbox from whichever tabs are currently visible
// (Settings' hidden-tabs checkboxes), then restores selection to
// keepLogicalIndex if it's still present - it always is, since Dashboard
// and Settings can never be hidden, and this is only ever called while the
// user is on one of those two.
void MainFrame::populateSidebar(int keepLogicalIndex) {
    if (!sidebar_.IsWindow()) return;

    const auto settings = appSettingsService_.getSettings();

    // A signed-out (regular) user never sees an admin-only tab, on top of
    // whatever they've personally chosen to hide - see VisibleSidebarTabIndices.
    sidebar_.ResetContent();
    visibleLogicalIndices_ = VisibleSidebarTabIndices(settings.hiddenTabs, adminAuthService_.isSignedIn());
    int keepPosition = 0;
    for (size_t position = 0; position < visibleLogicalIndices_.size(); ++position) {
        const int logicalIndex = visibleLogicalIndices_[position];
        if (logicalIndex == keepLogicalIndex) keepPosition = static_cast<int>(position);
        sidebar_.AddString(kSidebarTabs[static_cast<size_t>(logicalIndex)]);
    }

    sidebar_.SetCurSel(keepPosition);
}

// Re-themes the frame itself (dark titlebar) - each child view's own
// WM_CTLCOLORSTATIC/OnPaint already re-reads theme::* live, so a full
// repaint (see onSettingsChanged) is all they need.
void MainFrame::applyThemeToWindow() {
    const BOOL dark = theme::IsDarkMode() ? TRUE : FALSE;
    ::DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // DWM doesn't always repaint the non-client area (title bar/caption
    // buttons) just because the attribute changed - SWP_FRAMECHANGED forces
    // it to, which is the difference between the titlebar actually going
    // dark right now versus only on the next unrelated resize/redraw.
    ::SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void MainFrame::onSettingsChanged() {
    applyThemeToWindow();
    populateSidebar(kSettingsIndex);
    ::RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
}

void MainFrame::showView(int index) {
    HWND newView;
    if (index == kDashboardIndex) {
        dashboardView_.refresh();
        newView = dashboardView_.m_hWnd;
    } else if (index == kGamesIndex) {
        gamesView_.refresh();
        newView = gamesView_.m_hWnd;
    } else if (index == kUpdatesIndex) {
        updatesView_.refresh();
        newView = updatesView_.m_hWnd;
    } else if (index == kDownloadsIndex) {
        downloadsView_.refresh();
        newView = downloadsView_.m_hWnd;
    } else if (index == kStorageIndex) {
        storageView_.refresh();
        newView = storageView_.m_hWnd;
    } else if (index == kConsolesIndex) {
        consolesView_.refresh();
        newView = consolesView_.m_hWnd;
    } else if (index == kTransfersIndex) {
        transfersView_.refresh();
        newView = transfersView_.m_hWnd;
    } else if (index == kFtpIndex) {
        ftpView_.refresh();
        newView = ftpView_.m_hWnd;
    } else if (index == kNetworkIndex) {
        networkView_.refresh();
        newView = networkView_.m_hWnd;
    } else if (index == kLogsIndex) {
        logsView_.refresh();
        newView = logsView_.m_hWnd;
    } else if (index == kReportsIndex) {
        reportsView_.refresh();
        newView = reportsView_.m_hWnd;
    } else if (index == kEnterpriseIndex) {
        enterpriseView_.refresh();
        newView = enterpriseView_.m_hWnd;
    } else if (index == kSettingsIndex) {
        settingsView_.refresh();
        newView = settingsView_.m_hWnd;
    } else {
        comingSoonView_.setTitle(kSidebarTabs[static_cast<size_t>(index)]);
        newView = comingSoonView_.m_hWnd;
    }

    if (currentView_ && currentView_ != newView) ::ShowWindow(currentView_, SW_HIDE);
    currentView_ = newView;
    ::ShowWindow(currentView_, SW_SHOW);
}

void MainFrame::layout() {
    CRect clientRect;
    GetClientRect(&clientRect);

    if (sidebar_.IsWindow()) sidebar_.MoveWindow(0, 0, kSidebarWidth, clientRect.Height() - kFooterHeight);
    if (footer_.IsWindow()) {
        footer_.MoveWindow(0, clientRect.Height() - kFooterHeight, kSidebarWidth, kFooterHeight);
    }

    CRect contentRect(kSidebarWidth, 0, clientRect.right, clientRect.bottom);
    if (dashboardView_.IsWindow()) dashboardView_.MoveWindow(&contentRect);
    if (gamesView_.IsWindow()) gamesView_.MoveWindow(&contentRect);
    if (updatesView_.IsWindow()) updatesView_.MoveWindow(&contentRect);
    if (downloadsView_.IsWindow()) downloadsView_.MoveWindow(&contentRect);
    if (storageView_.IsWindow()) storageView_.MoveWindow(&contentRect);
    if (consolesView_.IsWindow()) consolesView_.MoveWindow(&contentRect);
    if (transfersView_.IsWindow()) transfersView_.MoveWindow(&contentRect);
    if (ftpView_.IsWindow()) ftpView_.MoveWindow(&contentRect);
    if (networkView_.IsWindow()) networkView_.MoveWindow(&contentRect);
    if (logsView_.IsWindow()) logsView_.MoveWindow(&contentRect);
    if (reportsView_.IsWindow()) reportsView_.MoveWindow(&contentRect);
    if (enterpriseView_.IsWindow()) enterpriseView_.MoveWindow(&contentRect);
    if (settingsView_.IsWindow()) settingsView_.MoveWindow(&contentRect);
    if (comingSoonView_.IsWindow()) comingSoonView_.MoveWindow(&contentRect);
}
