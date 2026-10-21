#pragma once

#include <algorithm>

#include "WtlCommon.h"

// Vertical-scroll helper for views whose content is a fixed-position stack
// of controls laid out once in OnCreate (Settings, FtpView) rather than a
// title + single list view that already resizes/scrolls itself. Those
// stacked-form pages have no way to reach their lower controls once the
// content is taller than the window, so this gives them a real scrollbar
// plus mouse-wheel support instead.
//
// Usage in a view (see SettingsView/FtpView):
//   1. Add `ScrollSupport scroll_;` as a member.
//   2. Call `scroll_.attach(m_hWnd);` at the top of OnCreate.
//   3. After laying out every control, call
//      `scroll_.setContentHeight(bottomMostY + margin);`.
//   4. Forward OnSize to `scroll_.onSize();` (still resize/move any control
//      that tracks the client width, same as today - only the scroll range
//      changes).
//   5. Add MESSAGE_HANDLER(WM_VSCROLL, OnVScroll) and
//      MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel) to the message map, each
//      forwarding straight to `scroll_.onVScroll(...)` / `onMouseWheel(...)`.
//
// A page shorter than the window simply never has a scroll range - the
// scrollbar (added dynamically via ShowScrollBar, no WS_VSCROLL style needed
// at Create() time) stays hidden, exactly like a short tab is expected to
// behave.
class ScrollSupport {
public:
    void attach(HWND hwnd) {
        hwnd_ = hwnd;
        ::ShowScrollBar(hwnd_, SB_VERT, TRUE);
    }

    void setContentHeight(int contentHeight) {
        contentHeight_ = contentHeight;
        updateScrollInfo();
    }

    void onSize() { updateScrollInfo(); }

    LRESULT onVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
        SCROLLINFO si{sizeof(SCROLLINFO), SIF_ALL};
        ::GetScrollInfo(hwnd_, SB_VERT, &si);
        int pos = si.nPos;
        switch (LOWORD(wParam)) {
            case SB_LINEUP: pos -= kLineStep; break;
            case SB_LINEDOWN: pos += kLineStep; break;
            case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
            case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: pos = si.nTrackPos; break;
            case SB_TOP: pos = 0; break;
            case SB_BOTTOM: pos = contentHeight_; break;
            default: return 0;
        }
        scrollTo(pos);
        return 0;
    }

    LRESULT onMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL&) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        SCROLLINFO si{sizeof(SCROLLINFO), SIF_POS};
        ::GetScrollInfo(hwnd_, SB_VERT, &si);
        scrollTo(si.nPos - (delta / WHEEL_DELTA) * kWheelStep);
        return 0;
    }

private:
    static constexpr int kLineStep = 30;
    static constexpr int kWheelStep = 60;

    void updateScrollInfo() {
        if (!hwnd_) return;
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        clientHeight_ = client.bottom - client.top;

        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = std::max(0, contentHeight_ - 1);
        si.nPage = static_cast<UINT>(std::max(0, clientHeight_));
        si.nPos = std::min(scrollPos_, maxScrollPos());
        ::SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
        scrollTo(si.nPos, /*force=*/true);
    }

    int maxScrollPos() const { return std::max(0, contentHeight_ - clientHeight_); }

    void scrollTo(int pos, bool force = false) {
        pos = std::max(0, std::min(pos, maxScrollPos()));
        if (!force && pos == scrollPos_) return;

        const int delta = scrollPos_ - pos;
        scrollPos_ = pos;

        SCROLLINFO si{sizeof(SCROLLINFO), SIF_POS};
        si.nPos = scrollPos_;
        ::SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);

        if (delta != 0) {
            ::ScrollWindowEx(hwnd_, 0, delta, nullptr, nullptr, nullptr, nullptr,
                              SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
            ::UpdateWindow(hwnd_);
        }
    }

    HWND hwnd_ = nullptr;
    int contentHeight_ = 0;
    int clientHeight_ = 0;
    int scrollPos_ = 0;
};
