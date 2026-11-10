#include "ComingSoonView.h"

#include <bit>

LRESULT ComingSoonView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CRect placeholderRect(0, 0, 100, 100);
    titleLabel_.Create(m_hWnd, placeholderRect, L"", WS_CHILD | WS_VISIBLE | SS_CENTER);
    titleLabel_.SetFont(theme::TitleFont());

    subtitleLabel_.Create(m_hWnd, placeholderRect, L"This module is planned for a later milestone.",
                           WS_CHILD | WS_VISIBLE | SS_CENTER);
    subtitleLabel_.SetFont(theme::BodyFont());

    layout();
    return 0;
}

LRESULT ComingSoonView::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
    layout();
    return 0;
}

LRESULT ComingSoonView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    const auto hdc = std::bit_cast<HDC>(wParam);
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, std::bit_cast<HWND>(lParam) == subtitleLabel_.m_hWnd ? theme::TextSecondary()
                                                                             : theme::TextPrimary());
    HBRUSH pageBrush = theme::ContentBgBrush();
    return std::bit_cast<LRESULT>(pageBrush);
}

void ComingSoonView::layout() {
    CRect rc;
    GetClientRect(&rc);
    const int centerY = rc.Height() / 2;

    if (titleLabel_.IsWindow()) titleLabel_.MoveWindow(0, centerY - 30, rc.Width(), 30);
    if (subtitleLabel_.IsWindow()) subtitleLabel_.MoveWindow(0, centerY + 4, rc.Width(), 24);
}

void ComingSoonView::setTitle(const std::wstring& title) {
    if (titleLabel_.IsWindow()) titleLabel_.SetWindowText(title.c_str());
}
