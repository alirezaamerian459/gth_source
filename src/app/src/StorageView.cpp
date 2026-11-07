#include "StorageView.h"

#include <algorithm>
#include <array>
#include <format>
#include <unordered_set>

#include "gth/core/enums/StorageStatus.h"
#include "gth/core/enums/StorageType.h"

namespace {
constexpr int kTopAreaHeight = 184;
constexpr int kMargin = 24;

// The health estimate with a plain rating beside it ("87%  Good"). The percentage comes from the disk's own SMART
// data (see SmartHealth.h); where a disk can't be read that way, Windows' Healthy / Warning / Unhealthy shows up as
// 100 / 50 / 10.
std::wstring healthText(const std::optional<int>& percent) {
    if (!percent.has_value()) return L"Unknown";

    const wchar_t* rating = L"Critical";
    if (*percent >= 90) {
        rating = L"Excellent";
    } else if (*percent >= 70) {
        rating = L"Good";
    } else if (*percent >= 40) {
        rating = L"Fair";
    } else if (*percent >= 20) {
        rating = L"Poor";
    }

    return std::format(L"{}%  {}", *percent, rating);
}

std::wstring temperatureText(const std::optional<int>& celsius) {
    if (!celsius.has_value()) return L"Unknown";
    std::array<wchar_t, 32> buffer{};
    swprintf_s(buffer.data(), buffer.size(), L"%d°C", *celsius);
    return buffer.data();
}

std::wstring formatSizeBytes(unsigned long long bytes) {
    if (bytes == 0) return L"-";
    std::array<wchar_t, 48> buffer{};
    const double gb = static_cast<double>(bytes) / 1024.0 / 1024.0 / 1024.0;
    if (gb >= 1024.0) {
        swprintf_s(buffer.data(), buffer.size(), L"%.2f TB", gb / 1024.0);
    } else {
        swprintf_s(buffer.data(), buffer.size(), L"%.1f GB", gb);
    }
    return buffer.data();
}

std::wstring getEditText(CEdit& edit) {
    const int len = edit.GetWindowTextLength();
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    const int copied = edit.GetWindowText(text.data(), len + 1);
    text.resize(static_cast<size_t>(std::max(0, copied)));
    return text;
}

// Only runs while a background rescan is in flight (started in
// OnRescanClick, killed once it's picked up completion) - this app doesn't
// use WM_TIMER for anything else, so a short-lived one here doesn't
// establish a new polling convention, just bridges one background thread's
// result back onto the UI thread.
constexpr UINT_PTR kRescanPollTimerId = 1;
constexpr UINT kRescanPollIntervalMs = 200;

// Keeps capacity/free-space/health/temperature current without the user
// having to click Rescan - runs for the lifetime of the view, started in
// OnCreate and stopped in OnDestroy. startRescan() is a no-op while a scan
// is already in flight, so this can never pile up overlapping background
// rescans if one run takes longer than 5 seconds.
constexpr UINT_PTR kAutoRefreshTimerId = 2;
constexpr UINT kAutoRefreshIntervalMs = 5000;
}  // namespace

