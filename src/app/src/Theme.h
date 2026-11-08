#pragma once

#include <commctrl.h>

#include <array>

#include "WtlCommon.h"

#pragma comment(lib, "comctl32.lib")

// Shared visual language for the whole app - colors, fonts, and a couple of
// GDI drawing helpers - so every view looks like it belongs to the same
// product instead of each accumulating its own ad-hoc styling. Palette and
// type scale are deliberately modeled after two references: PlayStation's
// own site (playstation.com) for the accent blue / light, airy section
// backgrounds / large light-weight headings, and Windows 11's own
// Settings-app look (light sidebar with a soft accent "pill" behind the
// selected item, rounded controls, generous whitespace) for the shell
// chrome, since this is a Windows desktop app, not a website.
//
// Colors are exposed as functions (theme::ContentBg(), not a constexpr) so
// the Settings tab's Light/Dark toggle can flip every view's palette at
// runtime - see SetDarkMode()/IsDarkMode() below. Native-drawn controls
// (plain CEdit/CListViewCtrl internals) don't follow this - only the
// surfaces this app paints itself (page backgrounds, ModernButton,
// StatusDot, the sidebar) do.
namespace theme {

namespace detail {
inline bool& darkModeFlag() {
    // Dark is the app's only real identity now (see AppSettings::theme's
    // default) - this flag's own default just needs to agree with that so
    // anything painted before MainFrame reads settings (there isn't
    // anything today, but better to not have a light-mode flash be possible)
    // is dark too.
    static bool dark = true;
    return dark;
}
}  // namespace detail

inline bool IsDarkMode() { return detail::darkModeFlag(); }
inline void SetDarkMode(bool dark) { detail::darkModeFlag() = dark; }

// --- Palette ---
// Light mode: accent blue sampled directly from playstation.com's computed
// styles (rgb(0, 112, 204)). Dark mode intentionally does NOT reuse that
// blue - a light-mode-tuned blue reads as muddy/low-contrast on a dark
// surface, and it's the accent that's visible everywhere (primary buttons,
// the selected sidebar pill, focus rings), so a "the whole app went dark"
// toggle needs it to change too. Dark mode instead uses Discord's blurple
// (#5865F2 family) against Discord's own near-black surface levels
// (#202225 sidebar / #36393f content) - that combination (true dark
// surfaces + a saturated purple accent, not just an inverted blue-on-gray)
// is specifically what was asked for here.
constexpr COLORREF kLightAccent = RGB(0, 112, 204);
constexpr COLORREF kLightAccentHover = RGB(0, 98, 181);
constexpr COLORREF kLightAccentPressed = RGB(0, 84, 158);

constexpr COLORREF kDarkAccent = RGB(88, 101, 242);     // Discord blurple #5865F2
constexpr COLORREF kDarkAccentHover = RGB(71, 82, 196);  // #4752C4
constexpr COLORREF kDarkAccentPressed = RGB(60, 68, 170);  // #3C45AA

constexpr COLORREF kTextOnAccent = RGB(255, 255, 255);

constexpr COLORREF kLightAccentSoft = RGB(223, 237, 251);
constexpr COLORREF kDarkAccentSoft = RGB(53, 45, 111);  // purple-tinted pill, not blue-tinted

constexpr COLORREF kLightSidebarBg = RGB(246, 247, 249);
constexpr COLORREF kDarkSidebarBg = RGB(32, 34, 37);  // Discord's #202225-family nav level

constexpr COLORREF kLightContentBg = RGB(255, 255, 255);
constexpr COLORREF kDarkContentBg = RGB(54, 57, 63);  // Discord's #36393f content level

constexpr COLORREF kLightHoverBg = RGB(237, 238, 241);
constexpr COLORREF kDarkHoverBg = RGB(66, 70, 78);

constexpr COLORREF kLightBorder = RGB(226, 227, 231);
constexpr COLORREF kDarkBorder = RGB(32, 34, 37);

// Header column separator lines specifically (see HeaderSubclassProc) -
// deliberately its own token rather than reusing Border(): Border()'s dark
// value sits *darker* than the header's own HoverBg background, so it reads
// as a barely-visible recessed groove instead of a clear divider. This is
// lighter/more visible against that same background while staying a calm
// gray, not full-contrast. The dark value needs a bigger absolute jump than
// the light one to read as equally visible - a +28 step off kDarkHoverBg
// (66,70,78) was too close in practice (still looked unchanged), so this
// uses a bigger step, similar in spirit to kDarkTextSecondary's brightness.
constexpr COLORREF kLightHeaderLine = RGB(205, 207, 212);
constexpr COLORREF kDarkHeaderLine = RGB(130, 135, 145);

constexpr COLORREF kLightDisabledFill = RGB(210, 213, 218);
constexpr COLORREF kDarkDisabledFill = RGB(64, 68, 75);

// Recessed input fields (edit boxes, combos) sit a shade darker than the
// page itself, the way Discord's own text inputs read as "cut into" the
// dark surface instead of floating on it as a bright white rectangle.
constexpr COLORREF kLightEditBg = RGB(255, 255, 255);
constexpr COLORREF kDarkEditBg = RGB(40, 43, 48);

constexpr COLORREF kLightTextPrimary = RGB(31, 31, 31);
constexpr COLORREF kDarkTextPrimary = RGB(220, 221, 222);

constexpr COLORREF kLightTextSecondary = RGB(112, 112, 118);
constexpr COLORREF kDarkTextSecondary = RGB(148, 150, 155);

constexpr COLORREF kLightTextDisabled = RGB(170, 170, 174);
constexpr COLORREF kDarkTextDisabled = RGB(98, 100, 105);

constexpr COLORREF kLightDanger = RGB(196, 43, 28);
constexpr COLORREF kDarkDanger = RGB(237, 66, 69);  // Discord red

constexpr COLORREF kLightSuccess = RGB(20, 132, 86);
constexpr COLORREF kDarkSuccess = RGB(59, 165, 93);  // Discord green

inline COLORREF Accent() { return IsDarkMode() ? kDarkAccent : kLightAccent; }
inline COLORREF AccentHover() { return IsDarkMode() ? kDarkAccentHover : kLightAccentHover; }
inline COLORREF AccentPressed() { return IsDarkMode() ? kDarkAccentPressed : kLightAccentPressed; }
inline COLORREF TextOnAccent() { return kTextOnAccent; }
inline COLORREF AccentSoft() { return IsDarkMode() ? kDarkAccentSoft : kLightAccentSoft; }
inline COLORREF SidebarBg() { return IsDarkMode() ? kDarkSidebarBg : kLightSidebarBg; }
inline COLORREF ContentBg() { return IsDarkMode() ? kDarkContentBg : kLightContentBg; }
inline COLORREF HoverBg() { return IsDarkMode() ? kDarkHoverBg : kLightHoverBg; }
inline COLORREF Border() { return IsDarkMode() ? kDarkBorder : kLightBorder; }
inline COLORREF HeaderLine() { return IsDarkMode() ? kDarkHeaderLine : kLightHeaderLine; }
inline COLORREF DisabledFill() { return IsDarkMode() ? kDarkDisabledFill : kLightDisabledFill; }
inline COLORREF EditBg() { return IsDarkMode() ? kDarkEditBg : kLightEditBg; }
inline COLORREF TextPrimary() { return IsDarkMode() ? kDarkTextPrimary : kLightTextPrimary; }
inline COLORREF TextSecondary() { return IsDarkMode() ? kDarkTextSecondary : kLightTextSecondary; }
inline COLORREF TextDisabled() { return IsDarkMode() ? kDarkTextDisabled : kLightTextDisabled; }
inline COLORREF Danger() { return IsDarkMode() ? kDarkDanger : kLightDanger; }
inline COLORREF Success() { return IsDarkMode() ? kDarkSuccess : kLightSuccess; }

// Brushes used as WM_CTLCOLORSTATIC/WM_CTLCOLORLISTBOX return values need a
// stable HBRUSH the caller doesn't delete - these keep one brush per mode
// alive for the process lifetime (created lazily, same "intentionally
// leaked" convention as the fonts below) instead of the old per-view
// `static HBRUSH x = ::CreateSolidBrush(...)` pattern, which baked in
// whichever mode was active on that control's first paint and never
// updated when the mode changed.
inline HBRUSH ContentBgBrush() {
    static HBRUSH light = ::CreateSolidBrush(kLightContentBg);
    static HBRUSH dark = ::CreateSolidBrush(kDarkContentBg);
    return IsDarkMode() ? dark : light;
}

inline HBRUSH SidebarBgBrush() {
    static HBRUSH light = ::CreateSolidBrush(kLightSidebarBg);
    static HBRUSH dark = ::CreateSolidBrush(kDarkSidebarBg);
    return IsDarkMode() ? dark : light;
}

inline HBRUSH EditBgBrush() {
    static HBRUSH light = ::CreateSolidBrush(kLightEditBg);
    static HBRUSH dark = ::CreateSolidBrush(kDarkEditBg);
    return IsDarkMode() ? dark : light;
}

namespace detail {

// The Header control (a ListView's column-header bar) doesn't expose a
// documented "set background/text color" message the way the list body
// does - visual-style theming alone leaves it a stark, high-contrast strip
// that doesn't match the calmer gray the rest of the app uses. Subclassing
// its WM_PAINT and drawing it ourselves (HoverBg background, TextSecondary
// text - both already-tuned muted grays, not new one-off colors) is the
// standard, well-supported way to control a native Header's exact colors.
// Subclassed once per header from ApplyListViewTheme() below rather than
// wired into every individual view's message map - NM_CUSTOMDRAW for a
// ListView's header notifies the view (the header's ultimate WM_NOTIFY
// target), which would mean repeating this in every view that owns a list;
// subclassing the header HWND directly here means every list in the app
// gets it for free just by calling the one function they already all call.
inline LRESULT CALLBACK HeaderSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId,
                                            DWORD_PTR) {
    if (msg == WM_ERASEBKGND) return 1;  // fully painted below - skip the default flash-white erase

    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = ::BeginPaint(hwnd, &ps);

        RECT clientRect;
        ::GetClientRect(hwnd, &clientRect);
        HBRUSH bg = ::CreateSolidBrush(HoverBg());
        ::FillRect(hdc, &clientRect, bg);
        ::DeleteObject(bg);

        HFONT font = reinterpret_cast<HFONT>(::SendMessage(hwnd, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : nullptr;
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, TextSecondary());

        HPEN separatorPen = ::CreatePen(PS_SOLID, 1, HeaderLine());
        HGDIOBJ oldPen = ::SelectObject(hdc, separatorPen);

        const int count = Header_GetItemCount(hwnd);
        for (int i = 0; i < count; ++i) {
            RECT itemRect;
            Header_GetItemRect(hwnd, i, &itemRect);

            std::array<wchar_t, 256> textBuf{};
            HDITEMW item{};
            item.mask = HDI_TEXT;
            item.pszText = textBuf.data();
            item.cchTextMax = 255;
            Header_GetItem(hwnd, i, &item);

            RECT textRect = itemRect;
            textRect.left += 8;
            textRect.right -= 4;
            ::DrawTextW(hdc, textBuf.data(), -1, &textRect,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            if (i > 0) {
                ::MoveToEx(hdc, itemRect.left, itemRect.top + 4, nullptr);
                ::LineTo(hdc, itemRect.left, itemRect.bottom - 4);
            }
        }

        ::SelectObject(hdc, oldPen);
        ::DeleteObject(separatorPen);
        if (oldFont) ::SelectObject(hdc, oldFont);

        ::EndPaint(hwnd, &ps);
        return 0;
    }

    if (msg == WM_NCDESTROY) {
        ::RemoveWindowSubclass(hwnd, HeaderSubclassProc, subclassId);
    }

    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

}  // namespace detail

// CListViewCtrl defaults to plain Win32 chrome; Explorer's visual style
// (see WtlCommon.h's ApplyExplorerTheme) gives row hover/selection bands
// "for free" but never touches the actual background/text/header colors -
// in dark mode that leaves a bright white grid sitting in the middle of an
// otherwise dark page, which is the single biggest reason a half-themed
// dark mode still reads as "not actually dark". "DarkMode_Explorer" is the
// same visual-style class File Explorer's own dark mode uses for its list
// view (present since Windows 10 1809); pairing it with the explicit
// LVM_SETBKCOLOR/TEXTBKCOLOR/TEXTCOLOR calls below (ordinary, documented
// messages - CListViewCtrl just wraps them) covers what the visual style
// alone doesn't. The header itself gets a custom-drawn gray background/text
// (see HeaderSubclassProc above) since neither the visual style nor those
// messages reach it. Call this instead of ApplyExplorerTheme() for any list
// view, both at Create() time and again from refresh() so a live
// Light/Dark toggle repaints already-created lists too, not just ones
// created after the toggle.
inline void ApplyListViewTheme(CListViewCtrl& listView) {
    ::SetWindowTheme(listView.m_hWnd, IsDarkMode() ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    listView.SetBkColor(ContentBg());
    listView.SetTextBkColor(ContentBg());
    listView.SetTextColor(TextPrimary());

    HWND header = ListView_GetHeader(listView.m_hWnd);
    if (header) {
        ::SetWindowSubclass(header, detail::HeaderSubclassProc, 1, 0);
        ::InvalidateRect(header, nullptr, TRUE);
    }
}

constexpr int kRadiusButton = 6;
constexpr int kRadiusPill = 999;  // clamped to half-height by the drawing helper - a true pill
constexpr int kMargin = 28;

// --- Fonts (lazily created once, intentionally leaked for the process
// lifetime - see WtlCommon.h's CreateUiFont for why). ---
inline HFONT TitleFont() {
    static HFONT font = [] {
        NONCLIENTMETRICS ncm{};
        ncm.cbSize = sizeof(ncm);
        ::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        LOGFONT lf = ncm.lfMessageFont;
        lf.lfHeight = static_cast<LONG>(lf.lfHeight * 1.9);
        lf.lfWeight = FW_SEMIBOLD;
        return ::CreateFontIndirect(&lf);
    }();
    return font;
}

inline HFONT SectionFont() {
    static HFONT font = [] {
        NONCLIENTMETRICS ncm{};
        ncm.cbSize = sizeof(ncm);
        ::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        LOGFONT lf = ncm.lfMessageFont;
        lf.lfHeight = static_cast<LONG>(lf.lfHeight * 1.25);
        lf.lfWeight = FW_SEMIBOLD;
        return ::CreateFontIndirect(&lf);
    }();
    return font;
}

inline HFONT BodyFont() {
    static HFONT font = CreateUiFont(false, 0);
    return font;
}

inline HFONT BodyFontBold() {
    static HFONT font = CreateUiFont(true, 0);
    return font;
}

inline HFONT SmallFont() {
    static HFONT font = [] {
        NONCLIENTMETRICS ncm{};
        ncm.cbSize = sizeof(ncm);
        ::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        LOGFONT lf = ncm.lfMessageFont;
        lf.lfHeight = static_cast<LONG>(lf.lfHeight * 0.86);
        return ::CreateFontIndirect(&lf);
    }();
    return font;
}

// --- Drawing helpers ---

// Fills (and optionally strokes) a rounded rectangle. Pass CLR_INVALID for
// borderColor to skip the stroke.
inline void FillRoundRect(HDC hdc, const RECT& rect, int radius, COLORREF fill,
                           COLORREF borderColor = CLR_INVALID) {
    HBRUSH brush = ::CreateSolidBrush(fill);
    HPEN pen = borderColor != CLR_INVALID ? ::CreatePen(PS_SOLID, 1, borderColor)
                                           : static_cast<HPEN>(::GetStockObject(NULL_PEN));
    HGDIOBJ oldBrush = ::SelectObject(hdc, brush);
    HGDIOBJ oldPen = ::SelectObject(hdc, pen);
    ::RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    ::SelectObject(hdc, oldBrush);
    ::SelectObject(hdc, oldPen);
    ::DeleteObject(brush);
    if (borderColor != CLR_INVALID) ::DeleteObject(pen);
}

inline void DrawCenteredText(HDC hdc, const RECT& rect, const std::wstring& text, HFONT font, COLORREF color) {
    HGDIOBJ oldFont = ::SelectObject(hdc, font);
    const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, color);
    RECT textRect = rect;
    ::DrawText(hdc, text.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, oldFont);
}

}  // namespace theme
