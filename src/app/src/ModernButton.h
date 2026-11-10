#pragma once

#include "Theme.h"
#include "WtlCommon.h"

// A drop-in replacement for CButton that paints a rounded, flat, hover/press
// aware button instead of the classic Win32 gray rectangle - standard
// controls don't get Windows 11's rounded-corner treatment just from the
// ComCtl32 v6 manifest (that's opt-in per-control, not automatic), so this
// repaints it by hand. Superclasses the real "Button" window class (WTL's
// DECLARE_WND_SUPERCLASS) rather than owner-drawing via the parent's
// WM_DRAWITEM, so every existing `CButton foo_;` in a view becomes
// `ModernButton foo_;` with no other code changes - Create()/EnableWindow()/
// BN_CLICKED notifications all keep working exactly as before.
class ModernButton : public CWindowImpl<ModernButton, CButton> {
public:
    DECLARE_WND_SUPERCLASS(L"GthModernButton", CButton::GetWndClassName())

    enum class Kind { Primary, Secondary, Subtle, Danger };

    BEGIN_MSG_MAP(ModernButton)
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

    void setKind(Kind kind) { kind_ = kind; }

private:
    Kind kind_ = Kind::Secondary;
    bool hovering_ = false;
    bool pressed_ = false;
    bool tracking_ = false;

    LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) { return 1; }  // painted fully in OnPaint - no flicker

    LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);

        CRect rect;
        GetClientRect(&rect);

        const bool enabled = IsWindowEnabled();
        COLORREF fill, textColor, border = CLR_INVALID;

        switch (kind_) {
            case Kind::Primary:
                if (!enabled) {
                    fill = theme::DisabledFill();
                } else if (pressed_) {
                    fill = theme::AccentPressed();
                } else if (hovering_) {
                    fill = theme::AccentHover();
                } else {
                    fill = theme::Accent();
                }
                textColor = !enabled ? theme::TextDisabled() : theme::TextOnAccent();
                break;
            case Kind::Danger:
                fill = !enabled ? theme::DisabledFill() : theme::Danger();
                textColor = !enabled ? theme::TextDisabled() : theme::TextOnAccent();
                break;
            case Kind::Subtle:
                fill = (pressed_ || hovering_) ? theme::HoverBg() : theme::ContentBg();
                textColor = !enabled ? theme::TextDisabled() : theme::Accent();
                break;
            case Kind::Secondary:
            default:
                fill = (pressed_ || hovering_) ? theme::HoverBg() : theme::ContentBg();
                textColor = !enabled ? theme::TextDisabled() : theme::TextPrimary();
                border = theme::Border();
                break;
        }

        theme::FillRoundRect(hdc, rect, theme::kRadiusButton, fill, border);

        if (::GetFocus() == m_hWnd && enabled) {
            CRect focusRect = rect;
            focusRect.DeflateRect(1, 1);
            HPEN focusPen = ::CreatePen(PS_SOLID, 1, theme::Accent());
            HGDIOBJ oldPen = ::SelectObject(hdc, focusPen);
            HGDIOBJ oldBrush = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
            ::RoundRect(hdc, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom, theme::kRadiusButton,
                        theme::kRadiusButton);
            ::SelectObject(hdc, oldBrush);
            ::SelectObject(hdc, oldPen);
            ::DeleteObject(focusPen);
        }

        const int len = GetWindowTextLength();
        std::wstring text(static_cast<size_t>(len), L'\0');
        if (len > 0) GetWindowText(text.data(), len + 1);
        HFONT font = std::bit_cast<HFONT>(SendMessage(WM_GETFONT, 0, 0));
        theme::DrawCenteredText(hdc, rect, text, font ? font : theme::BodyFont(), textColor);

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
        pressed_ = false;
        Invalidate();
        return 0;
    }

    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
        pressed_ = true;
        Invalidate();
        bHandled = FALSE;  // let CButton's default handling still fire BN_CLICKED etc.
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
        pressed_ = false;
        Invalidate();
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
