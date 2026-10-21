#pragma once

#include <initializer_list>
#include <string>

// Shared WTL bootstrap header - every translation unit that touches a WTL/ATL
// window class includes this instead of the raw atl*.h headers directly, so
// the _Module extern declaration and required defines stay in exactly one
// place.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // windows.h's min/max macros break <algorithm>/<chrono> use elsewhere
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00  // Windows 10/11
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <atlbase.h>
#include <atlapp.h>

extern WTL::CAppModule _Module;

#include <atlwin.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlmisc.h>
#include <uxtheme.h>

#pragma comment(lib, "uxtheme.lib")

using namespace WTL;

// Standard ListView/TreeView controls render with the plain flat-blue
// selection look unless explicitly opted into Explorer's visual style -
// this one call gives the same row hover/selection bands File Explorer's
// details view uses, for free (no owner-draw needed).
inline void ApplyExplorerTheme(HWND hwnd) { ::SetWindowTheme(hwnd, L"Explorer", nullptr); }

// Message-font sized/weighted the way the rest of Windows renders UI text
// (typically Segoe UI), instead of the ugly default GUI font every raw Win32
// control starts with. Callers own the returned HFONT for the process
// lifetime (deliberately never deleted - these are created a handful of
// times total and live as long as the app).
inline HFONT CreateUiFont(bool bold = false, int deltaHeight = 0) {
    NONCLIENTMETRICS ncm{};
    ncm.cbSize = sizeof(NONCLIENTMETRICS);
    ::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

    LOGFONT lf = ncm.lfMessageFont;
    lf.lfHeight -= deltaHeight;
    if (bold) lf.lfWeight = FW_BOLD;
    return ::CreateFontIndirect(&lf);
}

// Plain CStatic controls paint an opaque COLOR_BTNFACE-ish background by
// default, which looks like a mismatched gray box against a white page.
// Views with static labels wire WM_CTLCOLORSTATIC to this so labels blend
// into the page background instead. TextPrimary()/ContentBgBrush() are
// forward-declared (defined in Theme.h, which every caller of this
// function already includes for its fonts) rather than pulled in here -
// this header is meant to stay the minimal WTL bootstrap every other
// header includes first, not grow a dependency on the app's theme.
namespace theme {
COLORREF TextPrimary();
COLORREF EditBg();
HBRUSH ContentBgBrush();
HBRUSH EditBgBrush();
}  // namespace theme

inline LRESULT HandleCtlColorStatic(WPARAM wParam) {
    HDC hdc = reinterpret_cast<HDC>(wParam);
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, theme::TextPrimary());
    return reinterpret_cast<LRESULT>(theme::ContentBgBrush());
}

// Same idea as HandleCtlColorStatic but for CEdit/editable-combo surfaces -
// wire WM_CTLCOLOREDIT (and WM_CTLCOLORLISTBOX for CBS_DROPDOWNLIST combos,
// which paint their closed state through that message instead) to this so
// text fields go dark with the rest of the page instead of staying a fixed
// white rectangle regardless of theme.
inline LRESULT HandleCtlColorEdit(WPARAM wParam) {
    HDC hdc = reinterpret_cast<HDC>(wParam);
    ::SetBkMode(hdc, OPAQUE);
    ::SetBkColor(hdc, theme::EditBg());
    ::SetTextColor(hdc, theme::TextPrimary());
    return reinterpret_cast<LRESULT>(theme::EditBgBrush());
}

// WM_CTLCOLORSTATIC/EDIT only theme a *control's own* rectangle - none of
// them touch the page itself. A view window created via DECLARE_WND_CLASS
// with no explicit background brush gets ATL's default (COLOR_WINDOW+1,
// i.e. plain white) for WM_ERASEBKGND, so every pixel not directly covered
// by a themed control - margins, gaps between sections, anything below the
// last control - stayed white even with every individual control correctly
// dark. Wire WM_ERASEBKGND to this in every view (see DashboardView for the
// one view that already paints something fancier than a flat fill here) so
// the *whole* client area is the page background, not just its controls.
inline LRESULT HandleEraseBkgndWithContentBg(HWND hwnd, WPARAM wParam) {
    HDC hdc = reinterpret_cast<HDC>(wParam);
    RECT rect{};
    ::GetClientRect(hwnd, &rect);
    ::FillRect(hdc, &rect, theme::ContentBgBrush());
    return 1;
}

// Toggles ShowWindow(SW_SHOW/SW_HIDE) across a whole group of controls in
// one call - shared by SettingsView's Admin Sign In section and FtpView's
// admin-only sections, both of which need to show/hide a set of controls
// together based on sign-in state. Every WTL control type used in this app
// (CEdit, CStatic, CComboBox, CListViewCtrl, ModernButton, ModernCheckBox,
// StatusDot) publicly derives from CWindow, so a mixed-type initializer
// list converts implicitly at each call site.
inline void SetControlsVisible(std::initializer_list<CWindow*> controls, bool visible) {
    for (CWindow* control : controls) {
        if (control && control->IsWindow()) control->ShowWindow(visible ? SW_SHOW : SW_HIDE);
    }
}

inline std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), len);
    return wide;
}

inline std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string utf8(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), len, nullptr,
                           nullptr);
    return utf8;
}