LRESULT StorageView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CRect titleRect(kMargin, 24, kMargin + 300, 60);
    titleLabel_.Create(m_hWnd, titleRect, L"Storage", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    CRect rescanRect(kMargin + 320, 28, kMargin + 420, 60);
    rescanButton_.Create(m_hWnd, rescanRect, L"Rescan", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                          IDC_RESCAN_BUTTON);
    rescanButton_.SetFont(theme::BodyFont());
    rescanButton_.setKind(ModernButton::Kind::Secondary);

    // Local drives are auto-detected by Rescan; this is for anything that
    // isn't - a network share (\\server\share) or any other path the
    // detector can't discover on its own.
    CRect addPathRect(kMargin, 70, kMargin + 420, 94);
    addPathEdit_.Create(m_hWnd, addPathRect, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 0,
                         IDC_STORAGE_ADD_PATH_EDIT);
    addPathEdit_.SetFont(theme::BodyFont());
    addPathEdit_.SendMessage(EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"D:\\Games or \\\\server\\share"));

    CRect addPathButtonRect(kMargin + 430, 70, kMargin + 550, 94);
    addPathButton_.Create(m_hWnd, addPathButtonRect, L"Add Storage", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                           0, IDC_STORAGE_ADD_BUTTON);
    addPathButton_.SetFont(theme::BodyFont());
    addPathButton_.setKind(ModernButton::Kind::Secondary);

    CRect addStatusRect(kMargin + 560, 74, kMargin + 960, 92);
    addStatusLabel_.Create(m_hWnd, addStatusRect, L"", WS_CHILD | WS_VISIBLE);
    addStatusLabel_.SetFont(theme::BodyFont());

    CRect categoryLabelRect(kMargin, 111, kMargin + 72, 134);
    categoryLabel_.Create(m_hWnd, categoryLabelRect, L"Category", WS_CHILD | WS_VISIBLE);
    categoryLabel_.SetFont(theme::BodyFont());

    CRect categoryComboRect(kMargin + 76, 108, kMargin + 286, 220);
    categoryCombo_.Create(m_hWnd, categoryComboRect, L"",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0,
                          IDC_STORAGE_CATEGORY_COMBO);
    categoryCombo_.SetFont(theme::BodyFont());

    CRect categoryNameRect(kMargin + 296, 108, kMargin + 486, 134);
    categoryNameEdit_.Create(m_hWnd, categoryNameRect, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 0,
                             IDC_STORAGE_CATEGORY_NAME_EDIT);
    categoryNameEdit_.SetFont(theme::BodyFont());
    categoryNameEdit_.SendMessage(EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"Category name"));

    CRect createCategoryRect(kMargin + 496, 108, kMargin + 594, 136);
    createCategoryButton_.Create(m_hWnd, createCategoryRect, L"Create",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                 IDC_STORAGE_CATEGORY_CREATE_BUTTON);
    createCategoryButton_.SetFont(theme::BodyFont());
    createCategoryButton_.setKind(ModernButton::Kind::Secondary);

    CRect renameCategoryRect(kMargin + 602, 108, kMargin + 700, 136);
    renameCategoryButton_.Create(m_hWnd, renameCategoryRect, L"Rename",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                 IDC_STORAGE_CATEGORY_RENAME_BUTTON);
    renameCategoryButton_.SetFont(theme::BodyFont());
    renameCategoryButton_.setKind(ModernButton::Kind::Secondary);

    CRect deleteCategoryRect(kMargin + 708, 108, kMargin + 806, 136);
    deleteCategoryButton_.Create(m_hWnd, deleteCategoryRect, L"Delete",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                 IDC_STORAGE_CATEGORY_DELETE_BUTTON);
    deleteCategoryButton_.SetFont(theme::BodyFont());
    deleteCategoryButton_.setKind(ModernButton::Kind::Secondary);

    CRect categoryStorageLabelRect(kMargin, 147, kMargin + 72, 170);
    categoryStorageLabel_.Create(m_hWnd, categoryStorageLabelRect, L"Add storage", WS_CHILD | WS_VISIBLE);
    categoryStorageLabel_.SetFont(theme::BodyFont());

    CRect categoryStorageComboRect(kMargin + 76, 144, kMargin + 396, 260);
    categoryStorageCombo_.Create(m_hWnd, categoryStorageComboRect, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0,
                                 IDC_STORAGE_CATEGORY_STORAGE_COMBO);
    categoryStorageCombo_.SetFont(theme::BodyFont());

    CRect addToCategoryRect(kMargin + 406, 144, kMargin + 536, 172);
    addToCategoryButton_.Create(m_hWnd, addToCategoryRect, L"Add to Category",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                IDC_STORAGE_CATEGORY_ADD_STORAGE_BUTTON);
    addToCategoryButton_.SetFont(theme::BodyFont());
    addToCategoryButton_.setKind(ModernButton::Kind::Secondary);

    CRect removeFromCategoryRect(kMargin + 544, 144, kMargin + 704, 172);
    removeFromCategoryButton_.Create(m_hWnd, removeFromCategoryRect, L"Remove Selected",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                                     IDC_STORAGE_CATEGORY_REMOVE_STORAGE_BUTTON);
    removeFromCategoryButton_.SetFont(theme::BodyFont());
    removeFromCategoryButton_.setKind(ModernButton::Kind::Secondary);

    CRect categoryStatusRect(kMargin + 714, 149, kMargin + 1040, 168);
    categoryStatusLabel_.Create(m_hWnd, categoryStatusRect, L"", WS_CHILD | WS_VISIBLE);
    categoryStatusLabel_.SetFont(theme::BodyFont());

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(kMargin, kTopAreaHeight, clientRect.right - kMargin, clientRect.bottom - kMargin);
    listView_.Create(m_hWnd, listRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL, 0,
                      IDC_STORAGE_LISTVIEW);
    listView_.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    listView_.SetFont(theme::BodyFont());
    theme::ApplyListViewTheme(listView_);

    listView_.InsertColumn(0, L"Name", LVCFMT_LEFT, 280);
    listView_.InsertColumn(1, L"Type", LVCFMT_LEFT, 80);
    listView_.InsertColumn(2, L"Path", LVCFMT_LEFT, 110);
    listView_.InsertColumn(3, L"Capacity", LVCFMT_LEFT, 85);
    listView_.InsertColumn(4, L"Free", LVCFMT_LEFT, 85);
    listView_.InsertColumn(5, L"Status", LVCFMT_LEFT, 70);
    listView_.InsertColumn(6, L"Health", LVCFMT_LEFT, 130);
    listView_.InsertColumn(7, L"Temp", LVCFMT_LEFT, 70);
    listView_.InsertColumn(8, L"Alert", LVCFMT_LEFT, 300);

    refresh();
    SetTimer(kAutoRefreshTimerId, kAutoRefreshIntervalMs);
    return 0;
}

