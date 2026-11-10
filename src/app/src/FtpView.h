#pragma once

#include <memory>
#include <vector>

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "NetworkInfoHelper.h"
#include "ScrollSupport.h"
#include "StatusDot.h"
#include "Theme.h"
#include "gth/application/ftp/IFtpConnectionService.h"
#include "gth/application/ftp/IFtpTransferService.h"
#include "gth/application/ftp/IFtpServerService.h"
#include "gth/application/ftp/ICdnUrlService.h"
#include "gth/application/accelerator/IDownloadAcceleratorService.h"
#include "gth/acceleratorserver/IDownloadAcceleratorServerFactory.h"
#include "gth/application/auth/IAdminAuthService.h"
#include "gth/application/settings/IAppSettingsService.h"

// "FTP" tab: connection list/manager panel (add/connect/disconnect/remove),
// once connected a remote directory browse pane with Upload/Download, an
// optional local "Host a Server" panel, and - folded into the same page
// rather than a separate sidebar entry, since two half-empty tabs for
// closely related "get files onto/off this console over the network" jobs
// was more sidebar than the feature warranted - the Accelerator's CDN
// mirror list and the LAN Console Proxy. Kept entirely separate from
// TransfersView's local/SMB queue per the plan (this drives
// IFtpTransferService directly rather than going through
// ITransferQueueService).
class FtpView : public CWindowImpl<FtpView> {
public:
    DECLARE_WND_CLASS(L"GthFtpView")

