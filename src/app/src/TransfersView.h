#pragma once

#include <vector>

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "Theme.h"
#include "gth/application/transfer/ITransferQueueService.h"
#include "gth/core/entities/TransferItem.h"

class TransfersView : public CWindowImpl<TransfersView> {
public:
    DECLARE_WND_CLASS(L"GthTransfersView")

    BEGIN_MSG_MAP(TransfersView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
        COMMAND_ID_HANDLER(IDC_ENQUEUE_TRANSFER_BUTTON, OnEnqueueClick)
        COMMAND_ID_HANDLER(IDC_CANCEL_TRANSFER_BUTTON, OnCancelClick)
        COMMAND_ID_HANDLER(IDC_REFRESH_TRANSFERS_BUTTON, OnRefreshClick)
        COMMAND_ID_HANDLER(IDC_TRANSFERS_NEW_PANEL_BUTTON, OnNewPanelClick)
    END_MSG_MAP()

    void init(gth::application::ITransferQueueService& queue) { queue_ = &queue; }
    void refresh();

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCtlColorEdit(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEnqueueClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCancelClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnRefreshClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnNewPanelClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    void layoutControls();

    std::wstring getEditText(CEdit& edit);

    gth::application::ITransferQueueService* queue_ = nullptr;

    // One row per TransferItem (rather than per job) so progress/status is
    // visible at the granularity the queue actually reports it - a job with
    // several items shows several rows sharing its Job column value. Kept
    // parallel to the ListView rows the same way UpdatesView keeps
    // lastResults_, so OnCancelClick can map a selected row back to a
    // transferItemId without round-tripping through the ListView's own
    // storage.
    std::vector<gth::core::TransferItem> lastItems_;

    CStatic titleLabel_;

    CStatic nameCaption_, sourceCaption_, destCaption_;
    CEdit nameEdit_, sourceEdit_, destEdit_;
    ModernButton enqueueButton_;
    ModernButton newPanelButton_;

    ModernButton cancelButton_;
    ModernButton refreshButton_;
    CStatic statusLabel_;
    CListViewCtrl listView_;
    bool newTransferExpanded_ = false;
};