LRESULT StorageView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    if (addStatusLabel_.IsWindow()) {
        addStatusLabel_.MoveWindow(kMargin + 560, 74, std::max(120, width - (kMargin + 560) - kMargin), 18);
    }
    if (categoryStatusLabel_.IsWindow()) {
        categoryStatusLabel_.MoveWindow(kMargin + 714, 149, std::max(120, width - (kMargin + 714) - kMargin), 19);
    }
    if (listView_.IsWindow()) {
        listView_.MoveWindow(kMargin, kTopAreaHeight, std::max(100, width - 2 * kMargin),
                              std::max(100, height - kTopAreaHeight - kMargin));
    }
    return 0;
}

LRESULT StorageView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    KillTimer(kRescanPollTimerId);
    KillTimer(kAutoRefreshTimerId);
    // A scan in flight is bounded (each drive probe has its own timeout) but
    // can still take a few seconds; joining here briefly delays shutdown
    // rather than risking a detached thread touching this object's members
    // after it's destroyed.
    if (rescanThread_.joinable()) rescanThread_.join();
    bHandled = FALSE;
    return 0;
}

LRESULT StorageView::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (wParam == kAutoRefreshTimerId) {
        if (IsWindowVisible()) startRescan();
        return 0;
    }

    if (wParam != kRescanPollTimerId) return 0;
    if (rescanInProgress_.load()) return 0;

    KillTimer(kRescanPollTimerId);
    if (rescanThread_.joinable()) rescanThread_.join();
    rescanButton_.EnableWindow(TRUE);
    rescanButton_.SetWindowText(L"Rescan");
    refresh();
    return 0;
}

LRESULT StorageView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorStatic(wParam); }

