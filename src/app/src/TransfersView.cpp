#include "TransfersView.h"

#include <algorithm>
#include <array>

#include "gth/core/enums/TransferStatus.h"

namespace {
constexpr int kMargin = 24;
}  // namespace

LRESULT TransfersView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    HFONT g_normalFont = theme::BodyFont();

    CRect titleRect(kMargin, 24, kMargin + 200, 56);
    titleLabel_.Create(m_hWnd, titleRect, L"Quick Transfer", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    CRect newPanelRect(kMargin + 190, 27, kMargin + 340, 55);
    newPanelButton_.Create(m_hWnd, newPanelRect, L"New transfer", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                           0, IDC_TRANSFERS_NEW_PANEL_BUTTON);
    newPanelButton_.SetFont(g_normalFont);
    newPanelButton_.setKind(ModernButton::Kind::Primary);

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

    makeField(nameCaption_, nameEdit_, L"Job Name", 160);
    makeField(sourceCaption_, sourceEdit_, L"Source Path", 260);
    makeField(destCaption_, destEdit_, L"Destination Path", 260);

    CRect enqueueRect(x, editTop, x + 120, editTop + editHeight);
    enqueueButton_.Create(m_hWnd, enqueueRect, L"Enqueue", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                           IDC_ENQUEUE_TRANSFER_BUTTON);
    enqueueButton_.SetFont(g_normalFont);
    enqueueButton_.setKind(ModernButton::Kind::Primary);

    const int secondRowTop = editTop + editHeight + 12;
    CRect cancelRect(kMargin, secondRowTop, kMargin + 140, secondRowTop + editHeight);
    cancelButton_.Create(m_hWnd, cancelRect, L"Cancel Selected", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                          0, IDC_CANCEL_TRANSFER_BUTTON);
    cancelButton_.SetFont(g_normalFont);
    cancelButton_.setKind(ModernButton::Kind::Danger);

    CRect refreshRect(kMargin + 152, secondRowTop, kMargin + 252, secondRowTop + editHeight);
    refreshButton_.Create(m_hWnd, refreshRect, L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                           IDC_REFRESH_TRANSFERS_BUTTON);
    refreshButton_.SetFont(g_normalFont);
    refreshButton_.setKind(ModernButton::Kind::Subtle);

    CRect statusRect(kMargin + 270, secondRowTop + 3, kMargin + 700, secondRowTop + editHeight);
    statusLabel_.Create(m_hWnd, statusRect, L"", WS_CHILD | WS_VISIBLE);
    statusLabel_.SetFont(g_normalFont);

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(kMargin, 104, clientRect.right - kMargin, clientRect.bottom - kMargin);
    listView_.Create(m_hWnd, listRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 0,
                      IDC_TRANSFERS_LISTVIEW);
    listView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    listView_.SetFont(g_normalFont);
    theme::ApplyListViewTheme(listView_);

    listView_.InsertColumn(0, L"Job", LVCFMT_LEFT, 160);
    listView_.InsertColumn(1, L"Source", LVCFMT_LEFT, 220);
    listView_.InsertColumn(2, L"Destination", LVCFMT_LEFT, 220);
    listView_.InsertColumn(3, L"Status", LVCFMT_LEFT, 100);
    listView_.InsertColumn(4, L"Progress", LVCFMT_LEFT, 120);

    layoutControls();
    return 0;
}

LRESULT TransfersView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    layoutControls();
    return 0;
}

