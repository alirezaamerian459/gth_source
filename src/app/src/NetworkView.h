#pragma once

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "Theme.h"
#include "gth/core/network/INetworkInterfaceDetector.h"

class NetworkView : public CWindowImpl<NetworkView> {
public:
    DECLARE_WND_CLASS(L"GthNetworkView")

    BEGIN_MSG_MAP(NetworkView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        COMMAND_ID_HANDLER(IDC_REFRESH_NETWORK_BUTTON, OnRefreshClick)
    END_MSG_MAP()

    void init(gth::core::INetworkInterfaceDetector& detector) { detector_ = &detector; }
    void refresh();

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnRefreshClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);

    gth::core::INetworkInterfaceDetector* detector_ = nullptr;

    CStatic titleLabel_;
    ModernButton refreshButton_;
    CListViewCtrl listView_;
};
