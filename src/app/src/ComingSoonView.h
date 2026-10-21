#pragma once

#include "WtlCommon.h"
#include "Theme.h"

class ComingSoonView : public CWindowImpl<ComingSoonView> {
public:
    DECLARE_WND_CLASS(L"GthComingSoonView")

    BEGIN_MSG_MAP(ComingSoonView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
    END_MSG_MAP()

    void setTitle(const std::wstring& title);

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void layout();

    CStatic titleLabel_;
    CStatic subtitleLabel_;
};
