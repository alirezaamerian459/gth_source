#include "StorageView.h"

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <unordered_set>

#include "gth/core/enums/StorageStatus.h"
#include "gth/core/enums/StorageType.h"

namespace {
constexpr int kMargin = 24;
constexpr UINT_PTR kRescanPollTimerId = 1;
constexpr UINT kRescanPollIntervalMs = 200;
constexpr UINT_PTR kAutoRefreshTimerId = 2;
constexpr UINT kAutoRefreshIntervalMs = 5000;

// Popup command ranges are local to the Storage page and are never sent to
// the frame, so they cannot collide with normal control IDs.
constexpr UINT kAddStorageCommandBase = 4000;
constexpr UINT kRemoveStorageCommandBase = 5000;
constexpr UINT kDeleteCategoryCommand = 6000;

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
    if (const double gb = static_cast<double>(bytes) / 1024.0 / 1024.0 / 1024.0; gb >= 1024.0) {
        swprintf_s(buffer.data(), buffer.size(), L"%.2f TB", gb / 1024.0);
    } else {
        swprintf_s(buffer.data(), buffer.size(), L"%.1f GB", gb);
    }
    return buffer.data();
}

std::wstring smartMetricValue(const std::optional<std::uint64_t>& value) {
    return value.has_value() ? std::format(L"{}", *value) : L"-";
}

int smartMetricRows(const gth::core::PhysicalDiskHealth& disk, bool detailsVisible) {
    return detailsVisible && disk.smartMetrics.hasTelemetry() ? 4 : 0;
}

bool smartStatusNeedsAttention(const std::string& status) {
    return !status.empty() && status != "Good" && status != "Healthy" && status != "OK";
}

std::wstring getEditText(CEdit& edit) {
    const int len = edit.GetWindowTextLength();
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    const int copied = edit.GetWindowText(text.data(), len + 1);
    text.resize(static_cast<size_t>(std::max(0, copied)));
    return text;
}

}  // namespace

void StorageBoardWindow::setContentExtent(int width, int height) {
    contentWidth_ = std::max(0, width);
    contentHeight_ = std::max(0, height);
    updateScrollBars();
}

int StorageBoardWindow::maxHorizontal() const {
    RECT rect{};
    ::GetClientRect(m_hWnd, &rect);
    return std::max(0, contentWidth_ - static_cast<int>(rect.right - rect.left));
}

int StorageBoardWindow::maxVertical() const {
    RECT rect{};
    ::GetClientRect(m_hWnd, &rect);
    return std::max(0, contentHeight_ - static_cast<int>(rect.bottom - rect.top));
}

void StorageBoardWindow::updateScrollBars() {
    if (!m_hWnd) return;
    RECT rect{};
    ::GetClientRect(m_hWnd, &rect);

    horizontalPosition_ = std::min(horizontalPosition_, maxHorizontal());
    verticalPosition_ = std::min(verticalPosition_, maxVertical());

    SCROLLINFO horizontal{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
    horizontal.nMin = 0;
    horizontal.nMax = std::max(0, contentWidth_ - 1);
    horizontal.nPage = static_cast<UINT>(std::max(0, static_cast<int>(rect.right - rect.left)));
    horizontal.nPos = horizontalPosition_;
    ::SetScrollInfo(m_hWnd, SB_HORZ, &horizontal, TRUE);
    ::ShowScrollBar(m_hWnd, SB_HORZ, contentWidth_ > static_cast<int>(rect.right - rect.left));

    SCROLLINFO vertical{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
    vertical.nMin = 0;
    vertical.nMax = std::max(0, contentHeight_ - 1);
    vertical.nPage = static_cast<UINT>(std::max(0, static_cast<int>(rect.bottom - rect.top)));
    vertical.nPos = verticalPosition_;
    ::SetScrollInfo(m_hWnd, SB_VERT, &vertical, TRUE);
    ::ShowScrollBar(m_hWnd, SB_VERT, contentHeight_ > static_cast<int>(rect.bottom - rect.top));
}

void StorageBoardWindow::scrollTo(int horizontal, int vertical) {
    horizontalPosition_ = std::clamp(horizontal, 0, maxHorizontal());
    verticalPosition_ = std::clamp(vertical, 0, maxVertical());
    updateScrollBars();
    Invalidate(FALSE);
}

LRESULT StorageBoardWindow::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
    PAINTSTRUCT paint{};
    HDC hdc = ::BeginPaint(m_hWnd, &paint);
    RECT client{};
    ::GetClientRect(m_hWnd, &client);
    if (owner_) owner_->paintCategoryBoard(hdc, client, horizontalPosition_, verticalPosition_);
    ::EndPaint(m_hWnd, &paint);
    return 0;
}

LRESULT StorageBoardWindow::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
    updateScrollBars();
    Invalidate(FALSE);
    return 0;
}

