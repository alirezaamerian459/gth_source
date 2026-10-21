#pragma once

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "NetworkInfoHelper.h"
#include "Theme.h"
#include "gth/application/storage/IStorageService.h"

class DashboardView : public CWindowImpl<DashboardView> {
public:
    DECLARE_WND_CLASS(L"GthDashboardView")

    BEGIN_MSG_MAP(DashboardView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        COMMAND_ID_HANDLER(IDC_SCAN_BUTTON, OnScanClick)
        COMMAND_ID_HANDLER(IDC_COPY_IP_BUTTON, OnCopyIPClick)
    END_MSG_MAP()

    void init(gth::application::IStorageService& service) { service_ = &service; }
    void refresh();

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnScanClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCopyIPClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    void copyToClipboard(const std::wstring& text);
    void refreshNetworkInfo();

    gth::application::IStorageService* service_ = nullptr;
    int cardsTop_ = 0;
    int networkCardsTop_ = 0;

    CStatic titleLabel_;
    CStatic storagesCaption_, storagesValue_, onlineOfflineValue_;
    CStatic capacityCaption_, capacityValue_;
    CStatic freeCaption_, freeValue_;
    ModernButton scanButton_;
    CStatic statusLabel_;

    // Network Connection section - shows this machine's LAN IP/port so the
    // other side of a transfer (another PC, a console's FTP client) can be
    // pointed at it without the user having to go find it themselves via
    // ipconfig.
    CStatic networkLabel_;
    CStatic ipAddressCaption_, ipAddressValue_;
    CStatic portCaption_, portValue_;
    ModernButton copyIPButton_;
    CStatic networkStatusLabel_;
};
