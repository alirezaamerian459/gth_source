#pragma once

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "Theme.h"
#include "gth/application/devices/IConsoleManager.h"

class ConsolesView : public CWindowImpl<ConsolesView> {
public:
    DECLARE_WND_CLASS(L"GthConsolesView")

    BEGIN_MSG_MAP(ConsolesView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
        COMMAND_ID_HANDLER(IDC_ADD_CONSOLE_BUTTON, OnAddClick)
        COMMAND_ID_HANDLER(IDC_CHECK_CONSOLE_BUTTON, OnCheckClick)
        COMMAND_ID_HANDLER(IDC_REMOVE_CONSOLE_BUTTON, OnRemoveClick)
        COMMAND_ID_HANDLER(IDC_CONSOLES_ADD_PANEL_BUTTON, OnAddPanelClick)
    END_MSG_MAP()

    void init(gth::application::IConsoleManager& manager) { manager_ = &manager; }
    void refresh();

private:
    // Auto-detected rows (see IConsoleManager::recordConnection) can appear
    // at any time, driven by a background proxy thread - without this timer,
    // a console connecting while the user is already sitting on this tab
    // wouldn't show up until they navigated away and back. Mirrors FtpView's
    // kLiveRefreshTimerId/kLiveRefreshIntervalMs for the same reason.
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
    LRESULT OnAddClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCheckClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnRemoveClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnAddPanelClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    void layoutControls();

    std::string selectedConsoleId();
    std::wstring getEditText(CEdit& edit);

    gth::application::IConsoleManager* manager_ = nullptr;

    CStatic titleLabel_;
    ModernButton addPanelButton_;

    // Inline "add console" fields, always visible above the list - avoids
    // needing a modal dialog/resource template for a handful of text inputs.
    CStatic nameCaption_, platformCaption_, ipCaption_, portCaption_;
    CEdit nameEdit_, platformEdit_, ipEdit_, portEdit_;
    ModernButton addButton_;

    ModernButton checkButton_;
    ModernButton removeButton_;
    CStatic statusLabel_;
    CListViewCtrl listView_;

    // Cached result of the last getAllConsoles() call, in the same row order
    // as listView_ - lets selectedConsoleId() map a selected row back to a
    // consoleId without a redundant DB round-trip, and lets refresh() restore
    // the selection it had before a periodic (timer-driven) repopulate.
    std::vector<gth::core::Console> lastConsoles_;
    bool addConsoleExpanded_ = false;
};
