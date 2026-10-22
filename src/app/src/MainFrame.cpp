#include "MainFrame.h"

#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <set>

#include "SidebarTabs.h"
#include "gth/logging/Logging.h"

#pragma comment(lib, "dwmapi.lib")

namespace {
constexpr int kDashboardIndex = kSidebarDashboardIndex;
constexpr int kGamesIndex = 1;
constexpr int kJbIndex = 2;
constexpr int kUpdatesIndex = 3;
constexpr int kDownloadsIndex = 4;
constexpr int kStorageIndex = 5;
constexpr int kConsolesIndex = 6;
constexpr int kTransfersIndex = 7;
constexpr int kFtpIndex = 8;
constexpr int kNetworkIndex = 9;
constexpr int kLogsIndex = 10;
constexpr int kReportsIndex = 11;
constexpr int kEnterpriseIndex = 12;
constexpr int kBackupsIndex = 13;
constexpr int kSettingsIndex = kSidebarSettingsIndex;
constexpr int kSidebarWidth = 216;
constexpr int kSidebarItemHeight = 36;
constexpr int kSidebarPadding = 8;
constexpr int kSidebarHeaderHeight = 74;
constexpr int kShellHeaderHeight = 64;
// Two lines of small text beneath the nav list - the credit/rights footer.
constexpr int kFooterHeight = 34;

enum class SidebarGroup : int { Workspace = 0, Devices = 1, LibraryData = 2, Administration = 3, Settings = 4 };

SidebarGroup groupForLogicalIndex(int index) {
    switch (index) {
        case kDashboardIndex:
        case kGamesIndex:
        case kJbIndex:
        case kUpdatesIndex:
        case kDownloadsIndex:
        case kTransfersIndex:
            return SidebarGroup::Workspace;
        case kConsolesIndex:
        case kFtpIndex:
        case kNetworkIndex:
            return SidebarGroup::Devices;
        case kStorageIndex:
        case kBackupsIndex:
            return SidebarGroup::LibraryData;
        case kLogsIndex:
        case kReportsIndex:
        case kEnterpriseIndex:
            return SidebarGroup::Administration;
        case kSettingsIndex:
        default:
            return SidebarGroup::Settings;
    }
}

const wchar_t* groupLabel(SidebarGroup group) {
    switch (group) {
        case SidebarGroup::Workspace: return L"Workspace";
        case SidebarGroup::Devices: return L"Devices";
        case SidebarGroup::LibraryData: return L"Library data";
        case SidebarGroup::Administration: return L"Administration";
        case SidebarGroup::Settings: return L"Preferences";
    }
    return L"";
}

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
                      gth::core::ICoverArtClient& coverArtClient, gth::application::BackupService& backupService,
                      gth::network::TelegramBackupTransport& telegramBackupTransport,
                      gth::core::ICredentialStore& credentialStore, std::filesystem::path telegramChatIdPath,
                      std::filesystem::path defaultRestoreRoot)
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
      coverArtClient_(coverArtClient) {
    backupsView_.init(backupService, gameLibrary_, telegramBackupTransport, credentialStore,
                      std::move(telegramChatIdPath), std::move(defaultRestoreRoot));
}

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

    CRect productRect(20, 14, kSidebarWidth - 16, 42);
    productLabel_.Create(m_hWnd, productRect, L"Game Transfer Hub", WS_CHILD | WS_VISIBLE);
    productLabel_.SetFont(theme::BodyFontBold());

    CRect productSubtitleRect(20, 42, kSidebarWidth - 16, 62);
    productSubtitle_.Create(m_hWnd, productSubtitleRect, L"Your game library, in one place", WS_CHILD | WS_VISIBLE);
    productSubtitle_.SetFont(theme::SmallFont());

    CRect sidebarRect(0, kSidebarHeaderHeight, kSidebarWidth, clientRect.Height() - kFooterHeight);
    sidebar_.Create(m_hWnd, sidebarRect, L"",
                     WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT |
                         WS_VSCROLL,
                     0, IDC_SIDEBAR_LIST);

    CRect footerRect(0, clientRect.Height() - kFooterHeight, kSidebarWidth, clientRect.Height());
    footer_.Create(m_hWnd, footerRect,
                   L"Developed by Alireza Amerian\r\n\x00A9 Dr Game. All rights reserved.",
                   WS_CHILD | WS_VISIBLE | SS_CENTER);
    footer_.SetFont(theme::SmallFont());

    CRect contextRect(kSidebarWidth + 28, 13, clientRect.right - 220, 36);
    pageContextLabel_.Create(m_hWnd, contextRect, L"Game Transfer Hub", WS_CHILD | WS_VISIBLE);
    pageContextLabel_.SetFont(theme::SectionFont());

    CRect hintRect(kSidebarWidth + 28, 37, clientRect.right - 220, 56);
    pageContextHint_.Create(m_hWnd, hintRect,
                            L"Manage games, consoles, transfers, and backups from one workspace.",
                            WS_CHILD | WS_VISIBLE);
    pageContextHint_.SetFont(theme::SmallFont());

    CRect statusRect(clientRect.right - 205, 15, clientRect.right - 120, 38);
    shellStatusLabel_.Create(m_hWnd, statusRect, L"Ready", WS_CHILD | WS_VISIBLE | SS_RIGHT);
    shellStatusLabel_.SetFont(theme::SmallFont());

    CRect queueButtonRect(clientRect.right - 108, 13, clientRect.right - 28, 45);
    globalQueueButton_.Create(m_hWnd, queueButtonRect, L"Queue", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                              0, IDC_GLOBAL_QUEUE_BUTTON);
    globalQueueButton_.SetFont(theme::BodyFont());
    globalQueueButton_.setKind(ModernButton::Kind::Secondary);

    // Not const: _U_RECT (what Create() actually takes) binds to RECT&, which
    // cannot bind to a const lvalue either.
    CRect contentRect(kSidebarWidth, kShellHeaderHeight, clientRect.right, clientRect.bottom);

    dashboardView_.init(storageService_);
    dashboardView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    gamesView_.init(storageService_, gameIndexer_, gameLibrary_, titleLookupService_, sharedState_, posterCache_,
                     gamePosterService_);
    gamesView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    jbGamesView_.init(storageService_, posterCache_, gamePosterService_);
    jbGamesView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
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
    backupsView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);
    comingSoonView_.Create(m_hWnd, contentRect, nullptr, WS_CHILD);

    ::ShowWindow(dashboardView_.m_hWnd, SW_HIDE);
    ::ShowWindow(gamesView_.m_hWnd, SW_HIDE);
    ::ShowWindow(jbGamesView_.m_hWnd, SW_HIDE);
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
    ::ShowWindow(backupsView_.m_hWnd, SW_HIDE);
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

