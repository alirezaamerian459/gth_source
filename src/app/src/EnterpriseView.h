#pragma once

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "Theme.h"
#include "gth/application/enterprise/IAgentService.h"
#include "gth/application/enterprise/ICentralServerService.h"

// Milestone 14: Central Server / Agent. Two independent, opt-in halves on
// one page - a PC can be a server, an agent, both, or neither, since which
// role a given machine plays is a deployment decision the user makes, not
// something this app can infer.
class EnterpriseView : public CWindowImpl<EnterpriseView> {
public:
    DECLARE_WND_CLASS(L"GthEnterpriseView")

    BEGIN_MSG_MAP(EnterpriseView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
        COMMAND_ID_HANDLER(IDC_SERVER_TOGGLE_BUTTON, OnServerToggleClick)
        COMMAND_ID_HANDLER(IDC_AGENT_TOGGLE_BUTTON, OnAgentToggleClick)
        COMMAND_ID_HANDLER(IDC_REFRESH_NODES_BUTTON, OnRefreshClick)
    END_MSG_MAP()

    void init(gth::application::ICentralServerService& serverService, gth::application::IAgentService& agentService) {
        serverService_ = &serverService;
        agentService_ = &agentService;
    }
    void refresh();

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCtlColorEdit(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnServerToggleClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnAgentToggleClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnRefreshClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);

    std::wstring getEditText(CEdit& edit);
    void refreshNodesList();
    void updateToggleLabelsAndEnabledState();

    gth::application::ICentralServerService* serverService_ = nullptr;
    gth::application::IAgentService* agentService_ = nullptr;

    CStatic titleLabel_;

    CStatic serverSectionLabel_;
    CStatic serverPortCaption_;
    CEdit serverPortEdit_;
    ModernButton serverToggleButton_;
    CStatic serverStatusLabel_;

    CStatic agentSectionLabel_;
    CStatic agentAddressCaption_, agentPortCaption_, agentNameCaption_, agentIntervalCaption_;
    CEdit agentAddressEdit_, agentPortEdit_, agentNameEdit_, agentIntervalEdit_;
    ModernButton agentToggleButton_;
    CStatic agentStatusLabel_;

    CStatic nodesLabel_;
    ModernButton refreshNodesButton_;
    CListViewCtrl nodesListView_;
};
