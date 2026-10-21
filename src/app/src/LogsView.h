#pragma once

#include <filesystem>

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "Theme.h"

class LogsView : public CWindowImpl<LogsView> {
public:
    DECLARE_WND_CLASS(L"GthLogsView")

    BEGIN_MSG_MAP(LogsView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
        COMMAND_ID_HANDLER(IDC_REFRESH_LOGS_BUTTON, OnRefreshClick)
    END_MSG_MAP()

    void init(const std::filesystem::path& logDirectory) { logDirectory_ = logDirectory; }
    void refresh();

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCtlColorEdit(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnRefreshClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);

    std::filesystem::path todaysLogFilePath() const;

    std::filesystem::path logDirectory_;

    CStatic titleLabel_;
    ModernButton refreshButton_;
    CStatic pathLabel_;
    CEdit logTextEdit_;
};