LRESULT StorageBoardWindow::OnHScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
    SCROLLINFO info{sizeof(SCROLLINFO), SIF_ALL};
    ::GetScrollInfo(m_hWnd, SB_HORZ, &info);
    int next = horizontalPosition_;
    switch (LOWORD(wParam)) {
        case SB_LINELEFT: next -= 48; break;
        case SB_LINERIGHT: next += 48; break;
        case SB_PAGELEFT: next -= static_cast<int>(info.nPage); break;
        case SB_PAGERIGHT: next += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: next = info.nTrackPos; break;
        case SB_LEFT: next = 0; break;
        case SB_RIGHT: next = maxHorizontal(); break;
        default: return 0;
    }
    scrollTo(next, verticalPosition_);
    return 0;
}

LRESULT StorageBoardWindow::OnVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
    SCROLLINFO info{sizeof(SCROLLINFO), SIF_ALL};
    ::GetScrollInfo(m_hWnd, SB_VERT, &info);
    int next = verticalPosition_;
    switch (LOWORD(wParam)) {
        case SB_LINEUP: next -= 32; break;
        case SB_LINEDOWN: next += 32; break;
        case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: next = info.nTrackPos; break;
        case SB_TOP: next = 0; break;
        case SB_BOTTOM: next = maxVertical(); break;
        default: return 0;
    }
    scrollTo(horizontalPosition_, next);
    return 0;
}

LRESULT StorageBoardWindow::OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL&) {
    const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    scrollTo(horizontalPosition_, verticalPosition_ - (delta / WHEEL_DELTA) * 64);
    return 0;
}

LRESULT StorageBoardWindow::OnContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
    if (!owner_) return 0;
    POINT screenPoint{static_cast<SHORT>(LOWORD(lParam)), static_cast<SHORT>(HIWORD(lParam))};
    owner_->showCategoryContextMenu(screenPoint);
    return 0;
}

LRESULT StorageView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CRect titleRect(kMargin, 24, kMargin + 180, 60);
    titleLabel_.Create(m_hWnd, titleRect, L"Drives & Storage", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    auto makeButton = [&](ModernButton& button, const wchar_t* text, UINT id, ModernButton::Kind kind) {
        CRect rect(0, 0, 100, 28);
        button.Create(m_hWnd, rect, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, id);
        button.SetFont(theme::BodyFont());
        button.setKind(kind);
    };
    makeButton(rescanButton_, L"Rescan", IDC_RESCAN_BUTTON, ModernButton::Kind::Secondary);

    CRect controlRect(0, 0, 220, 24);
    categoryNameEdit_.Create(m_hWnd, controlRect, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 0,
                             IDC_STORAGE_CATEGORY_NAME_EDIT);
    categoryNameEdit_.SetFont(theme::BodyFont());
    categoryNameEdit_.SendMessage(EM_SETCUEBANNER, FALSE,
                                  std::bit_cast<LPARAM>(static_cast<const wchar_t*>(L"Category name")));
    makeButton(createCategoryButton_, L"Add category", IDC_STORAGE_CATEGORY_CREATE_BUTTON,
               ModernButton::Kind::Primary);
    makeButton(healthDetailsButton_, L"Show health details", IDC_STORAGE_HEALTH_DETAILS_BUTTON,
               ModernButton::Kind::Subtle);

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect boardRect(kMargin, 78, clientRect.right - kMargin, clientRect.bottom - kMargin);
    board_.init(*this);
    board_.Create(m_hWnd, boardRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, 0,
                  IDC_STORAGE_LISTVIEW);

    refresh();
    // The first scan is automatic; the Rescan button remains as an explicit
    // retry when a removable or network-backed drive becomes available.
    startRescan();
    SetTimer(kAutoRefreshTimerId, kAutoRefreshIntervalMs);
    return 0;
}

LRESULT StorageView::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
    layoutControls();
    return 0;
}

