#include "ConsolesView.h"

#include <algorithm>
#include <cwchar>

#include "gth/core/devices/Console.h"

namespace {
constexpr int kTopAreaHeight = 148;
constexpr int kMargin = 24;
}  // namespace

LRESULT ConsolesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    HFONT g_normalFont = theme::BodyFont();

    CRect titleRect(kMargin, 24, kMargin + 200, 56);
    titleLabel_.Create(m_hWnd, titleRect, L"Consoles", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    int x = kMargin;
    const int inputTop = 62;
    const int captionHeight = 18;
    const int editTop = inputTop + captionHeight;
    const int editHeight = 24;

    auto makeField = [&](CStatic& caption, CEdit& edit, const wchar_t* label, int width) {
        CRect capRect(x, inputTop, x + width, inputTop + captionHeight);
        caption.Create(m_hWnd, capRect, label, WS_CHILD | WS_VISIBLE);
        caption.SetFont(g_normalFont);

        CRect editRect(x, editTop, x + width, editTop + editHeight);
        edit.Create(m_hWnd, editRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
        edit.SetFont(g_normalFont);

        x += width + 12;
    };

    makeField(nameCaption_, nameEdit_, L"Name", 160);
    makeField(platformCaption_, platformEdit_, L"Platform", 110);
    makeField(ipCaption_, ipEdit_, L"IP Address", 130);
    makeField(portCaption_, portEdit_, L"Port", 70);

    CRect addRect(x, editTop, x + 120, editTop + editHeight);
    addButton_.Create(m_hWnd, addRect, L"Add Console", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                       IDC_ADD_CONSOLE_BUTTON);
    addButton_.SetFont(g_normalFont);
    addButton_.setKind(ModernButton::Kind::Primary);

    const int secondRowTop = editTop + editHeight + 12;
    CRect checkRect(kMargin, secondRowTop, kMargin + 130, secondRowTop + editHeight);
    checkButton_.Create(m_hWnd, checkRect, L"Check Selected", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                         IDC_CHECK_CONSOLE_BUTTON);
    checkButton_.SetFont(g_normalFont);
    checkButton_.setKind(ModernButton::Kind::Secondary);

    CRect removeRect(kMargin + 142, secondRowTop, kMargin + 262, secondRowTop + editHeight);
    removeButton_.Create(m_hWnd, removeRect, L"Remove Selected", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                          0, IDC_REMOVE_CONSOLE_BUTTON);
    removeButton_.SetFont(g_normalFont);
    removeButton_.setKind(ModernButton::Kind::Danger);

    CRect statusRect(kMargin + 280, secondRowTop + 3, kMargin + 700, secondRowTop + editHeight);
    statusLabel_.Create(m_hWnd, statusRect, L"", WS_CHILD | WS_VISIBLE);
    statusLabel_.SetFont(g_normalFont);

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(kMargin, kTopAreaHeight, clientRect.right - kMargin, clientRect.bottom - kMargin);
    listView_.Create(m_hWnd, listRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 0,
                      IDC_CONSOLES_LISTVIEW);
    listView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    listView_.SetFont(g_normalFont);
    theme::ApplyListViewTheme(listView_);

    listView_.InsertColumn(0, L"Name", LVCFMT_LEFT, 160);
    listView_.InsertColumn(1, L"Platform", LVCFMT_LEFT, 100);
    listView_.InsertColumn(2, L"IP Address", LVCFMT_LEFT, 120);
    listView_.InsertColumn(3, L"Port", LVCFMT_LEFT, 60);
    listView_.InsertColumn(4, L"Status", LVCFMT_LEFT, 80);
    listView_.InsertColumn(5, L"Last Connected", LVCFMT_LEFT, 160);
    listView_.InsertColumn(6, L"Source", LVCFMT_LEFT, 100);

    refresh();
    SetTimer(kLiveRefreshTimerId, kLiveRefreshIntervalMs, nullptr);
    return 0;
}

LRESULT ConsolesView::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (wParam != kLiveRefreshTimerId) return 0;
    refresh();
    return 0;
}

LRESULT ConsolesView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    if (listView_.IsWindow()) {
        listView_.MoveWindow(kMargin, kTopAreaHeight, std::max(100, width - 2 * kMargin),
                              std::max(100, height - kTopAreaHeight - kMargin));
    }
    return 0;
}

LRESULT ConsolesView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorStatic(wParam); }
LRESULT ConsolesView::OnCtlColorEdit(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorEdit(wParam); }

std::wstring ConsolesView::getEditText(CEdit& edit) {
    const int len = edit.GetWindowTextLength();
    std::wstring text(static_cast<size_t>(len), L'\0');
    if (len > 0) edit.GetWindowText(text.data(), len + 1);
    return text;
}

