#pragma once

#include "Theme.h"
#include "WtlCommon.h"

// Drop-in replacement for CButton with BS_AUTOCHECKBOX. Standard checkboxes
// paint their box glyph as a light/white square regardless of app theme
// (that's baked into ComCtl32's visual-style rendering, not something a
// WM_CTLCOLORBTN brush return can override) - the one native control left
// that could put white on screen in dark mode after ModernButton/
// ApplyListViewTheme/HandleCtlColorEdit already covered buttons, list views,
// and text fields. Same superclassing trick as ModernButton
// (DECLARE_WND_SUPERCLASS over the real "Button" class): Create()/GetCheck()/
// SetCheck()/BN_CLICKED all keep working exactly as before, so an existing
// `CButton foo_;` becomes `ModernCheckBox foo_;` with no other code changes.
class ModernCheckBox : public CWindowImpl<ModernCheckBox, CButton> {
public:
    DECLARE_WND_SUPERCLASS(L"GthModernCheckBox", CButton::GetWndClassName())

    BEGIN_MSG_MAP(ModernCheckBox)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_SETFOCUS, OnFocusChanged)
        MESSAGE_HANDLER(WM_KILLFOCUS, OnFocusChanged)
        MESSAGE_HANDLER(WM_ENABLE, OnEnableChanged)
    END_MSG_MAP()

private:
    static constexpr int kBoxSize = 16;

    bool hovering_ = false;
    bool tracking_ = false;

    LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) { return 1; }  // painted fully in OnPaint - no flicker

    LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);

        CRect rect;
        GetClientRect(&rect);
        theme::FillRoundRect(hdc, rect, 0, theme::ContentBg());

        const bool enabled = IsWindowEnabled();
        const int check = GetCheck();

        const int boxTop = rect.top + (rect.Height() - kBoxSize) / 2;
        CRect boxRect(rect.left, boxTop, rect.left + kBoxSize, boxTop + kBoxSize);

        const bool checked = check == BST_CHECKED || check == BST_INDETERMINATE;
        const COLORREF boxFill = checked ? (enabled ? theme::Accent() : theme::DisabledFill()) : theme::EditBg();
        const COLORREF boxBorder = checked ? boxFill : theme::Border();
        theme::FillRoundRect(hdc, boxRect, 3, boxFill, boxBorder);

        if (checked) {
            HPEN pen = ::CreatePen(PS_SOLID, 2, theme::TextOnAccent());
            HGDIOBJ oldPen = ::SelectObject(hdc, pen);
            ::MoveToEx(hdc, boxRect.left + 3, boxRect.top + 8, nullptr);
            ::LineTo(hdc, boxRect.left + 6, boxRect.top + 11);
            ::LineTo(hdc, boxRect.left + 13, boxRect.top + 4);
            ::SelectObject(hdc, oldPen);
            ::DeleteObject(pen);
        }

        if (hovering_ && enabled) {
            HPEN pen = ::CreatePen(PS_SOLID, 1, theme::Accent());
            HGDIOBJ oldPen = ::SelectObject(hdc, pen);
            HGDIOBJ oldBrush = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
            ::RoundRect(hdc, boxRect.left - 1, boxRect.top - 1, boxRect.right + 1, boxRect.bottom + 1, 4, 4);
            ::SelectObject(hdc, oldBrush);
            ::SelectObject(hdc, oldPen);
            ::DeleteObject(pen);
        }

        CRect textRect(boxRect.right + 8, rect.top, rect.right, rect.bottom);
        const int len = GetWindowTextLength();
        std::wstring text(static_cast<size_t>(len), L'\0');
        if (len > 0) GetWindowText(text.data(), len + 1);

        HGDIOBJ oldFont = ::SelectObject(hdc, theme::BodyFont());
        const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
        const COLORREF oldColor = ::SetTextColor(hdc, enabled ? theme::TextPrimary() : theme::TextDisabled());
        ::DrawText(hdc, text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ::SetTextColor(hdc, oldColor);
        ::SetBkMode(hdc, oldMode);
        ::SelectObject(hdc, oldFont);

        if (::GetFocus() == m_hWnd && enabled) {
            HPEN focusPen = ::CreatePen(PS_SOLID, 1, theme::Accent());
            HGDIOBJ oldFP = ::SelectObject(hdc, focusPen);
            HGDIOBJ oldFB = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
            CRect focusRect = rect;
            focusRect.DeflateRect(0, 1);
            ::Rectangle(hdc, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom);
            ::SelectObject(hdc, oldFB);
            ::SelectObject(hdc, oldFP);
            ::DeleteObject(focusPen);
        }

        EndPaint(&ps);
        return 0;
    }

    LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&) {
        if (!tracking_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hWnd, 0};
            ::TrackMouseEvent(&tme);
            tracking_ = true;
        }
        if (!hovering_) {
            hovering_ = true;
            Invalidate();
        }
        return 0;
    }

    LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
        tracking_ = false;
        hovering_ = false;
        Invalidate();
        return 0;
    }

    LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL& bHandled) {
        bHandled = FALSE;  // let CButton's default AUTOCHECKBOX toggle + BN_CLICKED still fire
        return 0;
    }

    LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL& bHandled) {
        Invalidate();  // reflect the new check state the default proc is about to set
        bHandled = FALSE;
        return 0;
    }

    LRESULT OnFocusChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
        Invalidate();
        bHandled = FALSE;
        return 0;
    }

    LRESULT OnEnableChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
        Invalidate();
        bHandled = FALSE;
        return 0;
    }
};