LRESULT MainFrame::OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) {
    HDC hdc = reinterpret_cast<HDC>(wParam);
    CRect clientRect;
    GetClientRect(&clientRect);

    HBRUSH contentBrush = theme::ContentBgBrush();
    ::FillRect(hdc, &clientRect, contentBrush);

    CRect sidebarRect(0, 0, kSidebarWidth, clientRect.bottom);
    HBRUSH sidebarBrush = theme::SidebarBgBrush();
    ::FillRect(hdc, &sidebarRect, sidebarBrush);

    HPEN separator = ::CreatePen(PS_SOLID, 1, theme::Border());
    HGDIOBJ oldPen = ::SelectObject(hdc, separator);
    ::MoveToEx(hdc, kSidebarWidth, 0, nullptr);
    ::LineTo(hdc, kSidebarWidth, clientRect.bottom);
    ::MoveToEx(hdc, kSidebarWidth, kShellHeaderHeight, nullptr);
    ::LineTo(hdc, clientRect.right, kShellHeaderHeight);
    ::SelectObject(hdc, oldPen);
    ::DeleteObject(separator);
    return 1;
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

    const int logicalIndex = dis->itemID < visibleLogicalIndices_.size()
                                 ? visibleLogicalIndices_[static_cast<size_t>(dis->itemID)]
                                 : -1;
    if (logicalIndex < 0) {
        wchar_t groupText[128]{};
        sidebar_.GetText(static_cast<int>(dis->itemID), groupText);
        CRect headingRect = itemRect;
        headingRect.left += kSidebarPadding + 12;
        headingRect.top += 6;
        HGDIOBJ oldFont = ::SelectObject(dis->hDC, theme::SmallFont());
        ::SetBkMode(dis->hDC, TRANSPARENT);
        ::SetTextColor(dis->hDC, theme::TextSecondary());
        ::DrawText(dis->hDC, groupText, -1, &headingRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ::SelectObject(dis->hDC, oldFont);
        return 1;
    }

    CRect textRect = itemRect;
    if (selected) {
        CRect pillRect = itemRect;
        pillRect.DeflateRect(kSidebarPadding, 3);
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
LRESULT MainFrame::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    HDC hdc = reinterpret_cast<HDC>(wParam);
    ::SetBkMode(hdc, TRANSPARENT);
    // The parent frame owns the brand/footer and the shell header controls.
    // Their backgrounds are painted by OnEraseBkgnd, so return the matching
    // brush to prevent white rectangles when text is refreshed.
    HWND source = reinterpret_cast<HWND>(lParam);
    const bool sidebarControl = source == productLabel_.m_hWnd || source == productSubtitle_.m_hWnd || source == footer_.m_hWnd;
    const bool primaryText = source == productLabel_.m_hWnd || source == pageContextLabel_.m_hWnd;
    ::SetTextColor(hdc, primaryText ? theme::TextPrimary() : theme::TextSecondary());
    return reinterpret_cast<LRESULT>(sidebarControl ? theme::SidebarBgBrush() : theme::ContentBgBrush());
}

void MainFrame::applyRoundedCorners() {
    // Windows 11 only (silently ignored/no-op on Windows 10, which is fine -
    // DWM there just leaves the window's default square corners alone).
    const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    ::DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}

LRESULT MainFrame::OnSidebarSelChange(WORD, WORD, HWND, BOOL&) {
    const int sel = sidebar_.GetCurSel();
    if (sel < 0 || sel >= static_cast<int>(visibleLogicalIndices_.size())) return 0;

    const int logicalIndex = visibleLogicalIndices_[static_cast<size_t>(sel)];
    if (logicalIndex < 0) {
        // Section headings are deliberately not selectable. If keyboard
        // navigation lands on one, move to the first real page below it.
        for (int next = sel + 1; next < static_cast<int>(visibleLogicalIndices_.size()); ++next) {
            if (visibleLogicalIndices_[static_cast<size_t>(next)] >= 0) {
                sidebar_.SetCurSel(next);
                break;
            }
        }
        return 0;
    }

    showView(logicalIndex);
    return 0;
}

LRESULT MainFrame::OnGlobalQueueClick(WORD, WORD, HWND, BOOL&) {
    const auto settings = appSettingsService_.getSettings();
    const auto visibleTabs = VisibleSidebarTabIndices(settings.hiddenTabs, adminAuthService_.isSignedIn());
    if (std::find(visibleTabs.begin(), visibleTabs.end(), kTransfersIndex) == visibleTabs.end()) {
        shellStatusLabel_.SetWindowText(L"Sign in to use the transfer queue.");
        return 0;
    }
    selectSidebarLogical(kTransfersIndex);
    showView(kTransfersIndex);
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
    const auto visibleTabs = VisibleSidebarTabIndices(settings.hiddenTabs, adminAuthService_.isSignedIn());
    visibleLogicalIndices_.clear();
    int keepPosition = 0;
    constexpr std::array<SidebarGroup, 5> groupOrder = {SidebarGroup::Workspace, SidebarGroup::Devices,
                                                        SidebarGroup::LibraryData, SidebarGroup::Administration,
                                                        SidebarGroup::Settings};
    for (const SidebarGroup group : groupOrder) {
        bool wroteGroup = false;
        for (int logicalIndex : visibleTabs) {
            if (groupForLogicalIndex(logicalIndex) != group) continue;

            if (!wroteGroup) {
                // Negative entries are section rows. Encode the group ordinal so
                // future drawing and keyboard handling can identify them without
                // changing the logical tab indices consumed by Settings/tests.
                const int sectionRow = -1 - static_cast<int>(group);
                visibleLogicalIndices_.push_back(sectionRow);
                sidebar_.AddString(groupLabel(group));
                wroteGroup = true;
            }

            const int row = static_cast<int>(visibleLogicalIndices_.size());
            visibleLogicalIndices_.push_back(logicalIndex);
            if (logicalIndex == keepLogicalIndex) keepPosition = row;
            sidebar_.AddString(kSidebarTabs[static_cast<size_t>(logicalIndex)]);
        }
    }

    sidebar_.SetCurSel(keepPosition);
}

void MainFrame::selectSidebarLogical(int logicalIndex) {
    for (size_t row = 0; row < visibleLogicalIndices_.size(); ++row) {
        if (visibleLogicalIndices_[row] == logicalIndex) {
            sidebar_.SetCurSel(static_cast<int>(row));
            return;
        }
    }
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
    } else if (index == kJbIndex) {
        jbGamesView_.refresh();
        newView = jbGamesView_.m_hWnd;
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
    } else if (index == kBackupsIndex) {
        backupsView_.refresh();
        newView = backupsView_.m_hWnd;
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

    // Keep the shell context consistent even when a view is opened from a
    // global shortcut rather than by clicking the sidebar.
    const wchar_t* title = kSidebarTabs[static_cast<size_t>(index)];
    pageContextLabel_.SetWindowText(title);
    if (index == kDashboardIndex) {
        pageContextHint_.SetWindowText(L"A quick view of your library, storage, and connection status.");
    } else if (index == kJbIndex) {
        pageContextHint_.SetWindowText(L"Browse games found under local folders named 'hacked'.");
    } else if (index == kTransfersIndex) {
        pageContextHint_.SetWindowText(L"Monitor downloads, updates, console sends, and backups in one queue.");
    } else if (index == kBackupsIndex) {
        pageContextHint_.SetWindowText(L"Protect your local game library and restore it when you need it.");
    } else if (index == kSettingsIndex) {
        pageContextHint_.SetWindowText(L"Adjust appearance, access, automation, and advanced tools.");
    } else {
        pageContextHint_.SetWindowText(L"Choose an item to get started, or use Queue to see active work.");
    }
}

void MainFrame::layout() {
    CRect clientRect;
    GetClientRect(&clientRect);

    if (sidebar_.IsWindow())
        sidebar_.MoveWindow(0, kSidebarHeaderHeight, kSidebarWidth, clientRect.Height() - kFooterHeight - kSidebarHeaderHeight);
    if (footer_.IsWindow()) {
        footer_.MoveWindow(0, clientRect.Height() - kFooterHeight, kSidebarWidth, kFooterHeight);
    }

    if (pageContextLabel_.IsWindow()) pageContextLabel_.MoveWindow(kSidebarWidth + 28, 13, clientRect.right - kSidebarWidth - 248, 24);
    if (pageContextHint_.IsWindow()) pageContextHint_.MoveWindow(kSidebarWidth + 28, 37, clientRect.right - kSidebarWidth - 248, 20);
    if (shellStatusLabel_.IsWindow()) shellStatusLabel_.MoveWindow(clientRect.right - 205, 15, 85, 23);
    if (globalQueueButton_.IsWindow()) globalQueueButton_.MoveWindow(clientRect.right - 108, 13, 80, 32);

    CRect contentRect(kSidebarWidth, kShellHeaderHeight, clientRect.right, clientRect.bottom);
    if (dashboardView_.IsWindow()) dashboardView_.MoveWindow(&contentRect);
    if (gamesView_.IsWindow()) gamesView_.MoveWindow(&contentRect);
    if (jbGamesView_.IsWindow()) jbGamesView_.MoveWindow(&contentRect);
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
    if (backupsView_.IsWindow()) backupsView_.MoveWindow(&contentRect);
    if (comingSoonView_.IsWindow()) comingSoonView_.MoveWindow(&contentRect);
}