void StorageView::layoutControls() {
    if (!board_.IsWindow()) return;

    CRect client;
    GetClientRect(&client);
    const int width = std::max(400, client.Width() - (2 * kMargin));
    auto move = [](auto& control, int x, int y, int w, int h) {
        if (control.IsWindow()) control.MoveWindow(x, y, std::max(0, w), h, FALSE);
    };

    int x = kMargin + 190;
    move(rescanButton_, x, 27, 88, 28);
    x += 96;

    const int categoryEditWidth = std::max(150, std::min(260, width - 390));
    move(categoryNameEdit_, x, 28, categoryEditWidth, 24);
    x += categoryEditWidth + 8;
    move(createCategoryButton_, x, 27, 118, 28);
    x += 126;
    move(healthDetailsButton_, x, 27, std::max(150, width - (x - kMargin)), 28);
    healthDetailsButton_.SetWindowText(healthDetailsVisible_ ? L"Hide health details" : L"Show health details");

    move(board_, kMargin, 78, width,
         std::max(120, client.Height() - 78 - kMargin));
    updateBoardExtent();
}

int StorageView::categoryCardWidth() const {
    if (!board_.IsWindow()) return 360;
    CRect client;
    board_.GetClientRect(&client);
    if (categoryCards_.size() <= 1) return std::max(320, client.Width() - 32);
    const int available = std::max(640, client.Width() - 48);
    return std::max(320, std::min(460, available / 2));
}

void StorageView::updateBoardExtent() {
    if (!board_.IsWindow()) return;
    const int width = categoryCardWidth();
    const int spacing = 16;
    const int columns = categoryCards_.size() > 1 ? 2 : 1;
    std::vector<int> rowHeights;
    for (size_t index = 0; index < categoryCards_.size(); index += static_cast<size_t>(columns)) {
        int rowHeight = 0;
        for (int column = 0; column < columns && index + static_cast<size_t>(column) < categoryCards_.size(); ++column) {
            const auto& category = categoryCards_[index + static_cast<size_t>(column)];
            int categoryHeight = 64;
            for (const auto& storage : category.storages) categoryHeight += storageCardHeight(storage) + 10;
            if (category.storages.empty()) categoryHeight += 34;
            rowHeight = std::max(rowHeight, categoryHeight);
        }
        rowHeights.push_back(rowHeight);
    }
    int height = 112 + (rowHeights.empty() ? 0 : spacing);
    for (const int rowHeight : rowHeights) height += rowHeight + spacing;
    board_.setContentExtent(categoryCards_.empty() ? 0 : columns * width + (columns + 1) * spacing, height);
}

void StorageView::startRescan() {
    if (!service_ || rescanInProgress_.load()) return;
    if (rescanThread_.joinable()) rescanThread_.join();

    rescanInProgress_.store(true);
    rescanButton_.EnableWindow(FALSE);
    rescanButton_.SetWindowText(L"Scanning...");
    rescanThread_ = std::jthread([this] {
        service_->detectAndSyncLocalStorages();
        rescanInProgress_.store(false);
    });
    SetTimer(kRescanPollTimerId, kRescanPollIntervalMs);
}

int StorageView::storageCardHeight(const StorageCard& card) const {
    int height = 178 + static_cast<int>(card.disks.size()) * 54;
    if (healthDetailsVisible_) {
        if (card.storage.healthAlertReason.has_value()) height += 18;
        for (const auto& disk : card.disks) {
            if (!disk.healthDetail.empty()) height += 18;
            height += smartMetricRows(disk, healthDetailsVisible_) * 18;
        }
    }
    return height;
}

void StorageView::refresh() {
    if (!service_ || !board_.IsWindow()) return;

    const auto storages = service_->getAllStorages();
    const auto categories = service_->getCategories();
    std::vector<StorageCard> allStorageCards;
    allStorageCards.reserve(storages.size());

    for (const auto& storage : storages) {
        StorageCard card;
        card.storage = storage;
        card.disks = service_->getPhysicalDisks(storage.rootPath);
        card.flagged = storage.healthAlertReason.has_value();
        for (const auto& disk : card.disks) {
            card.flagged = card.flagged ||
                           (disk.healthPercent.has_value() && *disk.healthPercent < 100) ||
                           smartStatusNeedsAttention(disk.smartStatus) ||
                           (disk.temperatureCelsius.has_value() &&
                            *disk.temperatureCelsius > gth::core::kOverheatThresholdCelsius);
        }
        allStorageCards.push_back(std::move(card));
    }

    totalCapacityBytes_ = 0;
    totalUsedBytes_ = 0;
    for (const auto& card : allStorageCards) {
        totalCapacityBytes_ += std::max<long long>(0, card.storage.totalCapacityBytes);
        totalUsedBytes_ += std::clamp(card.storage.usedSpaceBytes, 0LL,
                                      std::max<long long>(0, card.storage.totalCapacityBytes));
    }

    categoryCards_.clear();
    std::unordered_set<std::string> assignedStorageIds;
    for (const auto& category : categories) {
        CategoryCard categoryCard;
        categoryCard.categoryId = category.categoryId;
        categoryCard.name = Utf8ToWide(category.name);
        const auto memberIds = service_->getStorageIdsForCategory(category.categoryId);
        const std::unordered_set<std::string> members(memberIds.begin(), memberIds.end());

        for (const auto& storageCard : allStorageCards) {
            if (!members.contains(storageCard.storage.storageId)) continue;
            categoryCard.storages.push_back(storageCard);
            assignedStorageIds.insert(storageCard.storage.storageId);
        }
        categoryCards_.push_back(std::move(categoryCard));
    }

    CategoryCard ungrouped;
    ungrouped.name = L"Other storages";
    ungrouped.uncategorized = true;
    for (const auto& storageCard : allStorageCards) {
        if (!assignedStorageIds.contains(storageCard.storage.storageId)) {
            ungrouped.storages.push_back(storageCard);
        }
    }
    if (!ungrouped.storages.empty()) categoryCards_.push_back(std::move(ungrouped));

    categorySummaries_.clear();
    for (const auto& category : categoryCards_) {
        CategorySummary summary;
        summary.name = category.name;
        for (const auto& storage : category.storages) {
            summary.capacityBytes += std::max<long long>(0, storage.storage.totalCapacityBytes);
            summary.usedBytes += std::clamp(storage.storage.usedSpaceBytes, 0LL,
                                            std::max<long long>(0, storage.storage.totalCapacityBytes));
        }
        categorySummaries_.push_back(std::move(summary));
    }

    updateBoardExtent();
    board_.refresh();
}

