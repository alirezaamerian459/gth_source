#pragma once

#include "Theme.h"
#include "WtlCommon.h"

// Small filled-circle + label indicator - the "is this thing on" glance
// readout used throughout PS jailbreak/debug tooling (etaHEN, package
// installers, etc: a green dot next to "PC Agent", "CDN", "Offline
// raising"...). FtpView uses this next to its Connection/Host
// Server/Accelerator status text so the state reads at a glance instead of
// requiring the status sentence to be parsed. Owner-drawn CStatic
// superclass, same pattern as ModernButton: Create()/SetWindowText() (for
// the label) keep working, setState() drives the dot color.
class StatusDot : public CWindowImpl<StatusDot, CStatic> {
public:
    DECLARE_WND_SUPERCLASS(L"GthStatusDot", CStatic::GetWndClassName())

    enum class State { Off, On, Warning };

    BEGIN_MSG_MAP(StatusDot)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
    END_MSG_MAP()

    void setState(State state) {
        if (state_ == state) return;
        state_ = state;
        if (IsWindow()) Invalidate();
    }

private:
    State state_ = State::Off;

    LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) { return 1; }  // painted fully in OnPaint - no flicker

    LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);

        CRect rect;
        GetClientRect(&rect);

        HBRUSH bgBrush = ::CreateSolidBrush(theme::SurfaceBg());
        ::FillRect(hdc, &rect, bgBrush);
        ::DeleteObject(bgBrush);

        constexpr int kDiameter = 10;
        const int dotTop = rect.top + (rect.Height() - kDiameter) / 2;
        CRect dotRect(rect.left, dotTop, rect.left + kDiameter, dotTop + kDiameter);

        COLORREF fill;
        switch (state_) {
            case State::On:
                fill = theme::Success();
                break;
            case State::Warning:
                fill = theme::Warning();
                break;
            case State::Off:
            default:
                fill = theme::Border();
                break;
        }

        HBRUSH dotBrush = ::CreateSolidBrush(fill);
        HGDIOBJ oldBrush = ::SelectObject(hdc, dotBrush);
        HGDIOBJ oldPen = ::SelectObject(hdc, ::GetStockObject(NULL_PEN));
        ::Ellipse(hdc, dotRect.left, dotRect.top, dotRect.right, dotRect.bottom);
        ::SelectObject(hdc, oldPen);
        ::SelectObject(hdc, oldBrush);
        ::DeleteObject(dotBrush);

        const int len = GetWindowTextLength();
        if (len > 0) {
            std::wstring text(static_cast<size_t>(len), L'\0');
            GetWindowText(text.data(), len + 1);

            CRect textRect(dotRect.right + 8, rect.top, rect.right, rect.bottom);
            HGDIOBJ oldFont = ::SelectObject(hdc, theme::BodyFont());
            const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
            const COLORREF oldColor = ::SetTextColor(hdc, theme::TextPrimary());
            ::DrawText(hdc, text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            ::SetTextColor(hdc, oldColor);
            ::SetBkMode(hdc, oldMode);
            ::SelectObject(hdc, oldFont);
        }

        EndPaint(&ps);
        return 0;
    }
};
