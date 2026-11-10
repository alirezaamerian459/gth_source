#include "MainFrame.h"

#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <bit>
#include <format>
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
constexpr int kShellHeaderHeight = 64;
// Two lines of small text beneath the nav list - the credit/rights footer.
constexpr int kFooterHeight = 34;

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

    CRect sidebarRect(0, kShellHeaderHeight, kSidebarWidth, clientRect.Height() - kFooterHeight);
    sidebar_.Create(m_hWnd, sidebarRect, L"",
                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                         LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
                     0, IDC_SIDEBAR_LIST);
    ::SetWindowSubclass(sidebar_.m_hWnd, SidebarSubclassProc, 1, std::bit_cast<DWORD_PTR>(this));

    CRect footerRect(0, clientRect.Height() - kFooterHeight, kSidebarWidth, clientRect.Height());
    footer_.Create(m_hWnd, footerRect,
                   L"Developed by Alireza Amerian\r\n\x00A9 Dr Game. All rights reserved.",
                   WS_CHILD | WS_VISIBLE | SS_CENTER);
    footer_.SetFont(theme::SmallFont());

    CRect navButtonRect(12, 16, 90, 48);
    navToggleButton_.Create(m_hWnd, navButtonRect, L"\x2630 Hide",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_NAV_TOGGLE_BUTTON);
    navToggleButton_.SetFont(theme::BodyFont());
    navToggleButton_.setKind(ModernButton::Kind::Secondary);

    CRect contextRect(100, 12, clientRect.right - 350, 36);
    pageContextLabel_.Create(m_hWnd, contextRect, L"Game Transfer Hub", WS_CHILD | WS_VISIBLE);
    pageContextLabel_.SetFont(theme::SectionFont());

    CRect hintRect(100, 37, clientRect.right - 350, 56);
    pageContextHint_.Create(m_hWnd, hintRect,
                            L"Manage games, consoles, transfers, and backups from one workspace.",
                            WS_CHILD | WS_VISIBLE);
    pageContextHint_.SetFont(theme::SmallFont());

    CRect statusRect(clientRect.right - 335, 15, clientRect.right - 250, 38);
    shellStatusLabel_.Create(m_hWnd, statusRect, L"Ready", WS_CHILD | WS_VISIBLE | SS_RIGHT);
    shellStatusLabel_.SetFont(theme::SmallFont());

    CRect notificationRect(clientRect.right - 240, 13, clientRect.right - 118, 45);
    notificationButton_.Create(m_hWnd, notificationRect, L"\xE7F4 Updates",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                IDC_GLOBAL_NOTIFICATIONS_BUTTON);
    notificationButton_.SetFont(theme::BodyFont());
    notificationButton_.setKind(ModernButton::Kind::Secondary);

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
    updatesView_.setNotificationCallback([this](int count) { updateNotificationBell(count); });
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
    if (!result.has_value()) return 0;

    if (result->success) {
        GTH_LOG_INFO("Firewall: {}", result->message);
    } else {
        GTH_LOG_WARN("Firewall: couldn't allow this app automatically - {}", result->message);
    }
    settingsView_.refresh();
    return 0;
}

LRESULT MainFrame::OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) {
    HDC hdc = std::bit_cast<HDC>(wParam);
    CRect clientRect;
    GetClientRect(&clientRect);

    HBRUSH contentBrush = theme::ContentBgBrush();
    ::FillRect(hdc, &clientRect, contentBrush);

    if (sidebarVisible_) {
        CRect sidebarRect(0, kShellHeaderHeight, kSidebarWidth, clientRect.bottom);
        HBRUSH sidebarBrush = theme::SidebarBgBrush();
        ::FillRect(hdc, &sidebarRect, sidebarBrush);
    }

    HPEN separator = ::CreatePen(PS_SOLID, 1, theme::Border());
    HGDIOBJ oldPen = ::SelectObject(hdc, separator);
    if (sidebarVisible_) {
        ::MoveToEx(hdc, kSidebarWidth, kShellHeaderHeight, nullptr);
        ::LineTo(hdc, kSidebarWidth, clientRect.bottom);
    }
    ::MoveToEx(hdc, 0, kShellHeaderHeight, nullptr);
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
    HDC hdc = std::bit_cast<HDC>(wParam);
    ::SetBkMode(hdc, TRANSPARENT);
    return std::bit_cast<LRESULT>(sidebarBrush);
}