void StorageView::refresh() {
    if (!service_ || !listView_.IsWindow()) return;

    theme::ApplyListViewTheme(listView_);
    listView_.DeleteAllItems();
    lastRows_.clear();
    const auto storages = service_->getAllStorages();
    const auto categories = service_->getCategories();

    categoryCombo_.ResetContent();
    categoryIds_.clear();
    categoryCombo_.AddString(L"All storages");
    int selectedCategoryIndex = 0;
    bool selectedCategoryFound = selectedCategoryId_.empty();
    for (const auto& category : categories) {
        const int index = categoryCombo_.AddString(Utf8ToWide(category.name).c_str());
        categoryIds_.push_back(category.categoryId);
        if (category.categoryId == selectedCategoryId_) {
            selectedCategoryIndex = index;
            selectedCategoryFound = true;
        }
    }
    if (!selectedCategoryFound) selectedCategoryId_.clear();
    categoryCombo_.SetCurSel(selectedCategoryIndex);

    std::unordered_set<std::string> memberIds;
    if (!selectedCategoryId_.empty()) {
        for (const auto& storageId : service_->getStorageIdsForCategory(selectedCategoryId_)) {
            memberIds.insert(storageId);
        }
    }

    categoryStorageCombo_.ResetContent();
    categoryStorageIds_.clear();
    if (!selectedCategoryId_.empty()) {
        for (const auto& storage : storages) {
            if (memberIds.contains(storage.storageId)) continue;
            std::wstring label = Utf8ToWide(storage.name) + L" (" + Utf8ToWide(storage.rootPath) + L")";
            categoryStorageCombo_.AddString(label.c_str());
            categoryStorageIds_.push_back(storage.storageId);
        }
    }
    categoryStorageCombo_.SetCurSel(categoryStorageIds_.empty() ? -1 : 0);
    const bool hasCategory = !selectedCategoryId_.empty();
    renameCategoryButton_.EnableWindow(hasCategory);
    deleteCategoryButton_.EnableWindow(hasCategory);
    categoryStorageCombo_.EnableWindow(hasCategory && !categoryStorageIds_.empty());
    addToCategoryButton_.EnableWindow(hasCategory && !categoryStorageIds_.empty());
    removeFromCategoryButton_.EnableWindow(hasCategory);

    int row = 0;
    for (const auto& s : storages) {
        if (!selectedCategoryId_.empty() && !memberIds.contains(s.storageId)) continue;
        listView_.InsertItem(row, Utf8ToWide(s.name).c_str());
        listView_.SetItemText(row, 1, Utf8ToWide(gth::core::toString(s.type)).c_str());
        listView_.SetItemText(row, 2, Utf8ToWide(s.rootPath).c_str());

        wchar_t buf[64];
        swprintf_s(buf, L"%.2f GB", s.totalCapacityBytes / 1024.0 / 1024.0 / 1024.0);
        listView_.SetItemText(row, 3, buf);
        swprintf_s(buf, L"%.2f GB", s.freeSpaceBytes / 1024.0 / 1024.0 / 1024.0);
        listView_.SetItemText(row, 4, buf);

        listView_.SetItemText(row, 5, Utf8ToWide(gth::core::toString(s.status)).c_str());

        // A drive letter over several disks shows its worst health and hottest disk on this row; the disks follow.
        listView_.SetItemText(row, 6, healthText(s.healthPercent).c_str());
        listView_.SetItemText(row, 7, temperatureText(s.temperatureCelsius).c_str());
        listView_.SetItemText(row, 8, s.healthAlertReason ? Utf8ToWide(*s.healthAlertReason).c_str() : L"");

        lastRows_.push_back({s.healthAlertReason.has_value(), s.storageId});
        row++;

        // Every physical disk behind a drive that combines several, each with its own health and temperature.
        const auto disks = service_->getPhysicalDisks(s.rootPath);
        if (disks.size() < 2) continue;
        for (const auto& disk : disks) {
            std::wstring label = L"      Disk " + std::to_wstring(disk.diskNumber);
            if (!disk.model.empty()) label += L"  -  " + Utf8ToWide(disk.model);
            listView_.InsertItem(row, label.c_str());
            listView_.SetItemText(row, 1, L"Disk");
            listView_.SetItemText(row, 3, formatSizeBytes(disk.sizeBytes).c_str());
            listView_.SetItemText(row, 6, healthText(disk.healthPercent).c_str());
            listView_.SetItemText(row, 7, temperatureText(disk.temperatureCelsius).c_str());
            listView_.SetItemText(row, 8, Utf8ToWide(disk.healthDetail).c_str());  // why it isn't 100%

            const bool inTrouble = (disk.healthPercent && *disk.healthPercent < 100) ||
                                   (disk.temperatureCelsius && *disk.temperatureCelsius > gth::core::kOverheatThresholdCelsius);
            lastRows_.push_back({inTrouble, {}});
            row++;
        }
    }
}