LRESULT StorageView::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (wParam == kAutoRefreshTimerId) {
        if (IsWindowVisible()) startRescan();
        return 0;
    }
    if (wParam != kRescanPollTimerId || rescanInProgress_.load()) return 0;

    KillTimer(kRescanPollTimerId);
    if (rescanThread_.joinable()) rescanThread_.join();
    rescanButton_.EnableWindow(TRUE);
    rescanButton_.SetWindowText(L"Rescan");
    refresh();
    return 0;
}

LRESULT StorageView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    KillTimer(kRescanPollTimerId);
    KillTimer(kAutoRefreshTimerId);
    if (rescanThread_.joinable()) rescanThread_.join();
    bHandled = FALSE;
    return 0;
}

LRESULT StorageView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) {
    return HandleCtlColorStatic(wParam);
}

void StorageView::paintCategoryBoard(HDC hdc, const RECT& clientRect, int horizontalOffset, int verticalOffset) {
    CRect itemRect(clientRect);
    itemRect.OffsetRect(-horizontalOffset, -verticalOffset);
    HBRUSH background = ::CreateSolidBrush(theme::ContentBg());
    ::FillRect(hdc, &clientRect, background);
    ::DeleteObject(background);

    auto drawText = [&](const std::wstring& text, int x, int y, int width, int height, HFONT font,
                        COLORREF color, UINT format) {
        CRect textRect(x, y, x + std::max(0, width), y + height);
        HGDIOBJ oldFont = ::SelectObject(hdc, font);
        const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
        const COLORREF oldColor = ::SetTextColor(hdc, color);
        ::DrawTextW(hdc, text.c_str(), -1, &textRect, format | DT_NOPREFIX | DT_END_ELLIPSIS);
        ::SetTextColor(hdc, oldColor);
        ::SetBkMode(hdc, oldMode);
        ::SelectObject(hdc, oldFont);
    };

    auto drawStorage = [&](const StorageCard& card, const CRect& storageRect) {
        const auto& storage = card.storage;
        theme::FillRoundRect(hdc, storageRect, 7, theme::ContentBg(),
                             card.flagged ? theme::Danger() : theme::Border());
        const int storageLeft = storageRect.left + 12;
        const int storageRight = storageRect.right - 12;
        const int innerWidth = std::max(0, storageRight - storageLeft);
        const int top = storageRect.top + 9;
        const std::wstring name = storage.name.empty() ? Utf8ToWide(storage.rootPath) : Utf8ToWide(storage.name);
        drawText(name, storageLeft, top, std::max(0, innerWidth - 130), 22, theme::BodyFontBold(),
                 theme::TextPrimary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        drawText(Utf8ToWide(gth::core::toString(storage.status)), storageRight - 130, top, 130, 22,
                 theme::SmallFont(), card.flagged ? theme::Danger() : theme::TextSecondary(),
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        std::wstring location = Utf8ToWide(storage.rootPath) + L"   ·   " +
                                Utf8ToWide(gth::core::toString(storage.type));
        if (storage.fileSystem.has_value()) location += L"   ·   " + Utf8ToWide(*storage.fileSystem);
        drawText(location, storageLeft, top + 24, innerWidth, 18, theme::SmallFont(), theme::TextSecondary(),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const long long total = std::max<long long>(0, storage.totalCapacityBytes);
        const long long free = std::clamp(storage.freeSpaceBytes, 0LL, total);
        const long long used = std::clamp(storage.usedSpaceBytes, 0LL, total);
        const int colWidth = innerWidth / 3;
        const std::array<std::wstring, 3> labels{L"CAPACITY", L"USED", L"FREE"};
        const std::array<std::wstring, 3> values{
            formatSizeBytes(static_cast<unsigned long long>(total)),
            formatSizeBytes(static_cast<unsigned long long>(used)),
            formatSizeBytes(static_cast<unsigned long long>(free))};
        for (int column = 0; column < 3; ++column) {
            const int x = storageLeft + column * colWidth;
            drawText(labels[static_cast<size_t>(column)], x, top + 46, colWidth, 14, theme::SmallFont(),
                     theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            drawText(values[static_cast<size_t>(column)], x, top + 60, colWidth, 20, theme::BodyFontBold(),
                     theme::TextPrimary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        CRect barRect(storageLeft, top + 84, storageRight, top + 92);
        HBRUSH track = ::CreateSolidBrush(theme::HoverBg());
        ::FillRect(hdc, &barRect, track);
        ::DeleteObject(track);
        if (total > 0) {
            const int fillWidth = static_cast<int>((static_cast<double>(used) / static_cast<double>(total)) * barRect.Width());
            if (fillWidth > 0) {
                CRect fillRect = barRect;
                fillRect.right = fillRect.left + std::min(fillWidth, barRect.Width());
                HBRUSH usedBrush = ::CreateSolidBrush(theme::Accent());
                ::FillRect(hdc, &fillRect, usedBrush);
                ::DeleteObject(usedBrush);
            }
        }

        std::wstring healthLine = L"Health  " + healthText(storage.healthPercent) + L"     Temperature  " +
                                  temperatureText(storage.temperatureCelsius);
        drawText(healthLine, storageLeft, top + 97, innerWidth, 18, theme::SmallFont(),
                 card.flagged ? theme::Danger() : theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        int detailY = top + 121;
        for (const auto& disk : card.disks) {
            const bool hasDetail = healthDetailsVisible_ && !disk.healthDetail.empty();
            const int diskHeight = 48 + (hasDetail ? 18 : 0) + smartMetricRows(disk, healthDetailsVisible_) * 18;
            CRect diskRect(storageLeft, detailY - 3, storageRight, detailY + diskHeight - 3);
            theme::FillRoundRect(hdc, diskRect, 6, theme::SurfaceBg(), theme::Border());

            std::wstring title = std::format(L"Physical disk {}", disk.diskNumber);
            if (!disk.model.empty()) title += L"  ·  " + Utf8ToWide(disk.model);
            title += L"  ·  " + formatSizeBytes(disk.sizeBytes);
            drawText(title, storageLeft + 10, detailY + 3, std::max(0, innerWidth - 20), 18,
                     theme::BodyFontBold(), theme::TextPrimary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            std::wstring diskStatus = L"Health  " + healthText(disk.healthPercent) + L"     Temperature  " +
                                       temperatureText(disk.temperatureCelsius);
            if (!disk.smartStatus.empty()) diskStatus += L"     SMART  " + Utf8ToWide(disk.smartStatus);
            const bool diskFlagged = (disk.healthPercent.has_value() && *disk.healthPercent < 70) ||
                                     smartStatusNeedsAttention(disk.smartStatus) ||
                                     (disk.temperatureCelsius.has_value() &&
                                      *disk.temperatureCelsius > gth::core::kOverheatThresholdCelsius);
            drawText(diskStatus, storageLeft + 10, detailY + 22, std::max(0, innerWidth - 20), 18,
                     theme::SmallFont(), diskFlagged ? theme::Danger() : theme::TextSecondary(),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (hasDetail) {
                drawText(Utf8ToWide(disk.healthDetail), storageLeft + 10, detailY + 40,
                         std::max(0, innerWidth - 20), 18, theme::SmallFont(), theme::TextSecondary(),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            int metricsY = detailY + 40 + (hasDetail ? 18 : 0);
            if (smartMetricRows(disk, healthDetailsVisible_) > 0) {
                const auto& metrics = disk.smartMetrics;
                const std::wstring usage = std::format(
                    L"Power-on {} h  ·  Start/stop {}  ·  Power cycles {}  ·  Load cycles {}",
                    smartMetricValue(metrics.powerOnHours), smartMetricValue(metrics.startStopCount),
                    smartMetricValue(metrics.powerCycleCount), smartMetricValue(metrics.loadCycleCount));
                const std::wstring errors = std::format(
                    L"Reallocated {}  ·  Pending {}  ·  Uncorrectable {}  ·  Timeouts {}",
                    smartMetricValue(metrics.reallocatedSectorCount), smartMetricValue(metrics.pendingSectorCount),
                    smartMetricValue(metrics.uncorrectableSectorCount), smartMetricValue(metrics.commandTimeoutCount));
                const std::wstring transport = std::format(
                    L"Read errors {}  ·  Seek errors {}  ·  Spin retries {}  ·  End-to-end {}",
                    smartMetricValue(metrics.rawReadErrorRate), smartMetricValue(metrics.seekErrorRate),
                    smartMetricValue(metrics.spinRetryCount), smartMetricValue(metrics.endToEndErrorCount));
                const std::wstring io = std::format(
                    L"LBA written {}  ·  LBA read {}  ·  Head-flying {} h",
                    smartMetricValue(metrics.totalLbaWritten), smartMetricValue(metrics.totalLbaRead),
                    smartMetricValue(metrics.headFlyingHours));
                drawText(usage, storageLeft + 10, metricsY, std::max(0, innerWidth - 20), 18,
                         theme::SmallFont(), theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                drawText(errors, storageLeft + 10, metricsY + 18, std::max(0, innerWidth - 20), 18,
                         theme::SmallFont(), theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                drawText(transport, storageLeft + 10, metricsY + 36, std::max(0, innerWidth - 20), 18,
                         theme::SmallFont(), theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                drawText(io, storageLeft + 10, metricsY + 54, std::max(0, innerWidth - 20), 18,
                         theme::SmallFont(), theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            detailY += diskHeight + 6;
        }
        if (card.disks.empty()) {
            drawText(L"Physical disk details unavailable", storageLeft, detailY, innerWidth, 18,
                     theme::SmallFont(), theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        if (healthDetailsVisible_ && storage.healthAlertReason.has_value()) {
            drawText(Utf8ToWide(*storage.healthAlertReason), storageLeft, storageRect.bottom - 22, innerWidth, 18,
                     theme::SmallFont(), theme::Danger(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    };

    const int width = categoryCardWidth();
    const int spacing = 16;
    const int columns = categoryCards_.size() > 1 ? 2 : 1;
    const int contentWidth = columns * width + (columns + 1) * spacing;

    CRect summaryRect(spacing - horizontalOffset, 12 - verticalOffset,
                      contentWidth - spacing - horizontalOffset, 96 - verticalOffset);
    theme::FillRoundRect(hdc, summaryRect, 10, theme::SurfaceBg(), theme::Border());
    drawText(L"STORAGE OVERVIEW", summaryRect.left + 18, summaryRect.top + 10, 190, 16,
             theme::SmallFont(), theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const std::wstring aggregate = formatSizeBytes(static_cast<unsigned long long>(totalCapacityBytes_)) +
                                   L" total  ·  " +
                                   formatSizeBytes(static_cast<unsigned long long>(totalUsedBytes_)) + L" used";
    drawText(aggregate, summaryRect.left + 18, summaryRect.top + 28, 260, 28,
             theme::BodyFontBold(), theme::TextPrimary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const double usedRatio = totalCapacityBytes_ > 0
                                 ? static_cast<double>(totalUsedBytes_) / static_cast<double>(totalCapacityBytes_)
                                 : 0.0;
    drawText(std::format(L"{:.1f}% allocated", std::clamp(usedRatio * 100.0, 0.0, 100.0)),
             summaryRect.right - 170, summaryRect.top + 12, 150, 18, theme::SmallFont(),
             theme::TextSecondary(), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    CRect summaryBar(summaryRect.right - 260, summaryRect.top + 40, summaryRect.right - 18, summaryRect.top + 50);
    HBRUSH summaryTrack = ::CreateSolidBrush(theme::HoverBg());
    ::FillRect(hdc, &summaryBar, summaryTrack);
    ::DeleteObject(summaryTrack);
    if (summaryBar.Width() > 0 && usedRatio > 0.0) {
        CRect summaryFill = summaryBar;
        summaryFill.right = summaryFill.left +
                            static_cast<int>(summaryBar.Width() * std::clamp(usedRatio, 0.0, 1.0));
        HBRUSH summaryBrush = ::CreateSolidBrush(theme::Accent());
        ::FillRect(hdc, &summaryFill, summaryBrush);
        ::DeleteObject(summaryBrush);
    }
    for (size_t index = 0; index < categorySummaries_.size() && index < 4; ++index) {
        const int legendX = summaryRect.left + 300 + static_cast<int>(index) * 118;
        const int legendY = summaryRect.top + 17;
        HBRUSH dotBrush = ::CreateSolidBrush(theme::Accent());
        HGDIOBJ oldBrush = ::SelectObject(hdc, dotBrush);
        ::Ellipse(hdc, legendX, legendY + 3, legendX + 9, legendY + 12);
        ::SelectObject(hdc, oldBrush);
        ::DeleteObject(dotBrush);
        drawText(categorySummaries_[index].name, legendX + 14, legendY, 96, 18, theme::SmallFont(),
                 theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    std::vector<int> rowHeights;
    for (size_t index = 0; index < categoryCards_.size(); index += static_cast<size_t>(columns)) {
        int rowHeight = 0;
        for (int column = 0; column < columns && index + static_cast<size_t>(column) < categoryCards_.size(); ++column) {
            const auto& category = categoryCards_[index + static_cast<size_t>(column)];
            int categoryHeight = 64;
            for (const auto& storage : category.storages) categoryHeight += storageCardHeight(storage) + 10;
            if (category.storages.empty()) categoryHeight += 34;
            rowHeight = std::max(rowHeight, categoryHeight);
        }
        rowHeights.push_back(rowHeight);
    }

    std::vector<int> rowTops(rowHeights.size(), 112);
    for (size_t row = 1; row < rowTops.size(); ++row) {
        rowTops[row] = rowTops[row - 1] + rowHeights[row - 1] + spacing;
    }

    for (size_t index = 0; index < categoryCards_.size(); ++index) {
        const int row = static_cast<int>(index / static_cast<size_t>(columns));
        const int column = static_cast<int>(index % static_cast<size_t>(columns));
        const auto& category = categoryCards_[index];
        const int categoryX = spacing + column * (width + spacing) - horizontalOffset;
        const int categoryY = rowTops[static_cast<size_t>(row)] - verticalOffset;
        CRect categoryRect(categoryX, categoryY, categoryX + width,
                           categoryY + rowHeights[static_cast<size_t>(row)]);
        theme::FillRoundRect(hdc, categoryRect, 10, theme::SurfaceBg(), theme::Border());

        const int left = categoryRect.left + 16;
        const int right = categoryRect.right - 16;
        drawText(category.name, left, categoryRect.top + 10, std::max(0, right - left - 120), 24,
                 theme::BodyFontBold(), theme::TextPrimary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const std::wstring count = std::format(L"{} {}", category.storages.size(),
                                               category.storages.size() == 1 ? L"storage" : L"storages");
        drawText(count, right - 120, categoryRect.top + 10, 120, 24, theme::SmallFont(), theme::TextSecondary(),
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        if (!category.uncategorized) {
            drawText(L"Right-click to manage drives", left, categoryRect.top + 34, right - left, 18,
                     theme::SmallFont(), theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        int storageY = categoryRect.top + 58;
        if (category.storages.empty()) {
            drawText(category.uncategorized ? L"No storages detected yet." : L"No drives assigned. Right-click to add one.",
                     left, storageY, right - left, 24, theme::SmallFont(), theme::TextSecondary(),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        } else {
            for (const auto& storage : category.storages) {
                const int storageHeight = storageCardHeight(storage);
                CRect storageRect(left, storageY, right, storageY + storageHeight);
                drawStorage(storage, storageRect);
                storageY += storageHeight + 10;
            }
        }
    }
}

void StorageView::showCategoryContextMenu(POINT screenPoint) {
    if (!service_ || !board_.IsWindow()) return;

    POINT boardPoint = screenPoint;
    if (screenPoint.x == -1 && screenPoint.y == -1) {
        boardPoint = {16, 16};
        ::ClientToScreen(board_.m_hWnd, &boardPoint);
        screenPoint = boardPoint;
    } else {
        ::ScreenToClient(board_.m_hWnd, &boardPoint);
    }

    const int width = categoryCardWidth();
    const int spacing = 16;
    SCROLLINFO horizontalScroll{sizeof(SCROLLINFO), SIF_POS};
    SCROLLINFO verticalScroll{sizeof(SCROLLINFO), SIF_POS};
    ::GetScrollInfo(board_.m_hWnd, SB_HORZ, &horizontalScroll);
    ::GetScrollInfo(board_.m_hWnd, SB_VERT, &verticalScroll);
    const int contentX = boardPoint.x + horizontalScroll.nPos;
    const int contentY = boardPoint.y + verticalScroll.nPos;
    if (contentX < spacing || contentY < 112) return;
    const int columns = categoryCards_.size() > 1 ? 2 : 1;
    const int column = (contentX - spacing) / (width + spacing);
    const int withinCard = (contentX - spacing) % (width + spacing);
    if (column < 0 || column >= columns || withinCard >= width) return;

    int row = 0;
    int rowTop = 112;
    while (rowTop < contentY) {
        int rowHeight = 0;
        const size_t rowStart = static_cast<size_t>(row * columns);
        for (int currentColumn = 0;
             currentColumn < columns && rowStart + static_cast<size_t>(currentColumn) < categoryCards_.size();
             ++currentColumn) {
            const auto& rowCategory = categoryCards_[rowStart + static_cast<size_t>(currentColumn)];
            int categoryHeight = 64;
            for (const auto& storage : rowCategory.storages) categoryHeight += storageCardHeight(storage) + 10;
            if (rowCategory.storages.empty()) categoryHeight += 34;
            rowHeight = std::max(rowHeight, categoryHeight);
        }
        if (contentY < rowTop + rowHeight) break;
        rowTop += rowHeight + spacing;
        ++row;
    }
    const size_t index = static_cast<size_t>(row * columns + column);
    if (index >= categoryCards_.size()) return;

    const auto& category = categoryCards_[index];
    if (category.uncategorized) return;

    const auto allStorages = service_->getAllStorages();
    const auto memberIds = service_->getStorageIdsForCategory(category.categoryId);
    const std::unordered_set<std::string> members(memberIds.begin(), memberIds.end());

    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;
    std::vector<std::string> addIds;
    std::vector<std::string> removeIds;
    UINT nextAdd = kAddStorageCommandBase;
    for (const auto& storage : allStorages) {
        if (members.contains(storage.storageId)) continue;
        const UINT command = nextAdd++;
        addIds.push_back(storage.storageId);
        std::wstring label = L"Add " + Utf8ToWide(storage.name.empty() ? storage.rootPath : storage.name);
        ::AppendMenuW(menu, MF_STRING, command, label.c_str());
    }
    if (addIds.empty()) ::AppendMenuW(menu, MF_GRAYED, 0, L"All storages are already in this category");

    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    UINT nextRemove = kRemoveStorageCommandBase;
    for (const auto& storage : allStorages) {
        if (!members.contains(storage.storageId)) continue;
        const UINT command = nextRemove++;
        removeIds.push_back(storage.storageId);
        std::wstring label = L"Remove " + Utf8ToWide(storage.name.empty() ? storage.rootPath : storage.name);
        ::AppendMenuW(menu, MF_STRING, command, label.c_str());
    }
    if (removeIds.empty()) ::AppendMenuW(menu, MF_GRAYED, 0, L"No storages in this category");

    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kDeleteCategoryCommand, L"Delete category");

    const UINT command = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                          screenPoint.x, screenPoint.y, 0, m_hWnd, nullptr);
    ::DestroyMenu(menu);
    if (command >= kAddStorageCommandBase && command < kAddStorageCommandBase + addIds.size()) {
        service_->addStorageToCategory(category.categoryId, addIds[command - kAddStorageCommandBase]);
        refresh();
    } else if (command >= kRemoveStorageCommandBase && command < kRemoveStorageCommandBase + removeIds.size()) {
        service_->removeStorageFromCategory(category.categoryId, removeIds[command - kRemoveStorageCommandBase]);
        refresh();
    } else if (command == kDeleteCategoryCommand) {
        const int answer = ::MessageBoxW(m_hWnd,
                                         L"Delete this category? The storage locations and their game data will remain.",
                                         L"Delete Storage Category", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (answer == IDYES) {
            service_->deleteCategory(category.categoryId);
            refresh();
        }
    }
    return;
}

LRESULT StorageView::OnRescanClick(WORD, WORD, HWND, BOOL&) {
    startRescan();
    return 0;
}

LRESULT StorageView::OnHealthDetailsClick(WORD, WORD, HWND, BOOL&) {
    healthDetailsVisible_ = !healthDetailsVisible_;
    layoutControls();
    refresh();
    return 0;
}

LRESULT StorageView::OnCreateCategoryClick(WORD, WORD, HWND, BOOL&) {
    if (!service_) return 0;
    const std::string name = WideToUtf8(getEditText(categoryNameEdit_));
    if (name.empty()) {
        categoryNameEdit_.SetFocus();
        return 0;
    }

    const auto result = service_->createCategory(name);
    if (result.success) {
        categoryNameEdit_.SetWindowText(L"");
        refresh();
    }
    return 0;
}