void TransfersView::layoutControls() {
    if (!listView_.IsWindow()) return;
    CRect client;
    GetClientRect(&client);
    const int width = std::max(420, static_cast<int>(client.right) - 2 * kMargin);
    auto show = [](auto& control, bool visible) {
        if (control.IsWindow()) control.ShowWindow(visible ? SW_SHOW : SW_HIDE);
    };
    auto move = [](auto& control, int x, int y, int w, int h) {
        if (control.IsWindow()) control.MoveWindow(x, y, std::max(0, w), h, FALSE);
    };

    move(titleLabel_, kMargin, 22, 180, 36);
    move(newPanelButton_, kMargin + 190, 26, 150, 28);
    newPanelButton_.SetWindowText(newTransferExpanded_ ? L"Hide new transfer" : L"New transfer");
    for (CWindow* control : {static_cast<CWindow*>(&nameCaption_), static_cast<CWindow*>(&nameEdit_),
                             static_cast<CWindow*>(&sourceCaption_), static_cast<CWindow*>(&sourceEdit_),
                             static_cast<CWindow*>(&destCaption_), static_cast<CWindow*>(&destEdit_),
                             static_cast<CWindow*>(&enqueueButton_)}) show(*control, newTransferExpanded_);

    int actionTop = 66;
    if (newTransferExpanded_) {
        const int nameWidth = std::min(190, width / 4);
        const int pathWidth = std::max(180, (width - nameWidth - 160 - 42) / 2);
        move(nameCaption_, kMargin, 66, nameWidth, 18);
        move(nameEdit_, kMargin, 85, nameWidth, 24);
        int x = kMargin + nameWidth + 14;
        move(sourceCaption_, x, 66, pathWidth, 18);
        move(sourceEdit_, x, 85, pathWidth, 24); x += pathWidth + 14;
        move(destCaption_, x, 66, pathWidth, 18);
        move(destEdit_, x, 85, pathWidth, 24); x += pathWidth + 14;
        move(enqueueButton_, x, 84, 110, 28);
        actionTop = 120;
    }
    move(cancelButton_, kMargin, actionTop, 140, 28);
    move(refreshButton_, kMargin + 152, actionTop, 100, 28);
    move(statusLabel_, kMargin + 270, actionTop + 4, std::max(120, width - 270), 22);
    const int listTop = actionTop + 38;
    move(listView_, kMargin, listTop, width,
         std::max(100, static_cast<int>(client.bottom) - listTop - kMargin));
}

LRESULT TransfersView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorStatic(wParam); }
LRESULT TransfersView::OnCtlColorEdit(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorEdit(wParam); }

std::wstring TransfersView::getEditText(CEdit& edit) {
    const int len = edit.GetWindowTextLength();
    std::wstring text(static_cast<size_t>(len), L'\0');
    if (len > 0) edit.GetWindowText(text.data(), len + 1);
    return text;
}

void TransfersView::refresh() {
    if (!queue_ || !listView_.IsWindow()) return;

    theme::ApplyListViewTheme(listView_);
    listView_.DeleteAllItems();
    lastItems_.clear();

    int row = 0;
    for (const auto& job : queue_->getAllJobs()) {
        for (const auto& item : queue_->getItemsForJob(job.transferJobId)) {
            listView_.InsertItem(row, Utf8ToWide(job.name).c_str());
            listView_.SetItemText(row, 1, Utf8ToWide(item.sourcePath).c_str());
            listView_.SetItemText(row, 2, Utf8ToWide(item.destinationPath).c_str());
            listView_.SetItemText(row, 3, Utf8ToWide(gth::core::toString(item.status)).c_str());

            std::array<wchar_t, 64> progress{};
            swprintf_s(progress.data(), progress.size(), L"%lld / %lld bytes", item.bytesTransferred, item.sizeBytes);
            listView_.SetItemText(row, 4, progress.data());

            lastItems_.push_back(item);
            row++;
        }
    }
}

LRESULT TransfersView::OnEnqueueClick(WORD, WORD, HWND, BOOL&) {
    if (!queue_) return 0;

    const std::wstring name = getEditText(nameEdit_);
    const std::wstring source = getEditText(sourceEdit_);
    const std::wstring dest = getEditText(destEdit_);

    if (source.empty() || dest.empty()) {
        statusLabel_.SetWindowText(L"Source and destination paths are required.");
        return 0;
    }

    const std::string jobName = name.empty() ? "Transfer" : WideToUtf8(name);
    queue_->enqueueJob(jobName, {{WideToUtf8(source), WideToUtf8(dest)}});

    nameEdit_.SetWindowText(L"");
    sourceEdit_.SetWindowText(L"");
    destEdit_.SetWindowText(L"");

    statusLabel_.SetWindowText(L"Enqueued.");
    refresh();
    return 0;
}

LRESULT TransfersView::OnCancelClick(WORD, WORD, HWND, BOOL&) {
    if (!queue_) return 0;

    const int sel = listView_.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= lastItems_.size()) {
        statusLabel_.SetWindowText(L"Select a row first.");
        return 0;
    }

    queue_->cancelItem(lastItems_[static_cast<size_t>(sel)].transferItemId);
    statusLabel_.SetWindowText(L"Cancel requested.");
    refresh();
    return 0;
}

LRESULT TransfersView::OnRefreshClick(WORD, WORD, HWND, BOOL&) {
    refresh();
    statusLabel_.SetWindowText(L"Refreshed.");
    return 0;
}

LRESULT TransfersView::OnNewPanelClick(WORD, WORD, HWND, BOOL&) {
    newTransferExpanded_ = !newTransferExpanded_;
    layoutControls();
    return 0;
}
