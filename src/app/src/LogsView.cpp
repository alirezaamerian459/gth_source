#include "LogsView.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

namespace {
HFONT g_monoFont = nullptr;
constexpr int kTopAreaHeight = 96;
constexpr int kMargin = 24;

// spdlog's daily_file_sink names today's file "log_YYYY-MM-DD.txt" (base
// name "log.txt" with the date spliced in before the extension) - matched
// here via GetLocalTime() since that sink rotates on local time, not UTC
// (unlike gth::core::Clock, which is deliberately UTC-only elsewhere).
std::wstring todaysLogFileName() {
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    std::array<wchar_t, 32> buf{};
    swprintf_s(buf.data(), buf.size(), L"log_%04d-%02d-%02d.txt", st.wYear, st.wMonth, st.wDay);
    return buf.data();
}
}  // namespace

LRESULT LogsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    if (!g_monoFont) {
        LOGFONT lf{};
        lf.lfHeight = -16;
        lf.lfWeight = FW_NORMAL;
        wcscpy_s(lf.lfFaceName, L"Consolas");
        g_monoFont = ::CreateFontIndirect(&lf);
    }

    CRect titleRect(kMargin, 24, kMargin + 200, 56);
    titleLabel_.Create(m_hWnd, titleRect, L"Logs", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    CRect refreshRect(kMargin + 220, 28, kMargin + 340, 60);
    refreshButton_.Create(m_hWnd, refreshRect, L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                           IDC_REFRESH_LOGS_BUTTON);
    refreshButton_.SetFont(theme::BodyFont());
    refreshButton_.setKind(ModernButton::Kind::Subtle);

    CRect pathRect(kMargin, 62, kMargin + 700, 82);
    pathLabel_.Create(m_hWnd, pathRect, L"", WS_CHILD | WS_VISIBLE);
    pathLabel_.SetFont(theme::SmallFont());

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect textRect(kMargin, kTopAreaHeight, clientRect.right - kMargin, clientRect.bottom - kMargin);
    logTextEdit_.Create(m_hWnd, textRect, L"",
                         WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                             ES_AUTOVSCROLL | ES_WANTRETURN,
                         0, IDC_LOGS_TEXT);
    logTextEdit_.SetFont(g_monoFont);

    refresh();
    return 0;
}

LRESULT LogsView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    if (logTextEdit_.IsWindow()) {
        logTextEdit_.MoveWindow(kMargin, kTopAreaHeight, std::max(100, width - 2 * kMargin),
                                 std::max(100, height - kTopAreaHeight - kMargin));
    }
    return 0;
}

LRESULT LogsView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    HDC hdc = reinterpret_cast<HDC>(wParam);
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, reinterpret_cast<HWND>(lParam) == pathLabel_.m_hWnd ? theme::TextSecondary()
                                                                             : theme::TextPrimary());
    HBRUSH pageBrush = theme::ContentBgBrush();
    return reinterpret_cast<LRESULT>(pageBrush);
}

LRESULT LogsView::OnCtlColorEdit(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorEdit(wParam); }

std::filesystem::path LogsView::todaysLogFilePath() const { return logDirectory_ / todaysLogFileName(); }

void LogsView::refresh() {
    if (!logTextEdit_.IsWindow()) return;

    const auto path = todaysLogFilePath();
    pathLabel_.SetWindowText(path.wstring().c_str());

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        logTextEdit_.SetWindowText(L"No log file for today yet.");
        return;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const auto wide = Utf8ToWide(buffer.str());
    logTextEdit_.SetWindowText(wide.c_str());
    logTextEdit_.SendMessage(EM_LINESCROLL, 0, logTextEdit_.GetLineCount());
}

LRESULT LogsView::OnRefreshClick(WORD, WORD, HWND, BOOL&) {
    refresh();
    return 0;
}