LRESULT StorageView::OnRescanClick(WORD, WORD, HWND, BOOL&) {
    startRescan();
    return 0;
}

LRESULT StorageView::OnAddStorageClick(WORD, WORD, HWND, BOOL&) {
    if (!service_) return 0;

    const std::string path = WideToUtf8(getEditText(addPathEdit_));
    const auto result = service_->addManualStorage(path);

    if (result.success) {
        addPathEdit_.SetWindowText(L"");
        addStatusLabel_.SetWindowText(L"Added.");
        refresh();
    } else {
        addStatusLabel_.SetWindowText(Utf8ToWide(result.errorMessage).c_str());
    }
    return 0;
}

LRESULT StorageView::OnCategorySelectionChanged(WORD, WORD, HWND, BOOL&) {
    const int selectedIndex = categoryCombo_.GetCurSel();
    selectedCategoryId_.clear();
    if (selectedIndex > 0 && static_cast<size_t>(selectedIndex - 1) < categoryIds_.size()) {
        selectedCategoryId_ = categoryIds_[static_cast<size_t>(selectedIndex - 1)];
    }
    categoryStatusLabel_.SetWindowText(L"");
    refresh();
    return 0;
}

LRESULT StorageView::OnCreateCategoryClick(WORD, WORD, HWND, BOOL&) {
    if (!service_) return 0;

    const auto result = service_->createCategory(WideToUtf8(getEditText(categoryNameEdit_)));
    if (!result.success) {
        categoryStatusLabel_.SetWindowText(Utf8ToWide(result.errorMessage).c_str());
        return 0;
    }

    selectedCategoryId_ = result.categoryId;
    categoryNameEdit_.SetWindowText(L"");
    categoryStatusLabel_.SetWindowText(L"Category created.");
    refresh();
    return 0;
}

LRESULT StorageView::OnRenameCategoryClick(WORD, WORD, HWND, BOOL&) {
    if (!service_) return 0;
    if (selectedCategoryId_.empty()) {
        categoryStatusLabel_.SetWindowText(L"Select a category to rename.");
        return 0;
    }

    const auto result = service_->renameCategory(selectedCategoryId_, WideToUtf8(getEditText(categoryNameEdit_)));
    categoryStatusLabel_.SetWindowText(result.success ? L"Category renamed."
                                                        : Utf8ToWide(result.errorMessage).c_str());
    if (result.success) refresh();
    return 0;
}

LRESULT StorageView::OnDeleteCategoryClick(WORD, WORD, HWND, BOOL&) {
    if (!service_) return 0;
    if (selectedCategoryId_.empty()) {
        categoryStatusLabel_.SetWindowText(L"Select a category to delete.");
        return 0;
    }

    const int answer = ::MessageBoxW(m_hWnd,
                                     L"Delete this category? The storage locations and their game data will remain.",
                                     L"Delete Storage Category", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer != IDYES) return 0;

    const auto result = service_->deleteCategory(selectedCategoryId_);
    if (!result.success) {
        categoryStatusLabel_.SetWindowText(Utf8ToWide(result.errorMessage).c_str());
        return 0;
    }

    selectedCategoryId_.clear();
    categoryNameEdit_.SetWindowText(L"");
    categoryStatusLabel_.SetWindowText(L"Category deleted. Storages are unchanged.");
    refresh();
    return 0;
}