void ConsolesView::refresh() {
    if (!manager_ || !listView_.IsWindow()) return;

    // The auto-refresh timer calls this every few seconds now (not just on
    // tab navigation), so a row the user just clicked - about to hit "Check
    // Selected"/"Remove Selected" - must not silently lose its selection
    // out from under them.
    const std::string previouslySelectedId = selectedConsoleId();

    theme::ApplyListViewTheme(listView_);
    listView_.DeleteAllItems();
    lastConsoles_ = manager_->getAllConsoles();

    int row = 0;
    int rowToReselect = -1;
    for (const auto& console : lastConsoles_) {
        listView_.InsertItem(row, Utf8ToWide(console.name).c_str());
        listView_.SetItemText(row, 1, Utf8ToWide(console.platform).c_str());
        listView_.SetItemText(row, 2, Utf8ToWide(console.ipAddress).c_str());
        listView_.SetItemText(row, 3, std::to_wstring(console.port).c_str());
        listView_.SetItemText(row, 4, Utf8ToWide(gth::core::toString(console.status)).c_str());
        listView_.SetItemText(row, 5, console.lastConnected ? Utf8ToWide(*console.lastConnected).c_str() : L"-");
        listView_.SetItemText(row, 6, console.autoDetected ? L"Auto" : L"Manual");
        listView_.SetItemData(row, static_cast<DWORD_PTR>(row));
        if (!previouslySelectedId.empty() && console.consoleId == previouslySelectedId) {
            rowToReselect = row;
        }
        row++;
    }

    if (rowToReselect >= 0) {
        listView_.SetItemState(rowToReselect, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }

    // Stash consoleIds alongside rows via a parallel approach: since WTL's
    // ListView item data is a plain DWORD_PTR (not a string), the console
    // id is looked up again by row index against the cached lastConsoles_
    // in selectedConsoleId() - simpler than managing a pointer's lifetime
    // across refreshes for a list this small.
}

std::string ConsolesView::selectedConsoleId() {
    const int sel = listView_.IsWindow() ? listView_.GetNextItem(-1, LVNI_SELECTED) : -1;
    if (sel < 0 || static_cast<size_t>(sel) >= lastConsoles_.size()) return "";
    return lastConsoles_[static_cast<size_t>(sel)].consoleId;
}

LRESULT ConsolesView::OnAddClick(WORD, WORD, HWND, BOOL&) {
    if (!manager_) return 0;

    const std::wstring name = getEditText(nameEdit_);
    const std::wstring platform = getEditText(platformEdit_);
    const std::wstring ip = getEditText(ipEdit_);
    const std::wstring portText = getEditText(portEdit_);

    if (name.empty() || ip.empty()) {
        statusLabel_.SetWindowText(L"Name and IP address are required.");
        return 0;
    }

    int port = 0;
    try {
        port = std::stoi(portText);
    } catch (...) {
        statusLabel_.SetWindowText(L"Port must be a number.");
        return 0;
    }

    auto toUtf8 = [](const std::wstring& wide) {
        if (wide.empty()) return std::string();
        const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
                                                nullptr, nullptr);
        std::string result(static_cast<size_t>(len), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), len, nullptr,
                               nullptr);
        return result;
    };

    manager_->addConsole(toUtf8(name), toUtf8(platform), toUtf8(ip), port, gth::core::ConsoleProtocol::Custom);

    nameEdit_.SetWindowText(L"");
    platformEdit_.SetWindowText(L"");
    ipEdit_.SetWindowText(L"");
    portEdit_.SetWindowText(L"");

    statusLabel_.SetWindowText(L"Console added.");
    refresh();
    return 0;
}

LRESULT ConsolesView::OnCheckClick(WORD, WORD, HWND, BOOL&) {
    if (!manager_) return 0;

    const auto id = selectedConsoleId();
    if (id.empty()) {
        statusLabel_.SetWindowText(L"Select a console first.");
        return 0;
    }

    statusLabel_.SetWindowText(L"Checking...");
    ::UpdateWindow(statusLabel_.m_hWnd);

    const auto status = manager_->checkReachability(id);
    refresh();

    statusLabel_.SetWindowText(status == gth::core::ConsoleStatus::Online ? L"Console is online." : L"Console is offline.");
    return 0;
}

LRESULT ConsolesView::OnRemoveClick(WORD, WORD, HWND, BOOL&) {
    if (!manager_) return 0;

    const auto id = selectedConsoleId();
    if (id.empty()) {
        statusLabel_.SetWindowText(L"Select a console first.");
        return 0;
    }

    manager_->removeConsole(id);
    statusLabel_.SetWindowText(L"Console removed.");
    refresh();
    return 0;
}