LRESULT MainFrame::OnMeasureItem(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto* mis = std::bit_cast<MEASUREITEMSTRUCT*>(lParam);
    mis->itemHeight = kSidebarItemHeight;
    return 1;
}

// Custom-draws each sidebar row so the selected item gets a rounded,
// accent-tinted "pill" behind it - the same visual language Windows 11's
// own Settings app uses for its navigation pane, rather than the flat
// system-blue highlight bar a stock owner-draw-less listbox would show.
LRESULT MainFrame::OnDrawItem(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto* dis = std::bit_cast<DRAWITEMSTRUCT*>(lParam);
    if (dis->CtlID != IDC_SIDEBAR_LIST || dis->itemID == static_cast<UINT>(-1)) return 0;

    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    CRect itemRect(dis->rcItem);

    HBRUSH bg = ::CreateSolidBrush(theme::SidebarBg());
    ::FillRect(dis->hDC, &itemRect, bg);
    ::DeleteObject(bg);

    const int logicalIndex = dis->itemID < visibleLogicalIndices_.size()
                                 ? visibleLogicalIndices_[static_cast<size_t>(dis->itemID)]
                                 : -1;
    if (IsSidebarGroupRow(logicalIndex)) {
        const auto group = static_cast<SidebarGroup>(-1 - logicalIndex);
        const bool expanded = expandedSidebarGroups_[static_cast<size_t>(group)];
        if (selected) {
            CRect pillRect = itemRect;
            pillRect.DeflateRect(kSidebarPadding, 3);
            theme::FillRoundRect(dis->hDC, pillRect, 8, theme::AccentSoft());
        }

        CRect headingRect = itemRect;
        headingRect.left += kSidebarPadding + 12;
        headingRect.right -= 26;
        HGDIOBJ oldFont = ::SelectObject(dis->hDC, theme::SmallFont());
        ::SetBkMode(dis->hDC, TRANSPARENT);
        ::SetTextColor(dis->hDC, selected ? theme::Accent() : theme::TextSecondary());
        ::DrawText(dis->hDC, groupLabel(group), -1, &headingRect,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ::SelectObject(dis->hDC, oldFont);

        const int centerX = itemRect.right - 18;
        const int centerY = itemRect.top + itemRect.Height() / 2;
        HPEN chevronPen = ::CreatePen(PS_SOLID, 1, selected ? theme::Accent() : theme::TextSecondary());
        HGDIOBJ oldPen = ::SelectObject(dis->hDC, chevronPen);
        if (expanded) {
            ::MoveToEx(dis->hDC, centerX - 4, centerY - 2, nullptr);
            ::LineTo(dis->hDC, centerX, centerY + 2);
            ::LineTo(dis->hDC, centerX + 4, centerY - 2);
        } else {
            ::MoveToEx(dis->hDC, centerX - 2, centerY - 4, nullptr);
            ::LineTo(dis->hDC, centerX + 2, centerY);
            ::LineTo(dis->hDC, centerX - 2, centerY + 4);
        }
        ::SelectObject(dis->hDC, oldPen);
        ::DeleteObject(chevronPen);
        return 1;
    }

    CRect textRect = itemRect;
    if (selected) {
        CRect pillRect = itemRect;
        pillRect.DeflateRect(kSidebarPadding, 3);
        theme::FillRoundRect(dis->hDC, pillRect, 8, theme::AccentSoft());
    }
    textRect.left += kSidebarPadding + 12;

    std::array<wchar_t, 128> text{};
    sidebar_.GetText(static_cast<int>(dis->itemID), text.data());

    HGDIOBJ oldFont = ::SelectObject(dis->hDC, theme::BodyFontBold());
    ::SetBkMode(dis->hDC, TRANSPARENT);
    ::SetTextColor(dis->hDC, selected ? theme::Accent() : theme::TextPrimary());
    ::DrawText(dis->hDC, text.data(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::SelectObject(dis->hDC, oldFont);

    return 1;
}

// Every view's own statics are parented to that view, which handles
// WM_CTLCOLORSTATIC on its own. The sidebar footer is the only frame-level
// static that needs the navigation surface brush.
LRESULT MainFrame::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    HDC hdc = std::bit_cast<HDC>(wParam);
    ::SetBkMode(hdc, TRANSPARENT);
    // The frame owns the footer and shell header controls. Their backgrounds
    // are painted by OnEraseBkgnd, so return the matching brush on refresh.
    HWND source = std::bit_cast<HWND>(lParam);
    const bool sidebarControl = source == footer_.m_hWnd;
    const bool primaryText = source == pageContextLabel_.m_hWnd;
    ::SetTextColor(hdc, primaryText ? theme::TextPrimary() : theme::TextSecondary());
    return std::bit_cast<LRESULT>(sidebarControl ? theme::SidebarBgBrush() : theme::ContentBgBrush());
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
    if (IsSidebarGroupRow(logicalIndex)) return 0;

    showView(logicalIndex);
    return 0;
}

LRESULT MainFrame::SidebarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId,
                                       DWORD_PTR referenceData) {
    auto* self = std::bit_cast<MainFrame*>(referenceData);
    if (!self) return ::DefSubclassProc(hwnd, msg, wParam, lParam);

    const auto groupAtPoint = [self, hwnd](LPARAM point) {
        const LRESULT hit = ::SendMessage(hwnd, LB_ITEMFROMPOINT, 0, point);
        if (HIWORD(hit) != 0) return -1;
        const auto row = static_cast<size_t>(LOWORD(hit));
        if (row >= self->visibleLogicalIndices_.size()) return -1;
        const int rowValue = self->visibleLogicalIndices_[row];
        return IsSidebarGroupRow(rowValue) ? -1 - rowValue : -1;
    };

    if (msg == WM_LBUTTONDOWN) {
        const int groupIndex = groupAtPoint(lParam);
        if (groupIndex >= 0) {
            self->pressedSidebarGroup_ = groupIndex;
            ::SetFocus(hwnd);
            ::SetCapture(hwnd);
            return 0;
        }
    } else if (msg == WM_LBUTTONUP && self->pressedSidebarGroup_ >= 0) {
        const int pressedGroup = self->pressedSidebarGroup_;
        self->pressedSidebarGroup_ = -1;
        if (::GetCapture() == hwnd) ::ReleaseCapture();
        if (groupAtPoint(lParam) == pressedGroup) {
            ::SendMessage(::GetParent(hwnd), kSidebarToggleGroupMessage, static_cast<WPARAM>(pressedGroup), 0);
        }
        return 0;
    } else if (msg == WM_KEYDOWN && (wParam == VK_SPACE || wParam == VK_RETURN)) {
        const LRESULT selected = ::SendMessage(hwnd, LB_GETCURSEL, 0, 0);
        if (selected >= 0 && static_cast<size_t>(selected) < self->visibleLogicalIndices_.size()) {
            const int rowValue = self->visibleLogicalIndices_[static_cast<size_t>(selected)];
            if (IsSidebarGroupRow(rowValue)) {
                ::SendMessage(::GetParent(hwnd), kSidebarToggleGroupMessage, static_cast<WPARAM>(-1 - rowValue), 1);
                return 0;
            }
        }
    } else if (msg == WM_CAPTURECHANGED) {
        self->pressedSidebarGroup_ = -1;
    } else if (msg == WM_NCDESTROY) {
        ::RemoveWindowSubclass(hwnd, SidebarSubclassProc, subclassId);
    }

    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT MainFrame::OnSidebarToggleGroup(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    toggleSidebarGroup(static_cast<int>(wParam), lParam != 0);
    return 0;
}

void MainFrame::toggleSidebarGroup(int groupIndex, bool keepGroupFocused) {
    if (groupIndex < 0 || groupIndex >= kSidebarGroupCount) return;
    const auto groupPosition = static_cast<size_t>(groupIndex);
    expandedSidebarGroups_[groupPosition] = !expandedSidebarGroups_[groupPosition];
    populateSidebar(currentLogicalIndex_);

    if (keepGroupFocused) {
        const int target = SidebarGroupRowValue(static_cast<SidebarGroup>(groupIndex));
        for (size_t row = 0; row < visibleLogicalIndices_.size(); ++row) {
            if (visibleLogicalIndices_[row] == target) {
                sidebar_.SetCurSel(static_cast<int>(row));
                sidebar_.SetFocus();
                break;
            }
        }
    }
}

LRESULT MainFrame::OnNavToggleClick(WORD, WORD, HWND, BOOL&) {
    sidebarVisible_ = !sidebarVisible_;
    if (sidebarVisible_) {
        expandedSidebarGroups_[static_cast<size_t>(SidebarGroupForLogicalIndex(currentLogicalIndex_))] = true;
    }
    navToggleButton_.SetWindowText(sidebarVisible_ ? L"\x2630 Hide" : L"\x2630 Menu");
    if (sidebar_.IsWindow()) sidebar_.ShowWindow(sidebarVisible_ ? SW_SHOW : SW_HIDE);
    if (footer_.IsWindow()) footer_.ShowWindow(sidebarVisible_ ? SW_SHOW : SW_HIDE);
    layout();
    ::RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
    return 0;
}

LRESULT MainFrame::OnGlobalQueueClick(WORD, WORD, HWND, BOOL&) {
    const auto settings = appSettingsService_.getSettings();
    const auto visibleTabs = VisibleSidebarTabIndices(settings.hiddenTabs, adminAuthService_.isSignedIn());
    if (std::find(visibleTabs.begin(), visibleTabs.end(), kTransfersIndex) == visibleTabs.end()) {
        shellStatusLabel_.SetWindowText(L"Sign in to use the transfer queue.");
        return 0;
    }
    expandedSidebarGroups_[static_cast<size_t>(SidebarGroup::Workspace)] = true;
    populateSidebar(currentLogicalIndex_);
    selectSidebarLogical(kTransfersIndex);
    showView(kTransfersIndex);
    return 0;
}

LRESULT MainFrame::OnNotificationsClick(WORD, WORD, HWND, BOOL&) {
    const int count = notificationCount_;
    notificationCount_ = 0;
    updateNotificationBell(0);
    expandedSidebarGroups_[static_cast<size_t>(SidebarGroup::Workspace)] = true;
    populateSidebar(currentLogicalIndex_);
    selectSidebarLogical(kUpdatesIndex);
    showView(kUpdatesIndex);
    shellStatusLabel_.SetWindowText(count > 0 ? L"Updates ready" : L"No new updates");
    return 0;
}

void MainFrame::updateNotificationBell(int count) {
    notificationCount_ = std::max(0, count);
    if (!notificationButton_.IsWindow()) return;
    notificationButton_.SetWindowText(notificationCount_ > 0
                                          ? std::format(L"\xE7F4 Updates ({})", notificationCount_).c_str()
                                          : L"\xE7F4 Updates");
}

// Rebuilds the accordion navigation from the pages currently available to
// this user, then restores the active page selection when its section is open.
void MainFrame::populateSidebar(int keepLogicalIndex) {
    if (!sidebar_.IsWindow()) return;

    const auto settings = appSettingsService_.getSettings();

    // A signed-out (regular) user never sees an admin-only tab, on top of
    // whatever they've personally chosen to hide - see VisibleSidebarTabIndices.
    sidebar_.ResetContent();
    const auto visibleTabs = VisibleSidebarTabIndices(settings.hiddenTabs, adminAuthService_.isSignedIn());
    visibleLogicalIndices_ = BuildSidebarNavigationRows(visibleTabs, expandedSidebarGroups_);
    int keepPosition = -1;
    for (size_t row = 0; row < visibleLogicalIndices_.size(); ++row) {
        const int rowValue = visibleLogicalIndices_[row];
        if (IsSidebarGroupRow(rowValue)) {
            const auto group = static_cast<SidebarGroup>(-1 - rowValue);
            std::wstring accessibleLabel = groupLabel(group);
            accessibleLabel += expandedSidebarGroups_[static_cast<size_t>(group)] ? L", expanded" : L", collapsed";
            sidebar_.AddString(accessibleLabel.c_str());
            continue;
        }

        if (rowValue == keepLogicalIndex) keepPosition = static_cast<int>(row);
        sidebar_.AddString(kSidebarTabs[static_cast<size_t>(rowValue)]);
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
    expandedSidebarGroups_[static_cast<size_t>(SidebarGroup::Settings)] = true;
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
    currentLogicalIndex_ = index;
    ::ShowWindow(currentView_, SW_SHOW);

    // Keep the shell context consistent even when a view is opened from a
    // global shortcut rather than by clicking the sidebar.
    const wchar_t* title = kSidebarTabs[static_cast<size_t>(index)];
    pageContextLabel_.SetWindowText(title);
    if (index == kDashboardIndex) {
        pageContextHint_.SetWindowText(L"A quick view of your library, storage, and connection status.");
    } else if (index == kJbIndex) {
        pageContextHint_.SetWindowText(L"Browse JB games found under local 'hacked' or 'p30day' folders, with local package and DLC markers.");
    } else if (index == kGamesIndex) {
        pageContextHint_.SetWindowText(L"Browse games found across your registered storage locations.");
    } else if (index == kUpdatesIndex) {
        pageContextHint_.SetWindowText(L"Check game updates, manage versions, and sync packages.");
    } else if (index == kDownloadsIndex) {
        pageContextHint_.SetWindowText(L"Review downloaded files and recent download activity.");
    } else if (index == kStorageIndex) {
        pageContextHint_.SetWindowText(L"Add storage, organize drives into categories, and scan for games.");
    } else if (index == kConsolesIndex) {
        pageContextHint_.SetWindowText(L"Register consoles and check their network connection.");
    } else if (index == kTransfersIndex) {
        pageContextHint_.SetWindowText(L"Monitor downloads, updates, console sends, and backups in one queue.");
    } else if (index == kFtpIndex) {
        pageContextHint_.SetWindowText(L"Manage file-transfer connections, local hosting, and network tools.");
    } else if (index == kNetworkIndex) {
        pageContextHint_.SetWindowText(L"Review the network interfaces and addresses available to GTH.");
    } else if (index == kLogsIndex) {
        pageContextHint_.SetWindowText(L"Review recent application activity and diagnostic messages.");
    } else if (index == kReportsIndex) {
        pageContextHint_.SetWindowText(L"Review summaries of your game library and storage.");
    } else if (index == kEnterpriseIndex) {
        pageContextHint_.SetWindowText(L"Manage central-server and agent connections.");
    } else if (index == kBackupsIndex) {
        pageContextHint_.SetWindowText(L"Protect your local game library and restore it when you need it.");
    } else if (index == kSettingsIndex) {
        pageContextHint_.SetWindowText(L"Adjust appearance, access, automation, and advanced tools.");
    }
}

void MainFrame::layout() {
    CRect clientRect;
    GetClientRect(&clientRect);

    if (sidebar_.IsWindow())
        sidebar_.MoveWindow(0, kShellHeaderHeight, kSidebarWidth, clientRect.Height() - kFooterHeight - kShellHeaderHeight);
    if (footer_.IsWindow()) {
        footer_.MoveWindow(0, clientRect.Height() - kFooterHeight, kSidebarWidth, kFooterHeight);
    }

    if (navToggleButton_.IsWindow()) {
        navToggleButton_.SetWindowText(sidebarVisible_ ? L"\x2630 Hide" : L"\x2630 Menu");
        navToggleButton_.MoveWindow(12, 16, 78, 32);
    }
    const int contextWidth = std::max(0, static_cast<int>(clientRect.right) - 350 - 100);
    if (pageContextLabel_.IsWindow()) pageContextLabel_.MoveWindow(100, 12, contextWidth, 24);
    if (pageContextHint_.IsWindow()) pageContextHint_.MoveWindow(100, 37, contextWidth, 20);
    if (shellStatusLabel_.IsWindow()) shellStatusLabel_.MoveWindow(clientRect.right - 335, 15, 85, 23);
    if (notificationButton_.IsWindow()) notificationButton_.MoveWindow(clientRect.right - 240, 13, 122, 32);
    if (globalQueueButton_.IsWindow()) globalQueueButton_.MoveWindow(clientRect.right - 108, 13, 80, 32);

    const int contentLeft = sidebarVisible_ ? kSidebarWidth : 0;
    CRect contentRect(contentLeft, kShellHeaderHeight, clientRect.right, clientRect.bottom);
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
