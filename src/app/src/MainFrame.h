#pragma once

#include <array>
#include <filesystem>
#include <vector>

#include "WtlCommon.h"
#include "ControlIds.h"
#include "PosterCache.h"
#include "SharedArchiveState.h"
#include "Theme.h"
#include "DashboardView.h"
#include "StorageView.h"
#include "GamesView.h"
#include "JbGamesView.h"
#include "UpdatesView.h"
#include "DownloadsView.h"
#include "NetworkView.h"
#include "ConsolesView.h"
#include "TransfersView.h"
#include "FtpView.h"
#include "LogsView.h"
#include "ReportsView.h"
#include "EnterpriseView.h"
#include "SettingsView.h"
#include "BackupsView.h"
#include "ComingSoonView.h"
#include "ModernButton.h"
#include "SidebarTabs.h"
#include "gth/application/devices/IConsoleManager.h"
#include "gth/application/enterprise/IAgentService.h"
#include "gth/application/enterprise/ICentralServerService.h"
#include "gth/application/indexing/IGameIndexer.h"
#include "gth/application/indexing/IGameLibraryService.h"
#include "gth/application/indexing/IGamePosterService.h"
#include "gth/application/reports/IReportService.h"
#include "gth/application/storage/IStorageService.h"
#include "gth/application/transfer/ITransferQueueService.h"
#include "gth/application/ftp/IFtpConnectionService.h"
#include "gth/application/ftp/IFtpTransferService.h"
#include "gth/application/ftp/IFtpServerService.h"
#include "gth/application/ftp/ICdnUrlService.h"
#include "gth/application/accelerator/IDownloadAcceleratorService.h"
#include "gth/application/settings/IAppSettingsService.h"
#include "gth/application/auth/IAdminAuthService.h"
#include "gth/acceleratorserver/IDownloadAcceleratorServerFactory.h"
#include "gth/application/updates/IUpdateDetector.h"
#include "gth/application/updates/IUpdateJobService.h"
#include "gth/application/indexing/IPackageSync.h"
#include "gth/application/indexing/IUpdateChecker.h"
#include "gth/core/index/ITitleLookupService.h"
#include "gth/core/network/IFirewallService.h"
#include "gth/core/network/ICoverArtClient.h"
#include "gth/core/network/INetworkInterfaceDetector.h"

// Posted to the frame at the end of startup so the firewall fix runs once the
// window is up, not in the middle of creating it.
constexpr UINT kAutoAllowFirewallMessage = WM_APP + 41;
constexpr UINT kSidebarToggleGroupMessage = WM_APP + 42;

class MainFrame : public CFrameWindowImpl<MainFrame> {
public:
    DECLARE_FRAME_WND_CLASS(L"GameTransferHubMainFrame", 0)

    MainFrame(gth::application::IStorageService& storageService, gth::application::IGameIndexer& gameIndexer,
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
              gth::core::IFirewallService& firewallService, gth::core::ITitleLookupService& titleLookupService,
              gth::application::IUpdateChecker& updateChecker, std::filesystem::path sonyLinksPath,
              gth::application::IPackageSync& packageSync, std::filesystem::path sonyAutomationPath,
              gth::core::ICoverArtClient& coverArtClient, gth::application::BackupService& backupService,
              gth::network::TelegramBackupTransport& telegramBackupTransport,
              gth::core::ICredentialStore& credentialStore, std::filesystem::path telegramChatIdPath,
              std::filesystem::path defaultRestoreRoot);