LRESULT StorageView::OnCategoryStorageSelectionChanged(WORD, WORD, HWND, BOOL&) {
    addToCategoryButton_.EnableWindow(!selectedCategoryId_.empty() && categoryStorageCombo_.GetCurSel() >= 0);
    return 0;
}

LRESULT StorageView::OnAddStorageToCategoryClick(WORD, WORD, HWND, BOOL&) {
    if (!service_) return 0;
    if (selectedCategoryId_.empty()) {
        categoryStatusLabel_.SetWindowText(L"Select a category first.");
        return 0;
    }

    const int selectedIndex = categoryStorageCombo_.GetCurSel();
    if (selectedIndex < 0 || static_cast<size_t>(selectedIndex) >= categoryStorageIds_.size()) {
        categoryStatusLabel_.SetWindowText(L"No unassigned storage is available for this category.");
        return 0;
    }

    const auto result = service_->addStorageToCategory(
        selectedCategoryId_, categoryStorageIds_[static_cast<size_t>(selectedIndex)]);
    categoryStatusLabel_.SetWindowText(result.success ? L"Storage added to category."
                                                        : Utf8ToWide(result.errorMessage).c_str());
    if (result.success) refresh();
    return 0;
}

LRESULT StorageView::OnRemoveStorageFromCategoryClick(WORD, WORD, HWND, BOOL&) {
    if (!service_) return 0;
    if (selectedCategoryId_.empty()) {
        categoryStatusLabel_.SetWindowText(L"Select a category first.");
        return 0;
    }

    const int row = listView_.GetSelectedIndex();
    if (row < 0 || static_cast<size_t>(row) >= lastRows_.size() || lastRows_[static_cast<size_t>(row)].storageId.empty()) {
        categoryStatusLabel_.SetWindowText(L"Select a storage row to remove from this category.");
        return 0;
    }

    const auto result = service_->removeStorageFromCategory(
        selectedCategoryId_, lastRows_[static_cast<size_t>(row)].storageId);
    categoryStatusLabel_.SetWindowText(result.success ? L"Storage removed from category."
                                                        : Utf8ToWide(result.errorMessage).c_str());
    if (result.success) refresh();
    return 0;
}

void StorageView::startRescan() {
    if (!service_ || rescanInProgress_.load()) return;

    if (rescanThread_.joinable()) rescanThread_.join();  // previous run's thread, already finished

    rescanInProgress_.store(true);
    rescanButton_.EnableWindow(FALSE);
    rescanButton_.SetWindowText(L"Scanning...");

    rescanThread_ = std::thread([this] {
        service_->detectAndSyncLocalStorages();
        rescanInProgress_.store(false);
    });
    SetTimer(kRescanPollTimerId, kRescanPollIntervalMs);
}

// Paints a flagged storage's row with a red tint (light red background,
// dark red text) instead of just relying on the Alert column's text -
// lastRows_ is kept parallel to the ListView's rows so this can look
// the flag up by row index without a round-trip through the service.
LRESULT StorageView::OnListViewCustomDraw(int, LPNMHDR pnmh, BOOL&) {
    auto* customDraw = reinterpret_cast<LPNMLVCUSTOMDRAW>(pnmh);

    switch (customDraw->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT: {
            const size_t index = customDraw->nmcd.dwItemSpec;
            const bool flagged = index < lastRows_.size() && lastRows_[index].flagged;
            if (flagged) {
                customDraw->clrTextBk = RGB(253, 231, 228);
                customDraw->clrText = theme::Danger();
            }
            return CDRF_NEWFONT;
        }
        default:
            return CDRF_DODEFAULT;
    }
}