    BEGIN_MSG_MAP(FtpView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
        MESSAGE_HANDLER(WM_CTLCOLORLISTBOX, OnCtlColorEdit)
        MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        COMMAND_ID_HANDLER(IDC_FTP_ADD_BUTTON, OnAddClick)
        COMMAND_ID_HANDLER(IDC_FTP_CONNECT_BUTTON, OnConnectClick)
        COMMAND_ID_HANDLER(IDC_FTP_DISCONNECT_BUTTON, OnDisconnectClick)
        COMMAND_ID_HANDLER(IDC_FTP_REMOVE_BUTTON, OnRemoveClick)
        COMMAND_ID_HANDLER(IDC_FTP_UPLOAD_BUTTON, OnUploadClick)
        COMMAND_ID_HANDLER(IDC_FTP_DOWNLOAD_BUTTON, OnDownloadClick)
        COMMAND_ID_HANDLER(IDC_FTP_REFRESH_BUTTON, OnRefreshBrowseClick)
        COMMAND_ID_HANDLER(IDC_FTP_HOST_BROWSE_BUTTON, OnHostBrowseClick)
        COMMAND_ID_HANDLER(IDC_FTP_HOST_START_BUTTON, OnHostStartClick)
        COMMAND_ID_HANDLER(IDC_FTP_HOST_STOP_BUTTON, OnHostStopClick)
        COMMAND_ID_HANDLER(IDC_ACCEL_ADD_MIRROR_BUTTON, OnAddMirrorClick)
        COMMAND_ID_HANDLER(IDC_ACCEL_REMOVE_MIRROR_BUTTON, OnRemoveMirrorClick)
        COMMAND_ID_HANDLER(IDC_ACCEL_MEASURE_BUTTON, OnMeasureClick)
        COMMAND_ID_HANDLER(IDC_ACCEL_CACHE_BROWSE_BUTTON, OnCacheBrowseClick)
        COMMAND_ID_HANDLER(IDC_ACCEL_CLEAR_CACHE_BUTTON, OnClearCacheClick)
        COMMAND_ID_HANDLER(IDC_CDN_URL_COPY_BUTTON, OnCopyUrlClick)
        COMMAND_ID_HANDLER(IDC_FTP_ADVANCED_TOOLS_BUTTON, OnAdvancedToolsClick)
        COMMAND_ID_HANDLER(IDC_FTP_HOST_COPY_ADDRESS_BUTTON, OnHostCopyAddressClick)
        COMMAND_ID_HANDLER(IDC_CDN_URL_ADD_BUTTON, OnAddCdnUrlClick)
        COMMAND_ID_HANDLER(IDC_CDN_URL_REMOVE_BUTTON, OnRemoveCdnUrlClick)
        COMMAND_ID_HANDLER(IDC_CDN_URL_BROWSE_BUTTON, OnCdnUrlBrowseClick)
        COMMAND_ID_HANDLER(IDC_PROXY_START_BUTTON, OnProxyStartClick)
        COMMAND_ID_HANDLER(IDC_PROXY_STOP_BUTTON, OnProxyStopClick)
        COMMAND_ID_HANDLER(IDC_MITM_ADD_BUTTON, OnAddMitmInstanceClick)
        COMMAND_RANGE_HANDLER(IDC_MITM_INSTANCE_FIRST, IDC_MITM_INSTANCE_LAST, OnMitmInstanceButtonClick)
        NOTIFY_HANDLER(IDC_CDN_URL_LISTVIEW, NM_DBLCLK, OnCdnUrlListDblClick)
        NOTIFY_HANDLER(IDC_CDN_URL_LISTVIEW, LVN_ITEMCHANGED, OnCdnUrlSelectionChanged)
        COMMAND_ID_HANDLER(IDC_CDN_URL_MARK_EDIT, OnCdnUrlMarkChanged)
        COMMAND_ID_HANDLER(IDC_CDN_URL_LOCAL_PATH_EDIT, OnCdnUrlLocalPathChanged)
    END_MSG_MAP()

    void init(gth::application::IFtpConnectionService& connectionService,
              gth::application::IFtpTransferService& transferService,
              gth::application::IFtpServerService& ftpServerService,
              gth::application::IDownloadAcceleratorService& downloadAcceleratorService,
              gth::application::ICdnUrlService& cdnUrlService,
              gth::application::IAdminAuthService& adminAuthService,
              gth::acceleratorserver::IDownloadAcceleratorServerFactory& downloadAcceleratorServerFactory,
              gth::application::IAppSettingsService& appSettingsService) {
        connectionService_ = &connectionService;
        transferService_ = &transferService;
        ftpServerService_ = &ftpServerService;
        acceleratorService_ = &downloadAcceleratorService;
        cdnUrlService_ = &cdnUrlService;
        adminAuthService_ = &adminAuthService;
        downloadAcceleratorServerFactory_ = &downloadAcceleratorServerFactory;
        appSettingsService_ = &appSettingsService;
    }
    void refresh();
    void setCdnUrl(const std::wstring& url) {
        if (cdnUrlEdit_.IsWindow()) {
            cdnUrlEdit_.SetWindowText(url.c_str());
        }
    }

private:
    // The CDN URL Log and the HTTP proxy status dot are both populated by a
    // background proxy thread (console traffic can arrive at any time), but
    // refresh() previously only ran when the user navigated onto this tab -
    // once shown, the list and the "Running" dot never updated again even
    // as new URLs were captured or the proxy died, making a live proxy look
    // identical to a dead one. This timer polls both while the tab exists.
    static constexpr UINT_PTR kLiveRefreshTimerId = 1;
    static constexpr UINT kLiveRefreshIntervalMs = 3000;

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
        KillTimer(kLiveRefreshTimerId);
        bHandled = FALSE;
        return 0;
    }
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCtlColorEdit(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnVScroll(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
        return scroll_.onVScroll(uMsg, wParam, lParam, bHandled);
    }
    LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
        return scroll_.onMouseWheel(uMsg, wParam, lParam, bHandled);
    }
    LRESULT OnAddClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnConnectClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnDisconnectClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnRemoveClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnUploadClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnDownloadClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnRefreshBrowseClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnHostBrowseClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnHostStartClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnHostStopClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnHostCopyAddressClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnAddMirrorClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnRemoveMirrorClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnMeasureClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCacheBrowseClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnClearCacheClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCopyUrlClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnAddCdnUrlClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnRemoveCdnUrlClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCdnUrlBrowseClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnProxyStartClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnProxyStopClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCdnUrlListDblClick(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
    LRESULT OnCdnUrlSelectionChanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
    LRESULT OnCdnUrlMarkChanged(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCdnUrlLocalPathChanged(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnAddMitmInstanceClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnMitmInstanceButtonClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnAdvancedToolsClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);

    // Hides the admin-only sections (FTP connection manager, Host a
    // Server/browse pane, the Accelerator's mirror list) for a signed-out
    // user. The LAN Console Proxy + CDN URL Log stay visible for everyone:
    // for a signed-out user the proxy section moves up under the page
    // header; restores the full layout for an admin.
    void applyAccessControl(bool isAdmin);

    // Absolute-position relayout of everything from the LAN Console Proxy
    // section onward - the original section's controls, every dynamic
    // MitmInstance row, the "Add Console" button, and the CDN URL Log
    // section - given the current admin-compact/full state and however
    // many extra instances currently exist. Called after add/remove and
    // from applyAccessControl() instead of the old fixed-delta shift, since
    // CDN's position now depends on instance count too, not just admin
    // state.
    void relayoutMitmAndCdnSections();
    void applyAdvancedToolsVisibility();
    void addMitmInstance();
    void removeMitmInstance(size_t index);

    void refreshConnectionsList();
    void refreshBrowseList();
    void refreshHostStatus();
    void refreshMirrorsList();
    void refreshAcceleratorStatus();
    void refreshCdnUrlsList();
    void saveCdnUrlEdits();
    std::string selectedConnectionId();
    std::string selectedMirrorId();
    std::wstring getEditText(CEdit& edit);
    void copyTextToClipboard(const std::wstring& text);

    gth::application::IFtpConnectionService* connectionService_ = nullptr;
    gth::application::IFtpTransferService* transferService_ = nullptr;
    gth::application::IFtpServerService* ftpServerService_ = nullptr;
    gth::application::IDownloadAcceleratorService* acceleratorService_ = nullptr;
    gth::application::ICdnUrlService* cdnUrlService_ = nullptr;
    gth::application::IAdminAuthService* adminAuthService_ = nullptr;
    gth::acceleratorserver::IDownloadAcceleratorServerFactory* downloadAcceleratorServerFactory_ = nullptr;
    gth::application::IAppSettingsService* appSettingsService_ = nullptr;

    std::vector<gth::core::FtpConnection> lastConnections_;
    std::vector<gth::core::FtpDirectoryEntry> lastEntries_;
    std::vector<gth::core::CdnMirror> lastMirrors_;
    std::vector<gth::core::CdnUrl> cdnUrls_;
    // Which CDN URL row the Mark/Local Path boxes are currently editing. Tracked by id (not row
    // index) because the live-refresh timer rebuilds the list underneath the user.
    std::string editingCdnUrlId_;
    bool suppressCdnEdit_ = false;
    std::string currentBrowsePath_ = "/";
    // Mirrors what the status dot currently shows, so the timer only touches
    // the label/dot when isRunning() actually changed instead of repainting
    // (and needlessly overwriting the IP text) every tick.
    bool lastKnownProxyRunning_ = false;

    // Two fixed baselines for where the LAN Console Proxy section starts:
    // "full" (below the three admin-only sections, the OnCreate-time
    // position) and "compact" (right under the page header, used when
    // those three sections are hidden). Captured once in OnCreate. Content
    // height is *not* cached alongside these anymore - once instances can
    // be added/removed at runtime, the section's total height depends on
    // mitmInstances_.size() too, so relayoutMitmAndCdnSections() computes
    // it fresh on every call instead of toggling between two precomputed
    // values.
    int httpProxyTopFull_ = 0;
    int httpProxyTopCompact_ = 0;
    int advancedToolsTop_ = 0;
    bool sections45Compact_ = false;
    bool advancedToolsExpanded_ = false;

    // Set once during OnCreate - every section below the FTP fields stacks
    // on a fixed height rather than filling to the bottom (the page can run
    // taller than the viewport now that Accelerator lives on it too; the
    // window just clips/needs maximizing rather than reflowing).
    int browseListTop_ = 0;

    ScrollSupport scroll_;

    CStatic titleLabel_;
    ModernButton advancedToolsButton_;

    CStatic nameCaption_, hostCaption_, portCaption_, protocolCaption_, userCaption_, passCaption_, pathCaption_;
    CEdit nameEdit_, hostEdit_, portEdit_, userEdit_, passEdit_, pathEdit_;
    CComboBox protocolCombo_;
    ModernButton addButton_;

    ModernButton connectButton_, disconnectButton_, removeButton_;
    StatusDot statusDot_;
    CStatic statusLabel_;
    CListViewCtrl connectionsListView_;

    CStatic hostSectionCaption_, hostFolderCaption_, hostPortCaption_;
    CEdit hostFolderEdit_, hostPortEdit_;
    ModernButton hostBrowseButton_, hostStartButton_, hostStopButton_;
    StatusDot hostStatusDot_;
    CStatic hostStatusLabel_;
    ModernButton hostCopyAddressButton_;
    // "ip:port" for whichever address refreshHostStatus() last displayed -
    // kept separate from hostStatusLabel_'s full sentence so the Copy
    // button copies just the address, not the surrounding instructions.
    std::wstring lastHostAddress_;

    CStatic browseCaption_, localPathCaption_;
    CEdit localPathEdit_;
    ModernButton uploadButton_, downloadButton_, refreshBrowseButton_;
    CListViewCtrl browseListView_;

    // Accelerator section - CDN mirror candidates (the LAN Console Proxy
    // section below is the other half of IDownloadAcceleratorService).
    CStatic acceleratorSectionCaption_;
    StatusDot acceleratorSectionDot_;
    CStatic mirrorHostCaption_, mirrorIpCaption_, mirrorLabelCaption_;
    CEdit mirrorHostEdit_, mirrorIpEdit_, mirrorLabelEdit_;
    ModernButton addMirrorButton_;
    ModernButton measureButton_, removeMirrorButton_;
    CStatic mirrorsStatusLabel_;
    CListViewCtrl mirrorsListView_;

    // LAN Console Proxy section - point a console's network proxy setting at this machine's address:port to get
    // console-tracking, CDN URL logging, response caching, CDN-mirror substitution and local-library serving all
    // from one listener (see WinsockCachingProxyServer) - previously two separate proxies on two separate ports.
    CStatic httpProxySectionCaption_;
    CStatic httpProxyIpLabel_;
    // Auto-filled with the detected LAN IP at OnCreate, but genuinely
    // editable - autodetection isn't always right on a multi-adapter
    // machine (see this app's own troubleshooting history), so the user
    // can type over it. Once a proxy Start/Stop/failure path used to
    // silently overwrite this with "Not running"/"Stopped"/etc, which is
    // fine for a read-only display but would wipe out a manually-typed IP;
    // those paths now leave this field alone and only touch the status
    // dot/label instead.
    CEdit httpProxyIpDisplay_;
    CStatic httpProxyPortCaption_;
    CEdit httpProxyPortEdit_;
    ModernButton proxyStartButton_, proxyStopButton_;
    // Second field row: the accelerator's on-disk response cache - shared by
    // every "+ Add Console" instance below too (see
    // IDownloadAcceleratorServerFactory), not just this original instance.
    CStatic cacheFolderCaption_;
    CEdit cacheFolderEdit_;
    ModernButton cacheBrowseButton_, clearCacheButton_;
    StatusDot httpProxyStatusDot_;
    CStatic httpProxyStatusLabel_;

    // One extra LAN Console Proxy listener, each independently started/
    // stopped/removed - added by clicking "Add Console" when a user has
    // more than one console to capture URLs from. unique_ptr per instance
    // is load-bearing here, not just style: this vector can grow/erase
    // freely (only pointers move), while every WTL control inside a given
    // MitmInstance stays at one fixed heap address for its whole lifetime
    // once Create()'d - required, since a CWindowImpl-derived control's
    // message thunk is bound to its exact object address at creation time
    // (the same constraint SettingsView::tabCheckboxes_ already works
    // around for a single control type; this is that same rule applied to
    // a whole struct of controls).
    struct MitmInstance {
        std::unique_ptr<gth::core::IDownloadAcceleratorServer> service;
        CEdit ipEdit, portEdit;
        ModernButton startButton, stopButton, removeButton;
        StatusDot statusDot;
        CStatic statusLabel;
        bool lastKnownRunning = false;
        // This instance's reserved Start/Stop/Remove IDs (idBase, idBase+1,
        // idBase+2) - stored rather than recomputed from position in
        // mitmInstances_, since removing an instance from the middle must
        // not require renumbering the survivors' already-created controls.
        int idBase = 0;
    };
    std::vector<std::unique_ptr<MitmInstance>> mitmInstances_;
    ModernButton addMitmInstanceButton_;

    // CDN URL Log section
    CStatic cdnSectionCaption_;
    CEdit cdnUrlEdit_;
    ModernButton cdnUrlAddButton_;
    ModernButton cdnUrlBrowseButton_;
    ModernButton cdnUrlRemoveButton_;
    CListViewCtrl cdnUrlListView_;
    // Promoted from OnCreate-local variables: applyAccessControl() must be
    // able to reposition this whole section as a block, which requires
    // every one of its controls to be a member, not a local.
    CStatic cdnUrlMarkCaption_, cdnUrlLocalPathCaption_;
    CEdit cdnUrlMarkEdit_;
    CEdit cdnUrlLocalPathEdit_;
    ModernButton cdnUrlCopyButton_;
};