    BEGIN_MSG_MAP(MainFrame)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(kAutoAllowFirewallMessage, OnAutoAllowFirewall)
        MESSAGE_HANDLER(WM_CTLCOLORLISTBOX, OnCtlColorListBox)
        MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)
        MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(kSidebarToggleGroupMessage, OnSidebarToggleGroup)
        COMMAND_HANDLER(IDC_SIDEBAR_LIST, LBN_SELCHANGE, OnSidebarSelChange)
        COMMAND_HANDLER(IDC_NAV_TOGGLE_BUTTON, BN_CLICKED, OnNavToggleClick)
        COMMAND_HANDLER(IDC_GLOBAL_QUEUE_BUTTON, BN_CLICKED, OnGlobalQueueClick)
        COMMAND_HANDLER(IDC_GLOBAL_NOTIFICATIONS_BUTTON, BN_CLICKED, OnNotificationsClick)
        CHAIN_MSG_MAP(CFrameWindowImpl<MainFrame>)
    END_MSG_MAP()

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnAutoAllowFirewall(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCtlColorListBox(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMeasureItem(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDrawItem(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSidebarToggleGroup(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSidebarSelChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnNavToggleClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnGlobalQueueClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnNotificationsClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    static LRESULT CALLBACK SidebarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR subclassId, DWORD_PTR referenceData);

    void showView(int index);
    void layout();
    void applyRoundedCorners();
    void populateSidebar(int keepLogicalIndex);
    void applyThemeToWindow();
    void onSettingsChanged();
    void selectSidebarLogical(int logicalIndex);
    void toggleSidebarGroup(int groupIndex, bool keepGroupFocused);
    void updateNotificationBell(int count);

    gth::application::IStorageService& storageService_;
    gth::application::IGameIndexer& gameIndexer_;
    gth::application::IGameLibraryService& gameLibrary_;
    gth::application::IGamePosterService& gamePosterService_;
    gth::application::IUpdateDetector& updateDetector_;
    gth::application::IUpdateJobService& updateJobService_;
    gth::core::INetworkInterfaceDetector& networkInterfaceDetector_;
    gth::application::IConsoleManager& consoleManager_;
    gth::application::ITransferQueueService& transferQueue_;
    gth::application::IReportService& reportService_;
    std::filesystem::path logDirectory_;
    gth::application::ICentralServerService& centralServerService_;
    gth::application::IAgentService& agentService_;
    gth::application::IFtpConnectionService& ftpConnectionService_;
    gth::application::IFtpTransferService& ftpTransferService_;
    gth::application::IFtpServerService& ftpServerService_;
    gth::application::ICdnUrlService& cdnUrlService_;
    gth::application::IDownloadAcceleratorService& downloadAcceleratorService_;
    gth::application::IAppSettingsService& appSettingsService_;
    gth::application::IAdminAuthService& adminAuthService_;
    gth::acceleratorserver::IDownloadAcceleratorServerFactory& downloadAcceleratorServerFactory_;
    gth::core::IFirewallService& firewallService_;
    gth::core::ITitleLookupService& titleLookupService_;
    gth::application::IUpdateChecker& updateChecker_;
    std::filesystem::path sonyLinksPath_;
    gth::application::IPackageSync& packageSync_;
    std::filesystem::path sonyAutomationPath_;
    gth::core::ICoverArtClient& coverArtClient_;

    // Shared by the Games and Updates tabs: one running job at a time, and the latest Sony verdicts.
    SharedArchiveState sharedState_;
    // Also shared by the Games and Updates tabs' poster grids - one GDI+ lifetime and one disk
    // cache for the whole app, not one per view.
    PosterCache posterCache_;

    CListBox sidebar_;
    // Rights/credit footer beneath the sidebar's nav list - always visible, unlike the splash
    // screen's own copy of the same wording, which is only up for a couple of seconds at launch.
    CStatic footer_;
    CStatic pageContextLabel_;
    CStatic pageContextHint_;
    CStatic shellStatusLabel_;
    ModernButton navToggleButton_;
    ModernButton globalQueueButton_;
    ModernButton notificationButton_;
    int notificationCount_ = 0;
    // Position in sidebar_ -> logical kXIndex, rebuilt by populateSidebar().
    // Negative values identify expandable section rows; nonnegative values
    // are stable page indices. The sidebar only contains a filtered subset
    // of the page list, so row positions are never used as route indices.
    std::vector<int> visibleLogicalIndices_;
    std::array<bool, kSidebarGroupCount> expandedSidebarGroups_{{true, false, false, false, false}};
    bool sidebarVisible_ = true;
    int currentLogicalIndex_ = kSidebarDashboardIndex;
    int pressedSidebarGroup_ = -1;

    DashboardView dashboardView_;
    GamesView gamesView_;
    JbGamesView jbGamesView_;
    UpdatesView updatesView_;
    DownloadsView downloadsView_;
    StorageView storageView_;
    ConsolesView consolesView_;
    TransfersView transfersView_;
    FtpView ftpView_;
    NetworkView networkView_;
    LogsView logsView_;
    ReportsView reportsView_;
    EnterpriseView enterpriseView_;
    SettingsView settingsView_;
    BackupsView backupsView_;
    ComingSoonView comingSoonView_;
    HWND currentView_ = nullptr;
};
