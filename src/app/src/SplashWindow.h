#pragma once

#include <algorithm>
#include <cmath>

#include "Theme.h"
#include "WtlCommon.h"

#pragma comment(lib, "msimg32.lib")

// Shown for a couple of seconds at startup before MainFrame appears - the
// "game launcher" intro treatment this app was asked to have (Ubisoft
// Connect and similar: a borderless window with the product's mark and
// name centered, animated in, held briefly with a real loading bar, then
// replaced by the real window). See WinMain.cpp for how the hold time is
// enforced and kHoldMs is the single source of truth both places use.
// Everything here is drawn with GDI primitives already used elsewhere in
// this app (theme::FillRoundRect/DrawCenteredText, GradientFill,
// AlphaBlend for the badge's scale+fade entrance) - no external image or
// video assets, so there's no new asset-loading path to maintain.
class SplashWindow : public CWindowImpl<SplashWindow> {
public:
    DECLARE_WND_CLASS(L"GthSplashWindow")

    // How long the splash is held on screen (WinMain.cpp's pump loop reads
    // this too, so the loading bar's 0-100% sweep and the actual hold
    // duration can never drift apart).
    static constexpr DWORD kHoldMs = 2200;

    BEGIN_MSG_MAP(SplashWindow)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()

    // Convenience over CWindowImpl::Create() - a splash screen is always
    // the same fixed size, borderless, and centered on the primary
    // monitor, so there's no reason to make callers specify a rect/parent/
    // style the way a normal view would.
    HWND CreateSplash() {
        CRect rect(0, 0, kWidth, kHeight);
        HWND hwnd = Create(nullptr, rect, L"", WS_POPUP | WS_VISIBLE, WS_EX_TOOLWINDOW);
        if (hwnd) {
            CenterWindow();
            creationTick_ = ::GetTickCount();
            // ~60fps repaint while the splash is up - WinMain's hold loop
            // pumps messages for it, so a timer this frequent is what
            // actually makes the pop-in/pulse/loading-bar animation move
            // instead of only updating on the occasional incidental
            // repaint.
            SetTimer(kAnimTimerId, 16, nullptr);
        }
        return hwnd;
    }

private:
    static constexpr int kWidth = 480;
    static constexpr int kHeight = 320;
    static constexpr int kBadgeSize = 84;
    static constexpr UINT_PTR kAnimTimerId = 1;

    // Badge pop-in: scales/fades up from kBadgeStartScale to full size over
    // kBadgeAnimMs with an ease-out curve, so it settles rather than
    // snapping to full size.
    static constexpr DWORD kBadgeAnimMs = 450;
    static constexpr double kBadgeStartScale = 0.55;
    // Title/version fade in staggered slightly after the badge starts
    // moving, via color interpolation (background color -> white) rather
    // than true alpha blending - simpler for text and visually equivalent
    // at this size.
    static constexpr DWORD kTextDelayMs = 150;
    static constexpr DWORD kTextAnimMs = 350;

    DWORD creationTick_ = 0;

    LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) { return 1; }  // painted fully in OnPaint - no flicker
    LRESULT OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
        if (wParam == kAnimTimerId) Invalidate(FALSE);
        return 0;
    }
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
        KillTimer(kAnimTimerId);
        bHandled = FALSE;
        return 0;
    }

    LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);

        CRect rect;
        GetClientRect(&rect);

        // Double-buffered: this now repaints ~60 times/sec for the
        // animation, so drawing straight to the screen DC would be
        // visibly flickery.
        HDC memDc = ::CreateCompatibleDC(hdc);
        HBITMAP memBmp = ::CreateCompatibleBitmap(hdc, rect.Width(), rect.Height());
        HGDIOBJ oldBmp = ::SelectObject(memDc, memBmp);

        paintContent(memDc, rect);

        ::BitBlt(hdc, 0, 0, rect.Width(), rect.Height(), memDc, 0, 0, SRCCOPY);
        ::SelectObject(memDc, oldBmp);
        ::DeleteObject(memBmp);
        ::DeleteDC(memDc);

        EndPaint(&ps);
        return 0;
    }

    // t in [0,1] -> eased [0,1], decelerating into the target (a linear
    // ramp reads as mechanical for a "reveal" moment; this is the same
    // quadratic ease-out curve most UI animation libraries default to).
    static double easeOut(double t) {
        t = std::clamp(t, 0.0, 1.0);
        return 1.0 - (1.0 - t) * (1.0 - t);
    }

    static COLORREF lerpColor(COLORREF from, COLORREF to, double t) {
        t = std::clamp(t, 0.0, 1.0);
        const auto lerp = [t](BYTE a, BYTE b) { return static_cast<BYTE>(a + (b - a) * t); };
        return RGB(lerp(GetRValue(from), GetRValue(to)), lerp(GetGValue(from), GetGValue(to)),
                   lerp(GetBValue(from), GetBValue(to)));
    }

    void paintContent(HDC hdc, const CRect& rect) {
        const DWORD elapsedMs = ::GetTickCount() - creationTick_;

        // Midnight purple vertical gradient - deliberately a different,
        // deeper purple than the app's own accent color anywhere else
        // (Theme.h's kDarkAccent blurple): this is a one-time launch
        // moment/brand mark, not a themed page.
        TRIVERTEX vertices[2]{};
        vertices[0].x = rect.left;
        vertices[0].y = rect.top;
        vertices[0].Red = 38 << 8;
        vertices[0].Green = 16 << 8;
        vertices[0].Blue = 74 << 8;
        vertices[1].x = rect.right;
        vertices[1].y = rect.bottom;
        vertices[1].Red = 12 << 8;
        vertices[1].Green = 6 << 8;
        vertices[1].Blue = 28 << 8;
        GRADIENT_RECT gRect{0, 1};
        ::GradientFill(hdc, vertices, 2, &gRect, 1, GRADIENT_FILL_RECT_V);

        // Hairline border so the borderless popup reads as an intentional
        // card against the desktop behind it, not a stray paint glitch.
        HPEN pen = ::CreatePen(PS_SOLID, 1, RGB(90, 60, 150));
        HGDIOBJ oldPen = ::SelectObject(hdc, pen);
        HGDIOBJ oldBrush = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
        ::Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
        ::SelectObject(hdc, oldBrush);
        ::SelectObject(hdc, oldPen);
        ::DeleteObject(pen);

        const CRect badgeHomeRect((rect.Width() - kBadgeSize) / 2, 54, (rect.Width() - kBadgeSize) / 2 + kBadgeSize,
                                   54 + kBadgeSize);
        const int badgeCenterX = badgeHomeRect.left + kBadgeSize / 2;
        const int badgeCenterY = badgeHomeRect.top + kBadgeSize / 2;

        // Soft pulsing glow behind the badge, continuous for as long as the
        // splash is up (not just during entrance) - a slow ~2.4s breathing
        // cycle so the window reads as alive rather than a static image
        // during the hold.
        {
            const double pulse = 0.5 + 0.5 * std::sin(elapsedMs / 2400.0 * 2.0 * 3.14159265);
            const int glowPad = static_cast<int>(14 + pulse * 10);
            CRect glowRect(badgeHomeRect.left - glowPad, badgeHomeRect.top - glowPad, badgeHomeRect.right + glowPad,
                            badgeHomeRect.bottom + glowPad);
            HDC glowSrcDc = ::CreateCompatibleDC(hdc);
            HBITMAP glowBmp = ::CreateCompatibleBitmap(hdc, glowRect.Width(), glowRect.Height());
            HGDIOBJ oldGlowBmp = ::SelectObject(glowSrcDc, glowBmp);
            RECT localGlowRect{0, 0, glowRect.Width(), glowRect.Height()};
            HBRUSH bg = ::CreateSolidBrush(RGB(38, 16, 74));
            ::FillRect(glowSrcDc, &localGlowRect, bg);
            ::DeleteObject(bg);
            theme::FillRoundRect(glowSrcDc, CRect(0, 0, glowRect.Width(), glowRect.Height()), glowRect.Width() / 2,
                                  RGB(150, 90, 220));
            BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(35 + pulse * 25), 0};
            ::AlphaBlend(hdc, glowRect.left, glowRect.top, glowRect.Width(), glowRect.Height(), glowSrcDc, 0, 0,
                         glowRect.Width(), glowRect.Height(), blend);
            ::SelectObject(glowSrcDc, oldGlowBmp);
            ::DeleteObject(glowBmp);
            ::DeleteDC(glowSrcDc);
        }

        // "DR" badge pop-in: rendered once at full size into its own
        // source DC, then AlphaBlend'd onto the canvas at an animated
        // (smaller -> full) destination size with animated alpha -
        // AlphaBlend supports source/dest dimensions differing, so this
        // gets scale and fade from a single call rather than needing a
        // real per-pixel-alpha bitmap.
        {
            HDC badgeSrcDc = ::CreateCompatibleDC(hdc);
            HBITMAP badgeBmp = ::CreateCompatibleBitmap(hdc, kBadgeSize, kBadgeSize);
            HGDIOBJ oldBadgeBmp = ::SelectObject(badgeSrcDc, badgeBmp);

            RECT localBadgeRect{0, 0, kBadgeSize, kBadgeSize};
            HBRUSH bg = ::CreateSolidBrush(RGB(38, 16, 74));
            ::FillRect(badgeSrcDc, &localBadgeRect, bg);
            ::DeleteObject(bg);
            theme::FillRoundRect(badgeSrcDc, CRect(0, 0, kBadgeSize, kBadgeSize), 20, RGB(108, 43, 173));
            HFONT badgeFont = ::CreateFontW(-38, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            theme::DrawCenteredText(badgeSrcDc, CRect(0, 0, kBadgeSize, kBadgeSize), L"DR", badgeFont,
                                     RGB(255, 255, 255));
            ::DeleteObject(badgeFont);

            const double badgeT = easeOut(static_cast<double>(elapsedMs) / kBadgeAnimMs);
            const double scale = kBadgeStartScale + (1.0 - kBadgeStartScale) * badgeT;
            const int destSize = static_cast<int>(kBadgeSize * scale);
            const int destX = badgeCenterX - destSize / 2;
            const int destY = badgeCenterY - destSize / 2;
            const BYTE alpha = static_cast<BYTE>(std::clamp(badgeT, 0.0, 1.0) * 255);

            BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, 0};
            ::AlphaBlend(hdc, destX, destY, destSize, destSize, badgeSrcDc, 0, 0, kBadgeSize, kBadgeSize, blend);

            ::SelectObject(badgeSrcDc, oldBadgeBmp);
            ::DeleteObject(badgeBmp);
            ::DeleteDC(badgeSrcDc);
        }

        // Title/version fade in via color interpolation (background ->
        // target color) rather than alpha blending - simpler for text,
        // and reads the same at this size. Staggered slightly after the
        // badge starts moving.
        const double textT =
            easeOut(static_cast<double>(elapsedMs > kTextDelayMs ? elapsedMs - kTextDelayMs : 0) / kTextAnimMs);
        constexpr COLORREF kBgApprox = RGB(30, 13, 60);

        CRect titleRect(0, badgeHomeRect.bottom + 22, rect.Width(), badgeHomeRect.bottom + 60);
        HFONT titleFont = ::CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        theme::DrawCenteredText(hdc, titleRect, L"Dr Game Transfer Hub", titleFont,
                                 lerpColor(kBgApprox, RGB(255, 255, 255), textT));
        ::DeleteObject(titleFont);

        // Version tag - same reasoning as MainFrame's title bar text: this
        // project has accumulated stale duplicate copies, so showing the
        // version at the one moment every launch passes through (the
        // splash) helps confirm which build just started.
        CRect versionRect(0, titleRect.bottom, rect.Width(), titleRect.bottom + 20);
        HFONT versionFont = ::CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        theme::DrawCenteredText(hdc, versionRect, L"v6.9.0", versionFont, lerpColor(kBgApprox, RGB(200, 190, 215), textT));
        ::DeleteObject(versionFont);

        // Loading bar - real progress against kHoldMs (the same constant
        // WinMain's pump loop holds the splash for), not a decorative
        // fake: it reaches 100% exactly when the splash is about to be
        // replaced by the real window.
        const double loadProgress = std::clamp(static_cast<double>(elapsedMs) / kHoldMs, 0.0, 1.0);
        constexpr int kBarHeight = 6;
        CRect barTrackRect(60, 246, rect.Width() - 60, 246 + kBarHeight);
        theme::FillRoundRect(hdc, barTrackRect, kBarHeight / 2, RGB(60, 40, 95));

        const int fillWidth = static_cast<int>(barTrackRect.Width() * loadProgress);
        if (fillWidth > 0) {
            CRect barFillRect(barTrackRect.left, barTrackRect.top, barTrackRect.left + fillWidth, barTrackRect.bottom);
            theme::FillRoundRect(hdc, barFillRect, kBarHeight / 2, RGB(150, 90, 220));
        }

        CRect percentRect(0, barTrackRect.bottom + 8, rect.Width(), barTrackRect.bottom + 28);
        HFONT percentFont = ::CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        wchar_t percentText[16];
        swprintf_s(percentText, L"%d%%", static_cast<int>(loadProgress * 100.0 + 0.5));
        theme::DrawCenteredText(hdc, percentRect, percentText, percentFont, RGB(200, 190, 215));
        ::DeleteObject(percentFont);

        // Developer credit + rights notice, bottom-right corner (two lines,
        // the font small enough that both fit without crowding the loading
        // bar just above).
        CRect creditRect(0, rect.bottom - 44, rect.Width() - 16, rect.bottom - 10);
        HFONT creditFont = ::CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ oldFont = ::SelectObject(hdc, creditFont);
        const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
        const COLORREF oldColor = ::SetTextColor(hdc, RGB(200, 190, 215));
        ::DrawText(hdc, L"Developed and designed by Alireza Amerian\nCopyright \x00A9 Dr Game. All rights reserved.",
                   -1, &creditRect, DT_RIGHT | DT_VCENTER | DT_NOPREFIX);
        ::SetTextColor(hdc, oldColor);
        ::SetBkMode(hdc, oldMode);
        ::SelectObject(hdc, oldFont);
        ::DeleteObject(creditFont);
    }
};
