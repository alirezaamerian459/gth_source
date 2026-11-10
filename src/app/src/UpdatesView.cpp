#include "UpdatesView.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cwctype>
#include <fstream>
#include <format>
#include <iterator>
#include <map>
#include <memory>
#include <set>

#include "gth/application/indexing/ContentIds.h"
#include "gth/core/enums/GameVariantStatus.h"
#include "gth/logging/Logging.h"
#include "gth/network/SonyUpdateService.h"
#include "UiMessageOwnership.h"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
constexpr int kMargin = 24;

constexpr int kRowHeight = 28;
constexpr int kMainRowTop = 24;
constexpr int kStatusHeight = 32;
constexpr int kBoardTop = 236;
constexpr int kBoardSummaryHeight = 76;
constexpr int kBoardCardWidth = 274;
constexpr int kBoardCardHeight = 324;
constexpr int kBoardSpacing = 16;

// Once a minute: asks whether the Sony automation is due (see IsSonyAutomationDue).
constexpr UINT_PTR kAutomationTimerId = 2;
constexpr UINT kAutomationTickMs = 60 * 1000;

// Keep the library's update status fresh without hammering Sony. The check is
// read-only and only starts while no other archive job is using the shared slot.
constexpr UINT_PTR kUpdatePollTimerId = 5;
constexpr UINT kUpdatePollTickMs = 15 * 60 * 1000;

// While collecting Sony links: looks for a new Sony address on the clipboard.
constexpr UINT_PTR kCollectTimerId = 3;
constexpr UINT kCollectTickMs = 700;

// Checks every 30 seconds during guided collection whether the automatic clicking has gone quiet -
// see kCollectStallThreshold - and if so, restarts PatchAutoClicker and SonyLinkCopyWatcher, so a
// tab that ended up stuck in either one's own internal "already tried this session" memory (e.g.
// after exhausting its warm-up retries, or a captcha rejection) gets a completely fresh pass at
// being recognized and clicked, instead of being silently left behind for the rest of the session.
constexpr UINT_PTR kCollectWatchdogTimerId = 4;
constexpr UINT kCollectWatchdogTickMs = 30 * 1000;
// How long with no visible progress (no click, no link learned) counts as "actually stalled" and
// worth restarting for - not merely "30 seconds have passed", which could interrupt healthy
// in-progress work (see kCollectWatchdogTimerId's own handler).
constexpr std::chrono::seconds kCollectStallThreshold{30};

// The public list of PS5 title -> Sony version-file links the PS5 scene keeps (about 540 titles, mostly early
// releases). Fetched only when the user asks.
constexpr const char* kPublicLinkListUrl = "https://raw.githubusercontent.com/1jtp8sobiu/ps5-pkg/master/PS5_XML.tsv";

std::wstring formatSize(long long bytes) {
    std::array<wchar_t, 48> buffer{};
    const double size = static_cast<double>(bytes);
    if (bytes >= 1024LL * 1024 * 1024) {
        swprintf_s(buffer.data(), buffer.size(), L"%.2f GB", size / 1024.0 / 1024.0 / 1024.0);
    } else {
        swprintf_s(buffer.data(), buffer.size(), L"%.1f MB", size / 1024.0 / 1024.0);
    }
    return buffer.data();
}

std::filesystem::path desktopDirectory() {
    PWSTR raw = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &raw)) || !raw) return {};
    std::filesystem::path result(raw);
    ::CoTaskMemFree(raw);
    return result;
}

// The list's own text for a game's answer from Sony.
std::wstring updateStateText(const gth::application::UpdateCheckEntry& entry) {
    using enum gth::application::UpdateState;
    const wchar_t* label;
    switch (entry.state) {
        case UpToDate: label = L"Up to date"; break;
        case UpdateAvailable: label = L"Update available"; break;
        case InstalledVersionUnknown: label = L"Version unknown"; break;
        case NoSonyData: label = L"No Sony link"; break;
        case CheckFailed: label = L"Check failed"; break;
        default: label = L""; break;
    }
    std::wstring text = label;
    if (!entry.detail.empty()) text += L" - " + Utf8ToWide(entry.detail);
    return text;
}

COLORREF updateStateColor(gth::application::UpdateState state) {
    using enum gth::application::UpdateState;
    switch (state) {
        case UpdateAvailable: return RGB(220, 148, 12);
        case UpToDate: return theme::Success();
        case CheckFailed: return theme::Danger();
        case InstalledVersionUnknown:
        case NoSonyData: return theme::TextSecondary();
        default: return theme::TextSecondary();
    }
}

std::wstring updateStateLabel(gth::application::UpdateState state) {
    using enum gth::application::UpdateState;
    switch (state) {
        case UpdateAvailable: return L"Update";
        case UpToDate: return L"Current";
        case InstalledVersionUnknown: return L"Unknown";
        case NoSonyData: return L"No link";
        case CheckFailed: return L"Failed";
        default: return L"Ready";
    }
}

std::wstring shortText(std::wstring text, size_t maxLength) {
    if (text.size() <= maxLength) return text;
    if (maxLength < 4) return text.substr(0, maxLength);
    text.resize(maxLength - 3);
    text += L"...";
    return text;
}

void drawBitmapCover(HDC hdc, HBITMAP bitmap, const RECT& target) {
    if (!bitmap) return;
    BITMAP source{};
    if (::GetObject(bitmap, sizeof(source), &source) != sizeof(source)) return;

    HDC sourceDc = ::CreateCompatibleDC(hdc);
    if (!sourceDc) return;
    HGDIOBJ oldBitmap = ::SelectObject(sourceDc, bitmap);
    const int oldMode = ::SetStretchBltMode(hdc, HALFTONE);
    ::SetBrushOrgEx(hdc, 0, 0, nullptr);
    ::StretchBlt(hdc, target.left, target.top, target.right - target.left, target.bottom - target.top,
                 sourceDc, 0, 0, source.bmWidth, source.bmHeight, SRCCOPY);
    ::SetStretchBltMode(hdc, oldMode);
    ::SelectObject(sourceDc, oldBitmap);
    ::DeleteDC(sourceDc);
}

void drawBadge(HDC hdc, const std::wstring& text, const RECT& bounds, COLORREF fill, COLORREF textColor,
               HFONT font) {
    theme::FillRoundRect(hdc, bounds, 8, fill, fill);
    HGDIOBJ oldFont = ::SelectObject(hdc, font);
    const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(hdc, textColor);
    RECT textRect = bounds;
    textRect.left += 8;
    textRect.right -= 8;
    ::DrawTextW(hdc, text.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldMode);
    ::SelectObject(hdc, oldFont);
}

// The busy cursor for the length of one UI-thread operation.
struct WaitCursor {
    HCURSOR previous = ::SetCursor(::LoadCursor(nullptr, IDC_WAIT));
    ~WaitCursor() { ::SetCursor(previous); }
};

// A list the user saved from a spreadsheet or notepad can be UTF-8 (with or
// without a byte-order mark) or UTF-16; the version parsers want UTF-8.
std::string readListFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring wide;
        wide.reserve((bytes.size() - 2) / 2);
        for (size_t offset = 2; offset + 1 < bytes.size(); offset += 2) {
            const auto lowByte = static_cast<unsigned char>(bytes[offset]);
            const auto highByte = static_cast<unsigned char>(bytes[offset + 1]);
            wide.push_back(static_cast<wchar_t>(lowByte | (highByte << 8)));
        }
        return WideToUtf8(wide);
    }
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    return bytes;
}

// The clipboard's text as UTF-8 (empty when it holds none), or nothing at all when the clipboard couldn't be
// opened - another program has it - which is worth trying again a moment later.
std::optional<std::string> readClipboardText(HWND owner) {
    if (!::OpenClipboard(owner)) return std::nullopt;
    std::string text;
    if (HANDLE data = ::GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* wide = static_cast<const wchar_t*>(::GlobalLock(data))) {
            text = WideToUtf8(wide);
            ::GlobalUnlock(data);
        }
    }
    ::CloseClipboard();
    return text;
}
}  // namespace

int UpdatesBoardWindow::maxVertical() const {
    CRect client;
    GetClientRect(&client);
    return std::max(0, contentHeight_ - client.Height());
}

void UpdatesBoardWindow::updateScrollBar() {
    if (!IsWindow()) return;
    const int maximum = std::max(0, contentHeight_ - 1);
    const int page = std::max(1, static_cast<int>([&] {
        CRect client;
        GetClientRect(&client);
        return client.Height();
    }()));
    verticalOffset_ = std::clamp(verticalOffset_, 0, maxVertical());

    SCROLLINFO info{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMin = 0;
    info.nMax = maximum;
    info.nPage = static_cast<UINT>(page);
    info.nPos = verticalOffset_;
    ::SetScrollInfo(m_hWnd, SB_VERT, &info, TRUE);
    ::ShowScrollBar(m_hWnd, SB_VERT, maxVertical() > 0 ? TRUE : FALSE);
}

void UpdatesBoardWindow::setContentExtent(int width, int height) {
    contentWidth_ = std::max(0, width);
    contentHeight_ = std::max(0, height);
    updateScrollBar();
    Invalidate(FALSE);
}

void UpdatesBoardWindow::scrollTo(int position) {
    const int clamped = std::clamp(position, 0, maxVertical());
    if (clamped == verticalOffset_) return;
    verticalOffset_ = clamped;
    updateScrollBar();
    Invalidate(FALSE);
}

LRESULT UpdatesBoardWindow::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
    PAINTSTRUCT paint{};
    HDC hdc = ::BeginPaint(m_hWnd, &paint);
    if (owner_) {
        RECT client{};
        ::GetClientRect(m_hWnd, &client);
        owner_->paintGameBoard(hdc, client, verticalOffset_);
    } else {
        ::FillRect(hdc, &paint.rcPaint, theme::ContentBgBrush());
    }
    ::EndPaint(m_hWnd, &paint);
    return 0;
}

LRESULT UpdatesBoardWindow::OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) {
    HDC hdc = std::bit_cast<HDC>(wParam);
    RECT client{};
    ::GetClientRect(m_hWnd, &client);
    ::FillRect(hdc, &client, theme::ContentBgBrush());
    return 1;
}

LRESULT UpdatesBoardWindow::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
    updateScrollBar();
    Invalidate(FALSE);
    return 0;
}

LRESULT UpdatesBoardWindow::OnVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
    SCROLLINFO info{sizeof(SCROLLINFO), SIF_ALL};
    ::GetScrollInfo(m_hWnd, SB_VERT, &info);
    int next = verticalOffset_;
    switch (LOWORD(wParam)) {
        case SB_TOP: next = 0; break;
        case SB_BOTTOM: next = maxVertical(); break;
        case SB_LINEUP: next -= 36; break;
        case SB_LINEDOWN: next += 36; break;
        case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: next = info.nTrackPos; break;
        default: return 0;
    }
    scrollTo(next);
    return 0;
}

LRESULT UpdatesBoardWindow::OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL&) {
    const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    scrollTo(verticalOffset_ - (delta / WHEEL_DELTA) * 72);
    return 0;
}

LRESULT UpdatesBoardWindow::OnContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
    if (!owner_) return 0;
    POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
    if (point.x == -1 && point.y == -1) {
        RECT rect{};
        ::GetWindowRect(m_hWnd, &rect);
        point = {rect.left + 16, rect.top + 16};
    }
    owner_->showToolsMenu(point);
    return 0;
}

LRESULT UpdatesBoardWindow::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
    SetFocus();
    POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
    if (owner_) owner_->onGameBoardClick(owner_->hitTestGameBoard(point, verticalOffset_), false);
    return 0;
}

LRESULT UpdatesBoardWindow::OnLButtonDoubleClick(UINT, WPARAM, LPARAM lParam, BOOL&) {
    SetFocus();
    POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
    if (owner_) owner_->onGameBoardClick(owner_->hitTestGameBoard(point, verticalOffset_), true);
    return 0;
}

LRESULT UpdatesBoardWindow::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (owner_) owner_->onGameBoardKeyDown(static_cast<UINT>(wParam));
    return 0;
}

LRESULT UpdatesBoardWindow::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
    Invalidate(FALSE);
    return 0;
}

LRESULT UpdatesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CRect titleRect(kMargin, kMainRowTop - 2, kMargin + 260, kMainRowTop + 36);
    titleLabel_.Create(m_hWnd, titleRect, L"Game Updates", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    const auto makeButton = [&](ModernButton& button, const wchar_t* text, UINT id,
                                ModernButton::Kind kind, bool visible = true) {
        CRect rect(0, 0, 120, kRowHeight);
        button.Create(m_hWnd, rect, text,
                      WS_CHILD | (visible ? WS_VISIBLE : 0) | WS_TABSTOP | BS_PUSHBUTTON, 0, id);
        button.SetFont(theme::BodyFont());
        button.setKind(kind);
    };
    makeButton(downloadAllButton_, L"Update all games", IDC_UPDATES_DOWNLOAD_ALL_BUTTON,
               ModernButton::Kind::Primary);
    makeButton(checkUpdatesButton_, L"Check for updates", IDC_UPDATES_CHECK_SONY_BUTTON,
               ModernButton::Kind::Secondary);
    makeButton(cancelSyncButton_, L"Cancel", IDC_UPDATES_CANCEL_SYNC_BUTTON, ModernButton::Kind::Danger, false);
    makeButton(automationPanelButton_, L"Automation settings  \x25B8", IDC_UPDATES_AUTOMATION_PANEL_BUTTON,
               ModernButton::Kind::Subtle);
    makeButton(selectedGamePanelButton_, L"Selected game  \x25B8",
               IDC_UPDATES_SELECTED_GAME_PANEL_BUTTON, ModernButton::Kind::Subtle);
    CRect controlRect(0, 0, 500, 22);
    automationSummaryLabel_.Create(m_hWnd, controlRect, L"", WS_CHILD | WS_VISIBLE);
    automationSummaryLabel_.SetFont(theme::BodyFont());
    controlRect.SetRect(0, 0, 600, 22);
    selectedGameSummaryLabel_.Create(m_hWnd, controlRect, L"", WS_CHILD | WS_VISIBLE);
    selectedGameSummaryLabel_.SetFont(theme::BodyFont());

    const auto makeCheckBox = [&](ModernCheckBox& box, const wchar_t* text, UINT id) {
        CRect rect(0, 0, 260, 24);
        box.Create(m_hWnd, rect, text, WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 0, id);
        box.SetFont(theme::BodyFont());
    };
    makeCheckBox(autoCheckBox_, L"Check for updates", IDC_UPDATES_AUTO_CHECK_CHECKBOX);
    makeCheckBox(autoMetadataBox_, L"Add missing metadata", IDC_UPDATES_AUTO_METADATA_CHECKBOX);
    makeCheckBox(autoUpdatesBox_, L"Download updates (may use many GB)", IDC_UPDATES_AUTO_UPDATES_CHECKBOX);
    makeCheckBox(autoScanBox_, L"Scan for new games first", IDC_UPDATES_AUTO_SCAN_CHECKBOX);
    applyAutomationSettingsToControls();

    controlRect.SetRect(0, 0, 100, 22);
    versionCaption_.Create(m_hWnd, controlRect, L"Installed version", WS_CHILD);
    versionCaption_.SetFont(theme::BodyFont());
    controlRect.SetRect(0, 0, 110, 24);
    versionEdit_.Create(m_hWnd, controlRect, L"", WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0,
                        IDC_UPDATES_VERSION_EDIT);
    versionEdit_.SetFont(theme::BodyFont());
    versionEdit_.SendMessage(EM_SETCUEBANNER, FALSE,
                             std::bit_cast<LPARAM>(static_cast<const wchar_t*>(L"01.300.000")));
    makeButton(setVersionButton_, L"Set version", IDC_UPDATES_SET_VERSION_BUTTON, ModernButton::Kind::Secondary,
               false);
    makeButton(resetVersionButton_, L"Reset", IDC_UPDATES_RESET_VERSION_BUTTON, ModernButton::Kind::Subtle,
               false);
    makeButton(syncSelectedButton_, L"Sync with Sony", IDC_UPDATES_SYNC_SELECTED_BUTTON,
               ModernButton::Kind::Secondary, false);
    makeButton(skipGameButton_, L"Skip game", IDC_UPDATES_SKIP_GAME_BUTTON,
               ModernButton::Kind::Secondary, false);
    skipGameButton_.ShowWindow(SW_HIDE);  // only while collecting links

    // What the last run did is worth seeing the moment the tab opens.
    const std::string lastRun = gth::application::LastSonyRunSummary(reportPath_);
    const std::wstring opening = lastRun.empty()
                                     ? std::wstring(L"Ready")
                                     : std::format(L"Last run: {}", shortText(Utf8ToWide(lastRun), 96));
    controlRect.SetRect(0, 0, 900, kStatusHeight);
    statusLabel_.Create(m_hWnd, controlRect, opening.c_str(), WS_CHILD | WS_VISIBLE);
    statusLabel_.SetFont(theme::BodyFont());

    CRect filterRect(0, 0, 520, 28);
    filterEdit_.Create(m_hWnd, filterRect, L"",
                       WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0,
                       IDC_UPDATES_FILTER_EDIT);
    filterEdit_.SetFont(theme::BodyFont());
    filterEdit_.SendMessage(EM_SETCUEBANNER, FALSE,
                            std::bit_cast<LPARAM>(static_cast<const wchar_t*>(L"Filter games...")));

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(0, 0, 1, 1);
    // LVS_ICON: a poster grid (box art on top, its label wrapped beneath), matching the Games
    // tab and the Sony storefronts both are modeled on - see composeLabel() for what the label's
    // three lines show (title, version/size, and this game's live status while a job is running).
    listView_.Create(m_hWnd, listRect, L"",
                      WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_ICON | LVS_AUTOARRANGE | LVS_SINGLESEL, 0,
                      IDC_UPDATES_LISTVIEW);
    listView_.SetFont(theme::BodyFont());
    theme::ApplyListViewTheme(listView_);
    listView_.SetIconSpacing(PosterCache::kTileWidth + 24, PosterCache::kTileHeight + 48);
    listView_.ShowWindow(SW_HIDE);

    board_.init(*this);
    CRect boardRect(kMargin, kBoardTop, clientRect.right - kMargin, clientRect.bottom - kMargin);
    board_.Create(m_hWnd, boardRect, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL, WS_EX_CLIENTEDGE,
                  IDC_UPDATES_BOARD);
    if (posterCache_) posterCache_->subscribe(m_hWnd);

    layoutControls();
    refresh();
    setBusy(false);  // sets every button's enabled state, including Cancel (off until a sync runs)
    SetTimer(kAutomationTimerId, kAutomationTickMs);
    SetTimer(kUpdatePollTimerId, kUpdatePollTickMs);
    return 0;
}

LRESULT UpdatesView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    KillTimer(kAutomationTimerId);
    KillTimer(kUpdatePollTimerId);
    KillTimer(kCollectTimerId);
    KillTimer(kCollectWatchdogTimerId);
    syncCancel_.store(true);  // a sync in flight stops at its next check instead of holding shutdown up
    linksCancel_.store(true);
    patchAutoClicker_.Stop();
    // Joining here briefly delays shutdown rather than risking a detached
    // thread touching this object's members after it's destroyed.
    if (updateThread_.joinable()) updateThread_.join();
    if (syncThread_.joinable()) syncThread_.join();
    if (linksThread_.joinable()) linksThread_.join();
    MSG message{};
    while (::PeekMessageW(&message, m_hWnd, kUpdateCheckDoneMessage, kPatchAutoClickMessage, PM_REMOVE)) {
        switch (message.message) {
            case kUpdateCheckDoneMessage:
                gth::app::detail::takeOwnedMessage<gth::application::UpdateCheckSummary>(message.lParam);
                break;
            case kSyncProgressMessage:
            case kLinksProgressMessage:
            case kPatchAutoClickMessage:
                gth::app::detail::takeOwnedMessage<std::wstring>(message.lParam);
                break;
            case kSyncDoneMessage: gth::app::detail::takeOwnedMessage<SyncJobOutcome>(message.lParam); break;
            case kLinksDoneMessage: gth::app::detail::takeOwnedMessage<LinksJobOutcome>(message.lParam); break;
            case kSyncRowMessage: gth::app::detail::takeOwnedMessage<RowUpdate>(message.lParam); break;
            default: break;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT UpdatesView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    layoutControls();
    return 0;
}

void UpdatesView::layoutControls() {
    if (!board_.IsWindow()) return;
    CRect client;
    GetClientRect(&client);
    const int width = std::max(500, static_cast<int>(client.right) - 2 * kMargin);
    auto show = [](auto& control, bool visible) {
        if (control.IsWindow()) control.ShowWindow(visible ? SW_SHOW : SW_HIDE);
    };
    auto move = [](auto& control, int x, int y, int w, int h) {
        if (control.IsWindow()) control.MoveWindow(x, y, std::max(0, w), h, FALSE);
    };

    int x = kMargin + 180;
    move(downloadAllButton_, x, kMainRowTop, 158, kRowHeight); x += 166;
    move(checkUpdatesButton_, x, kMainRowTop, 150, kRowHeight); x += 158;
    const bool cancelVisible = syncInProgress_.load() || linksInProgress_.load() || collecting_;
    show(cancelSyncButton_, cancelVisible);
    if (cancelVisible) { move(cancelSyncButton_, x, kMainRowTop, 72, kRowHeight); x += 80; }

    automationPanelButton_.SetWindowText(automationExpanded_ ? L"Automation settings  \x25BE"
                                                            : L"Automation settings  \x25B8");
    selectedGamePanelButton_.SetWindowText(selectedGameToolsExpanded_ ? L"Selected game  \x25BE"
                                                                       : L"Selected game  \x25B8");
    move(automationPanelButton_, kMargin, 66, 190, kRowHeight);
    move(automationSummaryLabel_, kMargin + 202, 70, std::max(160, width - 202), 22);
    move(selectedGamePanelButton_, kMargin, 102, 190, kRowHeight);
    move(selectedGameSummaryLabel_, kMargin + 202, 106, std::max(160, width - 202), 22);

    int contentTop = 136;
    show(autoCheckBox_, automationExpanded_); show(autoMetadataBox_, automationExpanded_);
    show(autoUpdatesBox_, automationExpanded_); show(autoScanBox_, automationExpanded_);
    if (automationExpanded_) {
        const int half = std::max(220, (width - 16) / 2);
        move(autoCheckBox_, kMargin, contentTop, half, 24);
        move(autoMetadataBox_, kMargin + half + 16, contentTop, half, 24);
        move(autoUpdatesBox_, kMargin, contentTop + 30, half, 24);
        move(autoScanBox_, kMargin + half + 16, contentTop + 30, half, 24);
        contentTop += 62;
    }

    const bool showSelectedTools = selectedGameToolsExpanded_;
    show(versionCaption_, showSelectedTools); show(versionEdit_, showSelectedTools);
    show(setVersionButton_, showSelectedTools); show(resetVersionButton_, showSelectedTools);
    show(syncSelectedButton_, showSelectedTools); show(skipGameButton_, showSelectedTools && collecting_);
    if (showSelectedTools) {
        int tx = kMargin;
        move(versionCaption_, tx, contentTop + 5, 116, 22); tx += 124;
        move(versionEdit_, tx, contentTop, 112, 26); tx += 120;
        move(setVersionButton_, tx, contentTop - 1, 92, kRowHeight); tx += 100;
        move(resetVersionButton_, tx, contentTop - 1, 70, kRowHeight); tx += 78;
        move(syncSelectedButton_, tx, contentTop - 1, 130, kRowHeight); tx += 138;
        move(skipGameButton_, tx, contentTop - 1, 95, kRowHeight);
        contentTop += 36;
    }

    move(filterEdit_, kMargin, contentTop, std::min(520, width), 28);
    contentTop += 36;

    const int statusTop = contentTop;
    const int statusHeight = std::min(kStatusHeight, std::max(30, static_cast<int>(client.bottom) / 8));
    move(statusLabel_, kMargin, statusTop, width, statusHeight);
    const int boardTop = statusTop + statusHeight + 10;
    move(board_, kMargin, boardTop, width,
         std::max(160, static_cast<int>(client.bottom) - boardTop - kMargin));
    listView_.ShowWindow(SW_HIDE);
    updateBoardExtent();
}

LRESULT UpdatesView::OnAutomationPanelClick(WORD, WORD, HWND, BOOL&) {
    automationExpanded_ = !automationExpanded_;
    layoutControls();
    return 0;
}

LRESULT UpdatesView::OnSelectedGamePanelClick(WORD, WORD, HWND, BOOL&) {
    selectedGameToolsExpanded_ = !selectedGameToolsExpanded_;
    layoutControls();
    return 0;
}

LRESULT UpdatesView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorStatic(wParam); }

LRESULT UpdatesView::OnCtlColorEdit(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorEdit(wParam); }

LRESULT UpdatesView::OnFilterChanged(WORD, WORD, HWND, BOOL&) {
    refresh();
    return 0;
}

bool UpdatesView::anythingRunning() const { return shared_ != nullptr && shared_->jobRunning(); }

void UpdatesView::setBusy(bool busy) {
    // A collection session keeps the other buttons off between its imports; only Skip Game and Cancel stay live.
    const BOOL enabled = (busy || collecting_) ? FALSE : TRUE;
    checkUpdatesButton_.EnableWindow(enabled);
    downloadAllButton_.EnableWindow(enabled);
    cancelSyncButton_.EnableWindow(syncInProgress_.load() || linksInProgress_.load() || collecting_ ? TRUE : FALSE);
    if (skipGameButton_.IsWindow()) skipGameButton_.EnableWindow(collecting_ ? TRUE : FALSE);
    updateVersionControls();
    layoutControls();
}

void UpdatesView::updateVersionControls() {
    if (!setVersionButton_.IsWindow()) return;
    const auto* entry = selectedEntry();
    const BOOL enabled = (entry != nullptr && !anythingRunning() && !collecting_) ? TRUE : FALSE;
    setVersionButton_.EnableWindow(enabled);
    resetVersionButton_.EnableWindow(enabled);
    syncSelectedButton_.EnableWindow(enabled);
    versionEdit_.EnableWindow(entry != nullptr ? TRUE : FALSE);
    versionEdit_.SetWindowText(entry ? Utf8ToWide(entry->currentVersion).c_str() : L"");
    selectedGameSummaryLabel_.SetWindowText(entry ? shortText(Utf8ToWide(entry->title), 48).c_str()
                                                    : L"Select a card");
}

std::wstring UpdatesView::filterText() const {
    if (!filterEdit_.IsWindow()) return {};
    const int length = filterEdit_.GetWindowTextLength();
    if (length <= 0) return {};
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    filterEdit_.GetWindowText(text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return std::towlower(c); });
    return text;
}

bool UpdatesView::filterMatches(const gth::application::GameLibraryEntry& entry) const {
    const std::wstring needle = filterText();
    if (needle.empty()) return true;
    std::wstring haystack = Utf8ToWide(entry.title) + L" " + Utf8ToWide(entry.titleId) + L" " +
                            Utf8ToWide(entry.platform) + L" " + Utf8ToWide(entry.region) + L" " +
                            Utf8ToWide(entry.currentVersion);
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](wchar_t c) { return std::towlower(c); });
    return haystack.find(needle) != std::wstring::npos;
}

int UpdatesView::boardCardWidth() const { return kBoardCardWidth; }

int UpdatesView::boardCardHeight() const { return kBoardCardHeight; }

void UpdatesView::updateBoardExtent() {
    if (!board_.IsWindow()) return;
    CRect client;
    board_.GetClientRect(&client);
    const int columns = std::max(1, (client.Width() + kBoardSpacing) / (boardCardWidth() + kBoardSpacing));
    const int rows = entries_.empty() ? 0 : static_cast<int>((entries_.size() + static_cast<size_t>(columns) - 1) /
                                                               static_cast<size_t>(columns));
    const int contentHeight = kBoardSummaryHeight + kBoardSpacing +
                              rows * (boardCardHeight() + kBoardSpacing) + kBoardSpacing;
    board_.setContentExtent(client.Width(), contentHeight);
}

void UpdatesView::paintGameBoard(HDC hdc, const RECT& clientRect, int verticalOffset) {
    ::FillRect(hdc, &clientRect, theme::ContentBgBrush());
    const int width = boardCardWidth();
    const int clientWidth = static_cast<int>(clientRect.right - clientRect.left);
    const int columns = std::max(1, (clientWidth + kBoardSpacing) / (width + kBoardSpacing));

    auto drawText = [&](const std::wstring& text, int x, int y, int w, int h, HFONT font, COLORREF color,
                        UINT format) {
        RECT rect{x, y, x + std::max(0, w), y + h};
        HGDIOBJ oldFont = ::SelectObject(hdc, font);
        const int oldMode = ::SetBkMode(hdc, TRANSPARENT);
        const COLORREF oldColor = ::SetTextColor(hdc, color);
        ::DrawTextW(hdc, text.c_str(), -1, &rect, format | DT_NOPREFIX | DT_END_ELLIPSIS);
        ::SetTextColor(hdc, oldColor);
        ::SetBkMode(hdc, oldMode);
        ::SelectObject(hdc, oldFont);
    };

    int updates = 0;
    int current = 0;
    int unknown = 0;
    int failed = 0;
    for (const auto& entry : entries_) {
        const auto found = shared_->updateByVariant.find(entry.gameVariantId);
        if (found == shared_->updateByVariant.end()) {
            ++unknown;
            continue;
        }
        using enum gth::application::UpdateState;
        switch (found->second.state) {
            case UpdateAvailable: ++updates; break;
            case UpToDate: ++current; break;
            case CheckFailed: ++failed; break;
            default: ++unknown; break;
        }
    }

    CRect summary(kBoardSpacing, 10 - verticalOffset,
                  clientRect.right - clientRect.left - kBoardSpacing, kBoardSummaryHeight - verticalOffset);
    theme::FillRoundRect(hdc, summary, 10, theme::SurfaceBg(), theme::Border());
    drawText(L"LIBRARY", summary.left + 16, summary.top + 10, 120, 18, theme::SmallFont(),
             theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawText(std::format(L"{} games", entries_.size()), summary.left + 16, summary.top + 30, 160, 26,
             theme::BodyFontBold(), theme::TextPrimary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawText(std::format(L"{} updates  ·  {} current  ·  {} queued", updates, current,
                         shared_->queuedDownloads.size() + shared_->liveDownloads.size()),
             summary.left + 230, summary.top + 20,
             std::max(180, static_cast<int>(summary.Width()) - 250), 22, theme::SmallFont(),
             updates > 0 ? updateStateColor(gth::application::UpdateState::UpdateAvailable) : theme::TextSecondary(),
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawText(failed > 0 ? std::format(L"{} failed", failed) : L"",
             summary.right - 310, summary.top + 46, 294, 18, theme::SmallFont(),
             failed > 0 ? theme::Danger() : theme::Success(), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    for (size_t index = 0; index < entries_.size(); ++index) {
        const int row = static_cast<int>(index / static_cast<size_t>(columns));
        const int column = static_cast<int>(index % static_cast<size_t>(columns));
        const int x = kBoardSpacing + column * (width + kBoardSpacing);
        const int y = kBoardSummaryHeight + kBoardSpacing + row * (boardCardHeight() + kBoardSpacing) - verticalOffset;
        CRect card(x, y, x + width, y + boardCardHeight());
        const bool selected = listView_.GetSelectedIndex() == static_cast<int>(index);
        theme::FillRoundRect(hdc, card, 10, theme::SurfaceBg(), selected ? theme::Accent() : theme::Border());

        CRect art(card.left, card.top, card.right, card.top + 132);
        if (index < posterTiles_.size()) {
            drawBitmapCover(hdc, posterTiles_[index], art);
        } else {
            HBRUSH placeholder = ::CreateSolidBrush(theme::HoverBg());
            ::FillRect(hdc, &art, placeholder);
            ::DeleteObject(placeholder);
        }

        const auto& entry = entries_[index];
        const auto found = shared_->updateByVariant.find(entry.gameVariantId);
        const auto state = found == shared_->updateByVariant.end()
                               ? gth::application::UpdateState::InstalledVersionUnknown
                               : found->second.state;
        const std::wstring stateLabel = updateStateLabel(state);
        drawBadge(hdc, Utf8ToWide(entry.platform.empty() ? "PS" : entry.platform),
                  CRect(art.left + 10, art.top + 10, art.left + 68, art.top + 32), theme::HoverBg(),
                  theme::TextPrimary(), theme::SmallFont());
        drawBadge(hdc, shortText(stateLabel, 23),
                  CRect(art.right - 126, art.top + 10, art.right - 10, art.top + 32),
                  updateStateColor(state), theme::TextOnAccent(), theme::SmallFont());

        const int left = card.left + 14;
        const int inner = card.Width() - 28;
        drawText(shortText(Utf8ToWide(entry.title), 34), left, card.top + 144, inner, 38, theme::BodyFontBold(),
                 theme::TextPrimary(), DT_LEFT | DT_WORDBREAK);
        drawText(L"ID       " + shortText(Utf8ToWide(entry.titleId), 18), left, card.top + 188, inner, 16,
                 theme::SmallFont(), theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const std::wstring version = entry.currentVersion.empty() ? L"Not detected" : Utf8ToWide(entry.currentVersion);
        drawText(L"Version  " + shortText(version, 18), left, card.top + 210, inner, 16, theme::SmallFont(),
                 theme::TextSecondary(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const COLORREF actionColor = state == gth::application::UpdateState::UpdateAvailable
                                         ? RGB(208, 137, 0)
                                         : theme::HoverBg();
        CRect action(card.left + 12, card.bottom - 44, card.right - 12, card.bottom - 12);
        theme::FillRoundRect(hdc, action, 7, actionColor, actionColor);
        const std::wstring actionLabel = state == gth::application::UpdateState::UpdateAvailable
                                             ? L"Update now"
                                             : (state == gth::application::UpdateState::UpToDate ? L"Verify files"
                                                                                                  : L"View details");
        drawText(actionLabel, action.left, action.top, action.Width(), action.Height(), theme::BodyFontBold(),
                 state == gth::application::UpdateState::UpdateAvailable ? RGB(32, 24, 8) : theme::TextPrimary(),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (GetFocus() == board_.m_hWnd) {
        RECT focusRect = clientRect;
        ::InflateRect(&focusRect, -2, -2);
        HBRUSH brush = ::CreateSolidBrush(theme::Accent());
        ::FrameRect(hdc, &focusRect, brush);
        ::DeleteObject(brush);
    }
}

int UpdatesView::hitTestGameBoard(POINT point, int verticalOffset) const {
    if (point.x < kBoardSpacing) return -1;
    CRect client;
    board_.GetClientRect(&client);
    const int width = boardCardWidth();
    const int columns = std::max(1, (client.Width() + kBoardSpacing) / (width + kBoardSpacing));
    const int column = (point.x - kBoardSpacing) / (width + kBoardSpacing);
    const int withinX = (point.x - kBoardSpacing) % (width + kBoardSpacing);
    if (column < 0 || column >= columns || withinX >= width) return -1;
    const int contentY = point.y + verticalOffset;
    if (contentY < kBoardSummaryHeight + kBoardSpacing) return -1;
    const int row = (contentY - kBoardSummaryHeight - kBoardSpacing) / (boardCardHeight() + kBoardSpacing);
    const int withinY = (contentY - kBoardSummaryHeight - kBoardSpacing) % (boardCardHeight() + kBoardSpacing);
    if (withinY >= boardCardHeight()) return -1;
    const size_t index = static_cast<size_t>(row * columns + column);
    return index < entries_.size() ? static_cast<int>(index) : -1;
}

void UpdatesView::onGameBoardClick(int index, bool activate) {
    if (index < 0 || static_cast<size_t>(index) >= entries_.size()) return;
    selectVariant(entries_[static_cast<size_t>(index)].gameVariantId);
    updateVersionControls();
    board_.refresh();
    if (activate) {
        BOOL handled = TRUE;
        OnSyncSelectedClick(0, IDC_UPDATES_SYNC_SELECTED_BUTTON, nullptr, handled);
    }
}

void UpdatesView::onGameBoardKeyDown(UINT key) {
    int selected = listView_.GetSelectedIndex();
    if (selected < 0 && !entries_.empty()) selected = 0;
    if (selected < 0) return;
    CRect client;
    board_.GetClientRect(&client);
    const int columns = std::max(1, (client.Width() + kBoardSpacing) / (boardCardWidth() + kBoardSpacing));
    int next = selected;
    switch (key) {
        case VK_LEFT: next = selected - 1; break;
        case VK_RIGHT: next = selected + 1; break;
        case VK_UP: next = selected - columns; break;
        case VK_DOWN: next = selected + columns; break;
        case VK_HOME: next = 0; break;
        case VK_END: next = static_cast<int>(entries_.size()) - 1; break;
        case VK_RETURN: onGameBoardClick(selected, true); return;
        default: return;
    }
    if (next >= 0 && static_cast<size_t>(next) < entries_.size()) onGameBoardClick(next, false);
}

void UpdatesView::refresh() {
    if (!library_ || !board_.IsWindow() || !listView_.IsWindow()) return;

    // Keep the same game selected across a refresh (setting a version
    // refreshes the list, and losing your place every time would be maddening).
    std::string keepSelected;
    if (const auto* selected = selectedEntry()) keepSelected = selected->gameVariantId;

    theme::ApplyListViewTheme(listView_);
    listView_.SetRedraw(FALSE);  // a library of thousands would repaint after every row otherwise
    listView_.DeleteAllItems();
    rowsByTitle_.clear();

    // Only games whose files are there: a game that has gone missing can't be updated. The board's
    // filter is presentation-only and never changes the library or update service data.
    entries_.clear();
    for (auto& entry : library_->getAllEntries()) {
        if (entry.variantStatus == gth::core::GameVariantStatus::Active && filterMatches(entry))
            entries_.push_back(std::move(entry));
    }
    posterTiles_.clear();
    posterTiles_.reserve(entries_.size());

    // Rebuilt from scratch every refresh - see GamesView::refresh()'s identical comment on why
    // that's cheap even for a large library.
    if (posterImageList_.IsNull()) {
        posterImageList_.Create(PosterCache::kTileWidth, PosterCache::kTileHeight, ILC_COLOR32, 0,
                                 static_cast<int>(entries_.size()) + 1);
    } else {
        posterImageList_.RemoveAll();
    }
    listView_.SetImageList(posterImageList_, LVSIL_NORMAL);

    int row = 0;
    for (const auto& entry : entries_) {
        if (!entry.titleId.empty()) rowsByTitle_[entry.titleId].push_back(row);

        const HBITMAP poster = posterCache_
                                   ? posterCache_->posterFor(entry.titleId, entry.title, entry.platform,
                                                             posterService_, entry.gameVariantId)
                                   : nullptr;
        posterTiles_.push_back(poster);
        const int imageIndex = poster ? posterImageList_.Add(poster) : -1;
        listView_.InsertItem(row, composeLabel(entry).c_str(), imageIndex);
        row++;
    }

    listView_.SetRedraw(TRUE);
    listView_.Invalidate();

    if (!keepSelected.empty()) selectVariant(keepSelected);
    updateVersionControls();
    setBusy(anythingRunning());  // a job started on the Games tab keeps these buttons off too
    updateBoardExtent();
    board_.refresh();
}

std::wstring UpdatesView::composeLabel(const gth::application::GameLibraryEntry& entry) const {
    // A poster tile's label only has room for two short lines (title, then this one) - not the
    // report view's five columns, and not liveRows_'s own full sentences either (those still go
    // to statusLabel_ in full, unchanged - see OnPatchAutoClick/OnSyncRow). A running job's own
    // words win over the update-check verdict, same priority order the old "Update" column had.
    if (const auto live = liveRows_.find(entry.titleId); live != liveRows_.end()) {
        return std::format(L"{}\r\n{}", Utf8ToWide(entry.title), live->second);
    }

    const auto update = shared_->updateByVariant.find(entry.gameVariantId);
    std::wstring line2;
    if (update != shared_->updateByVariant.end()) {
        line2 = updateStateText(update->second);
    } else if (entry.platform == "PS4" || entry.platform == "PS5") {
        line2 = L"Not checked yet";
    } else {
        line2 = L"-";
    }

    return std::format(L"{}\r\n{}", Utf8ToWide(entry.title), line2);
}

LRESULT UpdatesView::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (wParam == kCollectTimerId) {
        collectTick();
        return 0;
    }
    if (wParam == kCollectWatchdogTimerId) {
        // Only restart once nothing has visibly happened (a click, or a link learned) for a while -
        // not on every tick regardless. An unconditional restart on a short timer would otherwise
        // interrupt perfectly healthy in-progress work just because time passed - including,
        // concretely, PatchAutoClicker's own few-second wait for a just-clicked patch's SC link to
        // appear, which is exactly the kind of interruption that used to lose a game's link.
        if (collecting_ &&
            std::chrono::steady_clock::now() - lastAutoclickProgressAt_ >= kCollectStallThreshold) {
            // A game PatchAutoClicker already permanently gave up on (its own per-title cap - see
            // failureCounts_) shows "Needs you" here the moment that happens, and stays blacklisted
            // from further auto-clicks for the rest of the session regardless of how many times the
            // watchers are restarted - restarting can't undo that. Without this check, a session
            // that's otherwise fully stuck on exactly this one game (no other tab open to make
            // progress on instead, since only one is ever open at a time) would restart every
            // kCollectWatchdogTickMs forever, discover nothing changed, and repeat - the "Still
            // working..." message looping with no real activity behind it. The person's own Skip
            // Game (or solving it by hand) is what actually moves this forward now, so that's
            // treated as the progress being waited for instead.
            if (const auto stuck = liveRows_.find(collectCurrent_);
                stuck != liveRows_.end() && stuck->second == L"Needs you") {
                lastAutoclickProgressAt_ = std::chrono::steady_clock::now();
                return 0;
            }

            sonyLinkCopyWatcher_.Stop();
            patchAutoClicker_.Stop();
            sonyLinkCopyWatcher_.Start(m_hWnd, kSonyLinkCopiedMessage, sonyLinksPath_.parent_path() / "sony-sc-links-copied.txt");
            patchAutoClicker_.Start(m_hWnd, kPatchAutoClickMessage);
            lastAutoclickProgressAt_ = std::chrono::steady_clock::now();  // give the restart a fresh window before trying again
            collectNext(L"Still working - restarted the automatic clicking, nothing had happened for a while. ");
            GTH_LOG_INFO("Guided collection watchdog: restarted the automatic clicking (no progress for {}s)",
                         std::chrono::duration_cast<std::chrono::seconds>(kCollectStallThreshold).count());
        }
        return 0;
    }
    if (wParam == kUpdatePollTimerId) {
        if (!updateChecker_ || updateInProgress_.load() || collecting_ || anythingRunning()) return 0;
        BOOL handled = FALSE;
        OnCheckUpdatesClick(0, IDC_UPDATES_CHECK_SONY_BUTTON, nullptr, handled);
        return 0;
    }
    if (wParam != kAutomationTimerId) return 0;
    if (collecting_ || !packageSync_ || anythingRunning()) return 0;

    const auto now = std::chrono::steady_clock::now();
    if (gth::application::IsSonyAutomationDue(automationSettings_, appStarted_, lastAutomationRun_, now)) {
        if (startSync("", automationSettings_.downloadUpdates ? gth::application::SyncMode::Full
                                                               : gth::application::SyncMode::MetadataOnly,
                      /*automatic=*/true)) {
            lastAutomationRun_ = now;
            GTH_LOG_INFO("Sony automation starting");
        }
    }
    return 0;
}

LRESULT UpdatesView::OnGuideClick(WORD, WORD, HWND, BOOL&) {
    const std::wstring guide =
        L"HOW THE UPDATE TOOLS WORK\r\n\r\n"
        L"1. Scan first (Games tab > Scan All Storages), so the app knows your games.\r\n\r\n"
        L"2. Right-click the updates grid and choose Sony update links > Download the public Sony link list. A PS5 game can only be checked once the app has a "
        L"Sony \"version-file link\" for it; the public list has many older titles.\r\n\r\n"
        L"3. Check Sony for Updates. Each game gets a status:\r\n"
        L"    Up to date - it matches Sony's latest package.\r\n"
        L"    Update available - your files are older than Sony's latest.\r\n"
        L"    Version unknown - Sony's latest is known, but not which version you have.\r\n"
        L"    No Sony link - the app has no link for this game yet (step 4).\r\n\r\n"
        L"4. For games with no link: right-click the updates grid and choose Sony update links > Collect links from prosperopatches.com. The app opens the first "
        L"few link-less games' pages in your browser at once; whichever page is in front has its newest patch opened "
        L"automatically and its link copied for you - solve that page's captcha if it shows one, everything else happens "
        L"by itself. It reads the first 64 KB of that small Sony file, takes the "
        L"game's version-file link out of it, saves it and opens the next game's page as each one is done. Skip Game moves past a game "
        L"with no patch; Cancel stops. If a page doesn't open its patch on its own (or the site's captcha check rejects the "
        L"automatic click), click the newest patch yourself and copy the SC "
        L"link (right-click it > Copy link address). You can also paste addresses yourself: Add links from "
        L"the clipboard / from a file. Game-piece links (_0.pkg...) can't be used - Sony keeps them in another folder.\r\n\r\n"
        L"5. Update All Games is the one to press when you want everything current. It first looks through your "
        L"storages for games added since the last scan (\"Find new games first\"), reads the Sony link out of any "
        L"metadata piece already in your archive, works out which games need something, then goes through them one "
        L"after another until the line is empty - adding the small metadata piece and downloading only the pieces "
        L"that changed. Every file is checked against Sony's checksum before it touches your archive, each game's row "
        L"shows where it is, and Cancel is always safe (what is already downloaded is kept and continues next time). "
        L"A game that fails because of a dropped connection is tried again after the rest of the line. Leave the app "
        L"open; it keeps Windows from sleeping while it works. Sync Selected does the same for just the game you "
        L"picked.\r\n\r\n"
        L"    PS4 games need no link: when Sony has a newer update than the version your files show, it is downloaded "
        L"(each piece checked against Sony's checksum), joined into the one .pkg it is made of and saved in an "
        L"\"Updates\" folder beside the game's own files - the base game is never touched - and the next scan shows "
        L"the game at the new version.\r\n\r\n"
        L"6. The first three boxes let the app do the same thing by itself every 12 hours while it is open; automatic "
        L"downloads are on by default and can use many GB. The first run starts about two minutes after launch. "
        L"\"Find new games first\" scans reachable storages before each scheduled run.\r\n\r\n"
        L"7. Version: select a game, type its version and press Set (saved next to its files); Reset goes back to "
        L"automatic. Import List loads \"TITLEID, version\" lines.";
    ::MessageBoxW(m_hWnd, guide.c_str(), L"Updates - guide", MB_OK | MB_ICONINFORMATION);
    return 0;
}

const gth::application::GameLibraryEntry* UpdatesView::selectedEntry() {
    if (!listView_.IsWindow()) return nullptr;
    const int row = listView_.GetSelectedIndex();
    if (row < 0 || static_cast<size_t>(row) >= entries_.size()) return nullptr;
    return &entries_[static_cast<size_t>(row)];
}

void UpdatesView::selectVariant(const std::string& gameVariantId) {
    for (size_t row = 0; row < entries_.size(); ++row) {
        if (entries_[row].gameVariantId == gameVariantId) {
            listView_.SetItemState(static_cast<int>(row), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            listView_.EnsureVisible(static_cast<int>(row), FALSE);
            return;
        }
    }
}

LRESULT UpdatesView::OnListSelectionChanged(int, LPNMHDR pnmh, BOOL& bHandled) {
    // NMLISTVIEW::hdr is its first member, so the notification header and full record share an address.
    const auto* change = std::bit_cast<LPNMLISTVIEW>(pnmh);
    if ((change->uChanged & LVIF_STATE) != 0 && ((change->uNewState ^ change->uOldState) & LVIS_SELECTED) != 0) {
        updateVersionControls();
        board_.refresh();
    }
    bHandled = TRUE;
    return 0;
}

LRESULT UpdatesView::OnCheckUpdatesClick(WORD, WORD, HWND, BOOL&) {
    if (!updateChecker_ || !shared_->tryBeginJob()) return 0;
    if (updateThread_.joinable()) updateThread_.join();

    updateInProgress_.store(true);
    setBusy(true);
    statusLabel_.SetWindowText(L"Asking Sony about every game in the library - this can take a minute or two...");

    updateThread_ = std::jthread([this, hwnd = m_hWnd] {
        checkLearnedLinks_.store(static_cast<int>(learnLinksFromArchive()));
        auto summary = std::make_unique<gth::application::UpdateCheckSummary>(updateChecker_->checkAll());
        gth::app::detail::postOwnedMessage(hwnd, kUpdateCheckDoneMessage, std::move(summary));
    });
    return 0;
}

LRESULT UpdatesView::OnUpdateCheckDone(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto summary = gth::app::detail::takeOwnedMessage<gth::application::UpdateCheckSummary>(lParam);
    updateInProgress_.store(false);
    shared_->endJob();

    shared_->updateByVariant.clear();
    for (const auto& entry : summary->entries) shared_->updateByVariant[entry.gameVariantId] = entry;
    if (summary->updatesAvailable != lastNotifiedUpdateCount_) {
        lastNotifiedUpdateCount_ = summary->updatesAvailable;
        if (notificationCallback_) notificationCallback_(summary->updatesAvailable);
    }
    setBusy(false);
    refresh();

    std::wstring status = describeCheck(*summary, sonyLinksPath_);
    if (const int learned = checkLearnedLinks_.exchange(0); learned > 0) {
        status = std::format(L"Read {} Sony link(s) out of metadata pieces already in the archive.\r\n{}", learned,
                             status);
    }
    statusLabel_.SetWindowText(status.c_str());

    std::string logLine = WideToUtf8(status);
    std::replace(logLine.begin(), logLine.end(), '\r', ' ');
    std::replace(logLine.begin(), logLine.end(), '\n', '|');
    GTH_LOG_INFO("Sony update check finished: {}", logLine);
    return 0;
}

size_t UpdatesView::learnLinksFromArchive() {
    if (!packageSync_) return 0;
    const auto pieces = packageSync_->metadataPieceFiles();
    if (pieces.empty()) return 0;

    const auto learned = gth::network::LearnLinksFromLocalMetadata(pieces, sonyLinksPath_, linkFiles());
    if (learned.learned > 0) GTH_LOG_INFO("Learned {} Sony link(s) from metadata pieces in the archive", learned.learned);
    if (learned.saveFailed) GTH_LOG_WARN("Couldn't save the Sony links read from the archive to {}", sonyLinksPath_.string());
    return learned.learned;
}

std::wstring UpdatesView::describeCheck(const gth::application::UpdateCheckSummary& summary,
                                      const std::filesystem::path& sonyLinksPath) {
    std::array<wchar_t, 320> line{};
    swprintf_s(line.data(), line.size(), L"Sony check of %d game(s): %d up to date, %d with an update available, %d version unknown, "
                     L"%d without a Sony link, %d failed.",
               static_cast<int>(summary.entries.size()), summary.upToDate, summary.updatesAvailable,
               summary.installedUnknown, summary.noSonyData, summary.failed);
    std::wstring status = line.data();
    if (summary.noSonyData > 0) {
        status += std::format(
            L"\r\n{} PS5 game(s) have no Sony link yet: right-click the updates grid and choose Sony update links > Add links from the clipboard (copy each game's "
            L"SC link from its prosperopatches.com page - see Guide), or download the public list (links are kept in {}).",
            summary.noSonyData, sonyLinksPath.wstring());
    }
    if (summary.failed > 0) status += L"\r\nFirst failure: " + Utf8ToWide(summary.firstFailure);
    return status;
}

void UpdatesView::applyAutomationSettingsToControls() {
    if (!autoCheckBox_.IsWindow()) return;
    autoCheckBox_.SetCheck(automationSettings_.checkForUpdates ? BST_CHECKED : BST_UNCHECKED);
    autoMetadataBox_.SetCheck(automationSettings_.addMissingMetadata ? BST_CHECKED : BST_UNCHECKED);
    autoUpdatesBox_.SetCheck(automationSettings_.downloadUpdates ? BST_CHECKED : BST_UNCHECKED);
    autoScanBox_.SetCheck(automationSettings_.scanForNewGames ? BST_CHECKED : BST_UNCHECKED);
    updateAutomationSummary();
}

void UpdatesView::updateAutomationSummary() {
    if (!automationSummaryLabel_.IsWindow()) return;
    const bool enabled = automationSettings_.checkForUpdates || automationSettings_.addMissingMetadata ||
                         automationSettings_.downloadUpdates || automationSettings_.scanForNewGames;
    automationSummaryLabel_.SetWindowText(enabled ? L"Auto-sync on" : L"Auto-sync off");
}

LRESULT UpdatesView::OnAutomationSettingClicked(WORD, WORD id, HWND, BOOL&) {
    if (id == IDC_UPDATES_AUTO_UPDATES_CHECKBOX && autoUpdatesBox_.GetCheck() == BST_CHECKED) {
        // Automatic downloads of many gigabytes deserve one deliberate "yes".
        const int answer = ::MessageBoxW(
            m_hWnd,
            L"Download updates automatically?\r\n\r\nWhen a game in the archive falls behind Sony's latest package, the app "
            L"will download the pieces that changed (often many GB) and replace them, each one only after it matches "
            L"Sony's checksum. It runs while the app is open, at the interval in sony-automation.txt.",
            L"Download updates automatically", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (answer != IDYES) autoUpdatesBox_.SetCheck(BST_UNCHECKED);
    }

    automationSettings_.checkForUpdates = autoCheckBox_.GetCheck() == BST_CHECKED;
    automationSettings_.addMissingMetadata = autoMetadataBox_.GetCheck() == BST_CHECKED;
    automationSettings_.downloadUpdates = autoUpdatesBox_.GetCheck() == BST_CHECKED;
    automationSettings_.scanForNewGames = autoScanBox_.GetCheck() == BST_CHECKED;
    updateAutomationSummary();
    if (automationStore_ && !automationStore_->save(automationSettings_)) {
        statusLabel_.SetWindowText(std::format(L"Couldn't save the automation settings to {}",
                                               automationStore_->path().wstring()).c_str());
    }
    return 0;
}

LRESULT UpdatesView::OnSyncSelectedClick(WORD, WORD, HWND, BOOL&) {
    const auto* entry = selectedEntry();
    if (!entry || !packageSync_ || anythingRunning()) return 0;

    const std::wstring question = std::format(
        L"Sync \"{}\" with Sony?\r\n\r\n"
        L"This adds the small metadata piece the game is missing. If the game is out of date, it also downloads the pieces "
        L"that changed (can be many GB) and replaces them - each one only after it matches Sony's checksum, and nothing in "
        L"the archive is touched until then.", Utf8ToWide(entry->title));
    if (::MessageBoxW(m_hWnd, question.c_str(), L"Sync with Sony", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return 0;

    startSync(entry->gameVariantId, gth::application::SyncMode::Full, /*automatic=*/false);
    return 0;
}

LRESULT UpdatesView::OnDownloadAllClick(WORD, WORD, HWND, BOOL&) {
    if (!packageSync_ || anythingRunning()) return 0;

    const int answer = ::MessageBoxW(
        m_hWnd,
        L"Update every game that is behind?\r\n\r\n"
        L"The app looks for new games in your storages (unless \"Find new games first\" is off), asks Sony about every "
        L"game, then goes through the ones that need something, one after another, until the line is empty. That can "
        L"be many GB and take hours. Each file is checked against Sony's checksum before it touches your archive, a "
        L"game that fails because of the network is tried again at the end, and you can press Cancel at any time - "
        L"what is already downloaded is kept and continues next time.\r\n\r\n"
        L"For PS5 games without a Sony link, it automatically opens Prosperopatches, collects the links you confirm, saves "
        L"them to a Desktop text file, and continues with one game at a time.\r\n\r\n"
        L"Keep the app open; it stops Windows from going to sleep on its own while it works.",
        L"Update All Games", MB_OKCANCEL | MB_ICONQUESTION);
    if (answer != IDOK) return 0;

    updateAllChain_ = true;  // this press covers the whole job, links included
    startSync("", gth::application::SyncMode::Full, /*automatic=*/false);
    return 0;
}

LRESULT UpdatesView::OnCancelSyncClick(WORD, WORD, HWND, BOOL&) {
    // A collecting_ session can now have one of its just-gathered games downloading in the
    // background at the same time (see queueAutoDownload) - stop that first if it's what's
    // actually running; collecting_ alone (nothing downloading right now) still means Cancel
    // ends the whole session, as it always did.
    if (syncInProgress_.load()) {
        syncCancel_.store(true);
        shared_->queuedDownloads.clear();  // don't immediately start the next queued game either
        statusLabel_.SetWindowText(L"Cancelling - finishing the current step...");
    } else if (collecting_) {
        finishCollecting(L"Stopped.");
    } else if (linksInProgress_.load()) {
        linksCancel_.store(true);
        statusLabel_.SetWindowText(L"Cancelling - links already read are kept...");
    }
    return 0;
}

bool UpdatesView::startSync(const std::string& gameVariantId, gth::application::SyncMode mode, bool automatic,
                            bool checkAfter) {
    if (!packageSync_ || !indexer_ || !shared_->tryBeginJob()) return false;
    if (syncThread_.joinable()) syncThread_.join();

    syncCancel_.store(false);
    shared_->cancelRequested.store(false);
    syncInProgress_.store(true);
    liveRows_.clear();
    shared_->liveDownloads.clear();
    setBusy(true);
    statusLabel_.SetWindowText(automatic ? L"Sony automation is running..." : L"Working out what needs downloading...");

    // Names for the progress line: the thread only has title IDs.
    std::map<std::string, std::string> titles;
    for (const auto& entry : entries_) titles[entry.titleId] = entry.title;
    // Scanning is part of every all-games run, including automatic metadata-only/check runs, so newly added
    // packages enter the library and the next update check can see them.
    const bool lookForNewGames = gameVariantId.empty() && automationSettings_.scanForNewGames;

    syncThread_ = std::jthread([this, hwnd = m_hWnd, gameVariantId, mode, automatic, checkAfter, settings = automationSettings_,
                               titles = std::move(titles), lookForNewGames] {
        // A long download line must not be interrupted by Windows going to sleep on its own.
        struct KeepAwake {
            KeepAwake() { ::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED); }
            ~KeepAwake() { ::SetThreadExecutionState(ES_CONTINUOUS); }
        } keepAwake;

        const auto began = std::chrono::steady_clock::now();
        auto outcome = std::make_unique<SyncJobOutcome>();
        outcome->automatic = automatic;
        outcome->startedAt = std::time(nullptr);

        auto names = titles;  // grows when the scan below finds games the list didn't have
        const auto nameOf = [&](const std::string& titleId) {
            const auto found = names.find(titleId);
            return Utf8ToWide(found != names.end() && !found->second.empty() ? found->second : titleId);
        };
        const auto post = [&](const std::wstring& text) {
            gth::app::detail::postOwnedMessage(hwnd, kSyncProgressMessage, std::make_unique<std::wstring>(text));
        };
        const auto postRow = [&](const std::string& titleId, const std::wstring& text) {
            gth::app::detail::postOwnedMessage(hwnd, kSyncRowMessage, std::make_unique<RowUpdate>(RowUpdate{titleId, text}));
        };

        // One row per game in the Downloads tab's history - real per-game numbers, not just this
        // run's narrative summary. `startedAt`: 0 means "not tracked" (a batch game whose first
        // progress event this run never saw, e.g. it was skipped before downloading anything) -
        // recorded anyway, just with durationSeconds 0 (shown as "-", never as a fabricated speed).
        const auto recordHistory = [&](const std::string& titleId, gth::application::SyncKind kind,
                                       gth::application::DownloadHistoryOutcome result, long long bytes,
                                       const std::string& message, std::chrono::steady_clock::time_point startedAt) {
            gth::application::DownloadHistoryEntry entry;
            entry.when = std::time(nullptr);
            entry.titleId = titleId;
            entry.title = WideToUtf8(nameOf(titleId));
            entry.kind = kind;
            entry.outcome = result;
            entry.bytesDownloaded = bytes;
            entry.durationSeconds = startedAt.time_since_epoch().count() == 0
                                        ? 0
                                        : std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startedAt).count();
            entry.message = message;
            gth::application::AppendDownloadHistory(downloadHistoryPath_, entry);
        };
        std::map<std::string, std::chrono::steady_clock::time_point> gameStarted;

        gth::application::SyncCallbacks callbacks;
        // Two ways to ask for this: the Updates tab's own Cancel button, or the Downloads tab's
        // Stop on the row currently downloading (shared_->cancelRequested) - either one stops it.
        callbacks.shouldCancel = [this] { return syncCancel_.load() || shared_->cancelRequested.load(); };
        if (gameVariantId.empty()) {
            // A dropped connection halfway through a long line shouldn't leave a game for someone to press the
            // button again for: failures that can pass by themselves are retried after the rest of the line.
            callbacks.retryRounds = 2;
            callbacks.retryPause = std::chrono::seconds(45);
        }

        callbacks.onQueue = [&](const std::vector<gth::application::SyncQueueItem>& line) {
            if (line.empty()) {
                post(L"Nothing needs downloading.");
                return;
            }
            long long total = 0;
            for (const auto& item : line) {
                total += item.bytesToDownload;
                postRow(item.titleId, item.kind == gth::application::SyncKind::CompleteMetadata
                                          ? std::format(L"Queued - adds the small metadata piece ({})",
                                                        formatSize(item.bytesToDownload))
                                          : std::format(L"Queued - at least {} to download",
                                                        formatSize(item.bytesToDownload)));
            }
            post(std::format(L"The line has {} game(s), at least {} to download. Starting...", line.size(),
                             formatSize(total)));
        };

        auto lastPost = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        callbacks.onProgress = [&](const gth::application::SyncProgress& progress) {
            // Roughly two updates a second: a big download reports every megabyte.
            const auto now = std::chrono::steady_clock::now();
            const bool finished = progress.bytesTotal > 0 && progress.bytesDone >= progress.bytesTotal;
            if (!finished && now - lastPost < std::chrono::milliseconds(400)) return;
            lastPost = now;

            std::wstring step = Utf8ToWide(progress.step);
            if (const auto colon = step.find(L':'); colon != std::wstring::npos) step.resize(colon);  // drop the file name
            std::wstring amount;
            if (progress.bytesTotal > 0) {
                std::array<wchar_t, 24> percent{};
                swprintf_s(percent.data(), percent.size(), L"  %d%%  ",
                           static_cast<int>(100.0 * progress.bytesDone / progress.bytesTotal));
                amount = percent.data() + formatSize(progress.bytesDone) + L" of " + formatSize(progress.bytesTotal);
            }
            if (progress.gameCount > 0) {
                gameStarted.try_emplace(progress.titleId, std::chrono::steady_clock::now());
                post(std::format(L"[{}/{}] {}: {}{}", progress.gameIndex, progress.gameCount,
                                 nameOf(progress.titleId), step, amount));
                postRow(progress.titleId, step + amount);
            } else {
                post(step + amount);
            }
        };

        callbacks.onGameDone = [&](const gth::application::SyncGameOutcome& game) {
            const std::wstring what = Utf8ToWide(game.message);
            if (game.skippedNeedsUpdate) {
                postRow(game.titleId, std::format(L"Not downloaded - {}", what));
            } else if (game.success) {
                postRow(game.titleId, std::format(L"Done - {}", what));
            } else {
                postRow(game.titleId, std::format(L"FAILED - {}", what));
            }

            auto outcome = gth::application::DownloadHistoryOutcome::Failed;
            if (game.skippedNeedsUpdate) {
                outcome = gth::application::DownloadHistoryOutcome::SkippedNeedsUpdate;
            } else if (game.success) {
                outcome = gth::application::DownloadHistoryOutcome::Success;
            }
            const auto started = gameStarted.find(game.titleId);
            recordHistory(game.titleId, game.kind, outcome, game.bytesDownloaded, game.message,
                          started != gameStarted.end() ? started->second : std::chrono::steady_clock::time_point{});
        };

        // ---- games added since the last scan ----------------------------------------------------------------
        if (lookForNewGames && storageService_ && library_) {
            outcome->scannedForNewGames = true;
            post(L"Looking for new games in the storages...");
            storageService_->detectAndSyncLocalStorages();
            const auto storages = storageService_->getAllStorages();
            int position = 0;
            for (const auto& storage : storages) {
                ++position;
                if (syncCancel_.load()) break;
                // A drive or share that is switched off would look like every game on it vanishing.
                std::error_code reachable;
                if (!std::filesystem::is_directory(storage.rootPath, reachable)) {
                    ++outcome->storagesUnreachable;
                    continue;
                }
                post(std::format(L"Looking for new games - storage {} of {}: {}", position, storages.size(),
                                 Utf8ToWide(storage.name)));
                const auto scan = indexer_->scanStorage(storage.storageId);
                if (scan.success) outcome->newGamesFound += scan.gamesAdded;
            }
            for (const auto& entry : library_->getAllEntries()) {
                if (!entry.titleId.empty()) names[entry.titleId] = entry.title;
            }
            if (outcome->newGamesFound > 0) ::PostMessageW(hwnd, kSyncListMessage, 0, 0);
        }

        // ---- links the archive already carries -----------------------------------------------------------------
        if (!syncCancel_.load()) outcome->linksLearnedFromFiles = static_cast<int>(learnLinksFromArchive());

        std::set<std::string> toRescan;
        const bool syncGames = !gameVariantId.empty() || !automatic || settings.addMissingMetadata || settings.downloadUpdates;
        if (!gameVariantId.empty()) {
            const auto gameBegan = std::chrono::steady_clock::now();
            outcome->ranSync = true;
            outcome->oneGame = true;
            outcome->single = packageSync_->run(gameVariantId, mode, callbacks);
            if (outcome->single.filesDownloaded > 0 && !outcome->single.storageId.empty()) toRescan.insert(outcome->single.storageId);

            auto result = gth::application::DownloadHistoryOutcome::Failed;
            if (outcome->single.cancelled) {
                result = gth::application::DownloadHistoryOutcome::Cancelled;
            } else if (outcome->single.skippedNeedsUpdate) {
                result = gth::application::DownloadHistoryOutcome::SkippedNeedsUpdate;
            } else if (outcome->single.success) {
                result = gth::application::DownloadHistoryOutcome::Success;
            }
            recordHistory(outcome->single.titleId, outcome->single.kind, result, outcome->single.bytesDownloaded,
                          outcome->single.message, gameBegan);
        } else if (syncGames) {
            outcome->ranSync = true;
            outcome->batch = packageSync_->runAll(mode, callbacks);
            toRescan = outcome->batch.storageIds;
        }

        // What was added to the archive has to be read back into the library.
        for (const auto& storageId : toRescan) {
            if (syncCancel_.load()) break;
            post(L"Rescanning the storage that changed...");
            indexer_->scanStorage(storageId);
            ++outcome->storagesRescanned;
        }

        const bool wantCheck = checkAfter && (!automatic || settings.checkForUpdates);
        if (wantCheck && !syncCancel_.load() && updateChecker_) {
            post(L"Asking Sony whether each game is up to date...");
            outcome->check = updateChecker_->checkAll();
            outcome->checked = true;
        }

        outcome->took = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - began);
        gth::app::detail::postOwnedMessage(hwnd, kSyncDoneMessage, std::move(outcome));
    });
    return true;
}

LRESULT UpdatesView::OnSyncProgress(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto text = gth::app::detail::takeOwnedMessage<std::wstring>(lParam);
    if (syncInProgress_.load()) statusLabel_.SetWindowText(text->c_str());  // a late line must not overwrite the result
    return 0;
}

LRESULT UpdatesView::OnSyncRow(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto update = gth::app::detail::takeOwnedMessage<RowUpdate>(lParam);
    if (!syncInProgress_.load()) return 0;  // the line has ended and the list has been rebuilt

    liveRows_[update->titleId] = update->text;
    if (const auto rows = rowsByTitle_.find(update->titleId); rows != rowsByTitle_.end()) {
        for (const int row : rows->second) {
            if (static_cast<size_t>(row) < entries_.size()) {
                listView_.SetItemText(row, 0, composeLabel(entries_[static_cast<size_t>(row)]).c_str());
            }
        }
    }

    std::string title = update->titleId;
    for (const auto& entry : entries_) {
        if (entry.titleId == update->titleId && !entry.title.empty()) {
            title = entry.title;
            break;
        }
    }
    shared_->liveDownloads[update->titleId] = {title, update->text};
    return 0;
}

LRESULT UpdatesView::OnSyncList(UINT, WPARAM, LPARAM, BOOL&) {
    if (syncInProgress_.load()) refresh();  // new games turned up: give them rows before the line starts
    return 0;
}

// Visible proof that SonyLinkCopyWatcher actually copied something - the app's clipboard
// watcher (collectTick) should pick this up and either show "Reading the Sony addresses..."
// or a specific failure note within a second or two. If this message alone never appears,
// the copy itself isn't happening; if it appears but nothing follows, the problem is after.
LRESULT UpdatesView::OnSonyLinkCopied(UINT, WPARAM, LPARAM, BOOL&) {
    if (!collecting_) return 0;
    lastAutoclickProgressAt_ = std::chrono::steady_clock::now();
    statusLabel_.SetWindowText(L"Copied a patch's SC link automatically - reading it now...");
    return 0;
}

// Visible proof that PatchAutoClicker did something - a click (wParam 0); one of three outcomes
// once the wait after it ends, each retried automatically up to a few times (wParam 1: the site's
// captcha rejected it; wParam 2: no patch heading ever found; wParam 3: couldn't confirm what
// happened); or, once a title has failed enough times in a row, one final notice that it's being
// left alone for the rest of the session and genuinely needs a person now (wParam 4). Every case
// also marks that game's own row in the list - not just the status line, which the next game's
// progress can overwrite within moments - so it stays visible (in the Update column, right where
// the person is already looking) until it's actually resolved, instead of the session just looking
// generically "stuck" with no clue which game or why. A click (wParam 0) also counts as proof the
// automation is actively making progress - see lastAutoclickProgressAt_.
LRESULT UpdatesView::OnPatchAutoClick(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    auto titleIdOwned = gth::app::detail::takeOwnedMessage<std::wstring>(lParam);
    if (!collecting_) return 0;

    if (wParam == 0) lastAutoclickProgressAt_ = std::chrono::steady_clock::now();

    const std::string titleId = titleIdOwned ? WideToUtf8(*titleIdOwned) : std::string();
    std::wstring gameName = titleIdOwned ? *titleIdOwned : L"this game";
    for (const auto& entry : entries_) {
        if (entry.titleId == titleId && !entry.title.empty()) {
            gameName = Utf8ToWide(entry.title);
            break;
        }
    }

    const auto setRow = [&](const std::wstring& text) {
        if (titleId.empty()) return;
        liveRows_[titleId] = text;
        if (const auto rows = rowsByTitle_.find(titleId); rows != rowsByTitle_.end()) {
            for (const int row : rows->second) {
                if (static_cast<size_t>(row) < entries_.size()) {
                    listView_.SetItemText(row, 0, composeLabel(entries_[static_cast<size_t>(row)]).c_str());
                }
            }
        }
    };

    // setRow()'s own text is what the poster tile itself shows - a two-line label (see
    // composeLabel) has no room for a full sentence, so this is deliberately just a few words;
    // statusLabel_ right below still gets the full explanation every time, unchanged.
    switch (wParam) {
        case 1:
            setRow(L"Captcha rejected - retrying");
            statusLabel_.SetWindowText((L"The site's captcha check rejected the automatic click for " + gameName +
                                        L" - it will try again automatically (or click its newest patch yourself).").c_str());
            break;
        case 2:
            setRow(L"No patch found - retrying");
            statusLabel_.SetWindowText((gameName + L": no patch heading found automatically - it will try again "
                                        L"(it may genuinely have none, in which case check its page yourself).").c_str());
            break;
        case 3:
            setRow(L"Unconfirmed - retrying");
            statusLabel_.SetWindowText((gameName + L": couldn't confirm the link appeared after clicking - "
                                        L"it will try again automatically.").c_str());
            break;
        case 4:
            setRow(L"Needs you");
            statusLabel_.SetWindowText((gameName + L" needs you - tried automatically a few times without success. "
                                        L"Click its newest patch yourself, or Skip Game to move on.").c_str());
            break;
        default:
            setRow(L"Opened - waiting for link");
            statusLabel_.SetWindowText((L"Opened " + gameName + L"'s newest patch automatically - solve the captcha if the site shows one.").c_str());
            break;
    }
    return 0;
}

LRESULT UpdatesView::OnSyncDone(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto outcome = gth::app::detail::takeOwnedMessage<SyncJobOutcome>(lParam);
    syncInProgress_.store(false);
    shared_->endJob();
    liveRows_.clear();
    shared_->liveDownloads.clear();

    // What this run did, written down so it can be read later (and shown when the tab opens next time).
    {
        std::map<std::string, std::string> titles;
        for (const auto& entry : entries_) titles[entry.titleId] = entry.title;
        gth::application::SonyRunReportInput report;
        report.when = outcome->startedAt;
        report.automatic = outcome->automatic;
        report.duration = outcome->took;
        report.syncRan = outcome->ranSync;
        report.oneGame = outcome->oneGame;
        report.single = outcome->single;
        report.batch = outcome->batch;
        report.storagesRescanned = outcome->storagesRescanned;
        report.scannedForNewGames = outcome->scannedForNewGames;
        report.newGamesFound = outcome->newGamesFound;
        report.linksLearnedFromFiles = outcome->linksLearnedFromFiles;
        report.checked = outcome->checked;
        report.check = outcome->check;
        gth::application::AppendSonyRunReport(reportPath_, gth::application::FormatSonyRunReport(report, titles));
    }

    if (outcome->checked) {
        shared_->updateByVariant.clear();
        for (const auto& entry : outcome->check.entries) shared_->updateByVariant[entry.gameVariantId] = entry;
        if (outcome->check.updatesAvailable != lastNotifiedUpdateCount_) {
            lastNotifiedUpdateCount_ = outcome->check.updatesAvailable;
            if (notificationCallback_) notificationCallback_(outcome->check.updatesAvailable);
        }
    }
    setBusy(false);
    refresh();

    std::wstring status;
    if (outcome->scannedForNewGames) {
        status = std::format(L"Looked for new games: {} found", outcome->newGamesFound);
        if (outcome->storagesUnreachable > 0) {
            status += std::format(L" ({} storage(s) weren't reachable and were left as they are)",
                                  outcome->storagesUnreachable);
        }
        status += L".";
    }
    if (outcome->linksLearnedFromFiles > 0) {
        status += std::format(L"{}Read {} Sony link(s) out of files already in the archive.",
                              status.empty() ? L"" : L" ", outcome->linksLearnedFromFiles);
    }
    if (!status.empty()) status += L"\r\n";
    if (outcome->oneGame) {
        status += Utf8ToWide(outcome->single.titleId) + L": " + Utf8ToWide(outcome->single.message);
    } else if (outcome->ranSync) {
        const auto& b = outcome->batch;
        std::array<wchar_t, 400> line{};
        swprintf_s(line.data(), line.size(), L"Download line %s: %d updated, %d metadata added, %d already current, %d not downloaded, %d without a "
                         L"usable Sony link, %d failed. Downloaded %s in %d min.",
                   b.cancelled ? L"stopped" : L"finished", b.updated, b.completed, b.alreadyFine, b.needUpdate, b.unavailable,
                   b.failed, formatSize(b.bytesDownloaded).c_str(), static_cast<int>(outcome->took.count() / 60));
        status += line.data();
        if (b.recoveredOnRetry > 0) {
            status += std::format(L" {} game(s) failed at first and worked on a retry.", b.recoveredOnRetry);
        }
        if (b.failed > 0) {
            status += std::format(
                L"\r\n{} game(s) failed (first: {}). Press Update All Games again to retry - finished files are kept and downloads resume.",
                b.failed, Utf8ToWide(b.firstFailure));
        } else if (b.cancelled) {
            status += L"\r\nPress Update All Games again to carry on - finished files are kept and downloads resume.";
        }
    }
    if (outcome->checked) {
        if (!status.empty()) status += L"\r\n";
        status += describeCheck(outcome->check, sonyLinksPath_);
    }
    if (status.empty() || status == L"\r\n") status = L"Nothing to do.";
    statusLabel_.SetWindowText(status.c_str());

    std::string logLine = WideToUtf8(status);
    std::replace(logLine.begin(), logLine.end(), '\r', ' ');
    std::replace(logLine.begin(), logLine.end(), '\n', '|');
    GTH_LOG_INFO("Sony {} finished: {}", outcome->automatic ? "automation" : "sync", logLine);

    const bool chained = updateAllChain_;
    updateAllChain_ = false;
    if (chained && outcome->ranSync && !outcome->oneGame && !outcome->batch.cancelled && outcome->checked &&
        outcome->check.noSonyData > 0) {
        offerLinkCollection(outcome->check.noSonyData);
    }
    tryStartNextAutoDownload();  // the one-job slot just freed - let the next queued game take it
    return 0;
}

void UpdatesView::offerLinkCollection(int gamesWithoutLink) {
    if (gamesWithoutLink <= 0) return;
    startCollecting();
    updateAllAfterCollect_ = collecting_;
}

std::vector<std::filesystem::path> UpdatesView::linkFiles() const {
    std::vector<std::filesystem::path> files;
    std::array<wchar_t, MAX_PATH> exePath{};
    if (::GetModuleFileNameW(nullptr, exePath.data(), static_cast<DWORD>(exePath.size())) > 0) {
        files.push_back(std::filesystem::path(exePath.data()).parent_path() / "sony-version-links.tsv");
    }
    files.push_back(sonyLinksPath_);
    files.push_back(sonyLinksPath_.parent_path() / "sony-links-needed.tsv");
    return files;
}

std::vector<std::pair<std::string, std::string>> UpdatesView::ps5Games() const {
    std::vector<std::pair<std::string, std::string>> games;
    std::set<std::string> seen;
    for (const auto& entry : entries_) {
        if (entry.platform != "PS5" || entry.variantStatus != gth::core::GameVariantStatus::Active) continue;
        if (entry.titleId.empty() || !seen.insert(entry.titleId).second) continue;
        games.emplace_back(entry.titleId, entry.title);
    }
    return games;
}

LRESULT UpdatesView::OnSonyLinksClick(WORD, WORD, HWND, BOOL&) {
    if (anythingRunning()) return 0;

    constexpr UINT kDownload = 1, kNeeded = 2, kOpenFile = 3, kClipboard = 4, kFromFile = 5, kCollect = 6,
                   kOpenCopyLog = 7;
    HMENU menu = ::CreatePopupMenu();
    ::AppendMenuW(menu, MF_STRING, kCollect, L"Collect links from prosperopatches.com (guided)");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kClipboard, L"Add links from the clipboard (copied Sony addresses)");
    ::AppendMenuW(menu, MF_STRING, kFromFile, L"Add links from a file...");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kDownload, L"Download the public Sony link list");
    ::AppendMenuW(menu, MF_STRING, kNeeded, L"List the games that still need a link (opens Notepad)");
    ::AppendMenuW(menu, MF_STRING, kOpenFile, L"Open the links file");
    ::AppendMenuW(menu, MF_STRING, kOpenCopyLog, L"Open the copied-SC-links log (opens Notepad)");
    RECT rect{};
    board_.GetWindowRect(&rect);
    const UINT chosen = static_cast<UINT>(::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, rect.left,
                                                           rect.bottom, 0, m_hWnd, nullptr));
    ::DestroyMenu(menu);

    if (chosen == kCollect) {
        startCollecting();
    } else if (chosen == kClipboard) {
        addLinksFromClipboard();
    } else if (chosen == kFromFile) {
        addLinksFromFile();
    } else if (chosen == kDownload) {
        startLinkDownload();
    } else if (chosen == kNeeded) {
        writeNeededLinksList();
    } else if (chosen == kOpenFile) {
        std::error_code ec;
        if (!std::filesystem::exists(sonyLinksPath_, ec)) {
            std::filesystem::create_directories(sonyLinksPath_.parent_path(), ec);
            std::ofstream(sonyLinksPath_, std::ios::binary)
                << "# Sony version links: <title ID> <tab> <version file link>. Lines can be added by hand or by a download tool.\n";
        }
        ::ShellExecuteW(m_hWnd, L"open", L"notepad.exe", (L"\"" + sonyLinksPath_.wstring() + L"\"").c_str(), nullptr,
                        SW_SHOWNORMAL);
    } else if (chosen == kOpenCopyLog) {
        const auto logPath = sonyLinksPath_.parent_path() / "sony-sc-links-copied.txt";
        std::error_code ec;
        if (!std::filesystem::exists(logPath, ec)) {
            std::filesystem::create_directories(logPath.parent_path(), ec);
            std::ofstream(logPath, std::ios::binary)
                << "# Every SC link the guided collector has copied, in order: <local timestamp> <tab> <link>.\n";
        }
        ::ShellExecuteW(m_hWnd, L"open", L"notepad.exe", (L"\"" + logPath.wstring() + L"\"").c_str(), nullptr,
                        SW_SHOWNORMAL);
    }
    return 0;
}

void UpdatesView::writeNeededLinksList() {
    const auto neededPath = sonyLinksPath_.parent_path() / "sony-links-needed.tsv";

    // Links already filled in on an earlier list move to the main links file first, so writing a fresh list
    // never throws away work.
    std::error_code ec;
    if (std::filesystem::is_regular_file(neededPath, ec)) {
        gth::network::MergeSonyVersionLinks(sonyLinksPath_, readListFile(neededPath));
    }

    const auto known = gth::network::ReadSonyLinkTitleIds(linkFiles());
    std::vector<std::pair<std::string, std::string>> needed;
    for (const auto& game : ps5Games()) {
        if (!known.contains(game.first)) needed.push_back(game);
    }
    if (needed.empty()) {
        statusLabel_.SetWindowText(L"Every PS5 game in the library already has a Sony link.");
        std::filesystem::remove(neededPath, ec);
        return;
    }

    std::filesystem::create_directories(neededPath.parent_path(), ec);
    {
        std::ofstream out(neededPath, std::ios::binary | std::ios::trunc);
        out << gth::network::BuildSonyLinksNeededList(needed);
    }
    statusLabel_.SetWindowText(
        std::format(L"{} of {} PS5 game(s) still need a Sony link. Opened {} - put each game's version-file link at the end of its line, save, then press Check Sony for Updates.",
                    needed.size(), ps5Games().size(), neededPath.wstring())
            .c_str());
    ::ShellExecuteW(m_hWnd, L"open", L"notepad.exe", (L"\"" + neededPath.wstring() + L"\"").c_str(), nullptr,
                    SW_SHOWNORMAL);
}

void UpdatesView::startLinkDownload() {
    if (!shared_->tryBeginJob()) return;
    if (linksThread_.joinable()) linksThread_.join();

    linksInProgress_.store(true);
    setBusy(true);
    statusLabel_.SetWindowText(L"Downloading the public Sony link list...");

    std::vector<std::string> titleIds;
    for (const auto& game : ps5Games()) titleIds.push_back(game.first);

    linksThread_ = std::jthread([this, hwnd = m_hWnd, titleIds, files = linkFiles(), target = sonyLinksPath_] {
        auto outcome = std::make_unique<LinksJobOutcome>();
        const auto fetch = gth::network::MakeSonyFetcher();
        const auto response = fetch(kPublicLinkListUrl);
        if (response.status == 0) {
            outcome->error = response.failure;
        } else if (response.status != 200) {
            outcome->error = std::format("the server answered HTTP {}", response.status);
        } else {
            const auto merged = gth::network::MergeSonyVersionLinks(target, response.body);
            if (merged.incoming == 0) {
                outcome->error = "the list had no links in it";
            } else if (!merged.written) {
                outcome->error = std::format("couldn't save the links to {}", target.string());
            } else {
                outcome->ok = true;
                outcome->downloaded = merged.incoming;
                outcome->total = merged.total;
            }
        }

        const auto known = gth::network::ReadSonyLinkTitleIds(files);
        outcome->ps5Games = static_cast<int>(titleIds.size());
        for (const auto& id : titleIds) outcome->covered += known.contains(id) ? 1 : 0;
        gth::app::detail::postOwnedMessage(hwnd, kLinksDoneMessage, std::move(outcome));
    });
}

void UpdatesView::addLinksFromClipboard() {
    const std::string text = readClipboardText(m_hWnd).value_or("");
    if (gth::network::ExtractSonyAddresses(text).empty()) {
        ::MessageBoxW(m_hWnd,
                      L"The clipboard has no Sony addresses in it.\r\n\r\n"
                      L"Open the game's page on prosperopatches.com (Tracker Page opens it), click a patch, and copy the "
                      L"SC link (right-click the SC button > Copy link address) - the CRC or manifest link works too. "
                      L"Then choose this again. (The updates grid's context menu > Sony update links > Collect links from prosperopatches.com does the "
                      L"stepping from game to game for you.)",
                      L"Add links from the clipboard", MB_OK | MB_ICONINFORMATION);
        return;
    }
    startLinkImport(text);
}

void UpdatesView::addLinksFromFile() {
    if (anythingRunning()) return;

    std::array<wchar_t, MAX_PATH> file{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = m_hWnd;
    dialog.lpstrFile = file.data();
    dialog.nMaxFile = static_cast<DWORD>(file.size());
    dialog.lpstrTitle = L"Add links from a file of Sony addresses";
    dialog.lpstrFilter = L"Lists (*.txt;*.csv;*.tsv;*.log)\0*.txt;*.csv;*.tsv;*.log\0All files (*.*)\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!::GetOpenFileNameW(&dialog)) return;

    const std::string text = readListFile(file.data());
    if (gth::network::ExtractSonyAddresses(text).empty()) {
        ::MessageBoxW(m_hWnd, L"That file has no Sony addresses (sgst / gst.prod.dl.playstation.net) in it.",
                      L"Add links from a file", MB_OK | MB_ICONINFORMATION);
        return;
    }
    startLinkImport(text);
}

bool UpdatesView::startLinkImport(std::string text, bool guided) {
    if (!shared_->tryBeginJob()) return false;
    if (linksThread_.joinable()) linksThread_.join();

    linksCancel_.store(false);
    linksInProgress_.store(true);
    setBusy(true);
    statusLabel_.SetWindowText(L"Reading the Sony addresses...");

    std::vector<std::string> titleIds;
    for (const auto& game : ps5Games()) titleIds.push_back(game.first);

    linksThread_ = std::jthread([this, hwnd = m_hWnd, text = std::move(text), titleIds, files = linkFiles(), target = sonyLinksPath_, guided] {
        auto outcome = std::make_unique<LinksJobOutcome>();
        outcome->imported = true;
        outcome->guided = guided;

        gth::network::SonyLinkImporter importer(target, files);
        outcome->importResult = importer.importLinks(
            text, [this] { return linksCancel_.load(); },
            [hwnd](const std::string& line) {
                gth::app::detail::postOwnedMessage(hwnd, kLinksProgressMessage,
                                                   std::make_unique<std::wstring>(Utf8ToWide(line)));
            });
        outcome->ok = !outcome->importResult.saveFailed;

        const auto known = gth::network::ReadSonyLinkTitleIds(files);
        outcome->ps5Games = static_cast<int>(titleIds.size());
        for (const auto& id : titleIds) outcome->covered += known.contains(id) ? 1 : 0;
        gth::app::detail::postOwnedMessage(hwnd, kLinksDoneMessage, std::move(outcome));
    });
    return true;
}

// ---- Guided collection of Sony links ---------------------------------------------------------------------------------

void UpdatesView::appendCollectedLinksToDesktop(const std::vector<std::string>& titleIds) {
    if (const auto desktop = desktopDirectory(); !desktop.empty()) {
        collectLinksLogPath_ = desktop / "GTH-Prosperopatches-links.txt";
    }

    const auto links = gth::network::ParseSonyVersionLinks(readListFile(sonyLinksPath_));
    if (links.empty()) return;

    std::set<std::string> loggedIds;
    {
        std::ifstream existing(collectLinksLogPath_);
        std::string line;
        while (std::getline(existing, line)) {
            const auto tab = line.find('\t');
            if (tab != std::string::npos && tab > 0) loggedIds.insert(line.substr(0, tab));
        }
    }

    std::ofstream output(collectLinksLogPath_, std::ios::app);
    if (!output) return;
    for (const auto& titleId : titleIds) {
        const auto link = links.find(titleId);
        if (link == links.end() || !loggedIds.insert(titleId).second) continue;
        output << titleId << '\t' << link->second << "\r\n";
    }
}

void UpdatesView::startCollecting() {
    if (collecting_ || anythingRunning()) return;

    const auto known = gth::network::ReadSonyLinkTitleIds(linkFiles());
    int needed = 0;
    for (const auto& game : ps5Games()) needed += known.contains(game.first) ? 0 : 1;
    if (needed == 0) {
        statusLabel_.SetWindowText(L"Every PS5 game in the library already has a Sony link.");
        return;
    }

    collecting_ = true;
    collectSkipped_.clear();
    collectCurrent_.clear();
    collectPending_.clear();
    collectOpened_.clear();
    collectTotal_ = needed;
    collectLearned_ = 0;
    collectAccountRevealShown_ = false;
    collectLastSequence_ = ::GetClipboardSequenceNumber();  // what is on the clipboard now isn't an answer
    if (const auto desktop = desktopDirectory(); !desktop.empty()) {
        collectLinksLogPath_ = desktop / "GTH-Prosperopatches-links.txt";
    }
    {
        std::ofstream output(collectLinksLogPath_, std::ios::trunc);
        if (output) {
            output << "# GTH Prosperopatches link collection\r\n"
                      "# TITLE_ID\\tSONY_VERSION_LINK\r\n";
        }
    }
    lastAutoclickProgressAt_ = std::chrono::steady_clock::now();
    selectedGameToolsExpanded_ = true;
    skipGameButton_.ShowWindow(SW_SHOW);
    setBusy(false);
    SetTimer(kCollectTimerId, kCollectTickMs);
    SetTimer(kCollectWatchdogTimerId, kCollectWatchdogTickMs);
    sonyLinkCopyWatcher_.Start(m_hWnd, kSonyLinkCopiedMessage, sonyLinksPath_.parent_path() / "sony-sc-links-copied.txt");
    patchAutoClicker_.Start(m_hWnd, kPatchAutoClickMessage);

    GTH_LOG_INFO("Collecting Sony links for {} game(s)", needed);
    collectNext();
}

void UpdatesView::showCollectStatus(const std::wstring& lead) {
    std::wstring title = Utf8ToWide(collectCurrent_);
    for (const auto& game : ps5Games()) {
        if (game.first == collectCurrent_) title = std::format(L"{} ({})", Utf8ToWide(game.second), Utf8ToWide(game.first));
    }
    const int position = collectLearned_ + static_cast<int>(collectSkipped_.size()) + 1;
    std::wstring text = std::format(L"{}Collecting link {} / {}: {}. Open the newest patch and copy its SC link; Skip skips it; Cancel stops.",
                                    lead, position, collectTotal_, title);
    statusLabel_.SetWindowText(text.c_str());
}

void UpdatesView::collectNext(const std::wstring& lead) {
    const auto known = gth::network::ReadSonyLinkTitleIds(linkFiles());
    const auto games = ps5Games();
    const auto next = gth::application::NextGameNeedingLink(games, known, collectSkipped_);
    if (!next) {
        finishCollecting(std::format(L"{}Every game is covered or skipped.", lead), /*completed=*/true);
        return;
    }
    collectCurrent_ = next->first;

    // A title can end up marked "opened" here even though its actual tab is long gone:
    // PatchAutoClicker gives up on a title after enough failed attempts ("needs you") without
    // ever getting its link, and that tab is never closed by anything - the 15-tab cap
    // deliberately never touches a tab with no link copied yet, so it just sits there until the
    // person closes it themselves or the browser restarts. Once that happens, without this check
    // the game would never get a tab again for the rest of the session - collectOpened_ already
    // has its title ID, so the loop below would silently skip reopening it forever, and the
    // session just spins on whatever else is left. Reconciling against what SonyLinkCopyWatcher's
    // own scan actually saw open on its last tick catches exactly that: anything collectOpened_
    // believes is open, that isn't open and still has no link, is eligible to be opened again. An
    // empty snapshot (the watcher hasn't ticked yet, e.g. right after Start()) is left alone
    // rather than read as "everything closed" - that would reopen every game at once.
    if (const auto openNow = sonyLinkCopyWatcher_.CurrentlyOpenTitleIds(); !openNow.empty()) {
        const std::set<std::string> stillOpen(openNow.begin(), openNow.end());
        for (auto it = collectOpened_.begin(); it != collectOpened_.end();) {
            if (stillOpen.contains(*it) || known.contains(*it)) {
                ++it;
            } else {
                it = collectOpened_.erase(it);
            }
        }
    }

    // Open this game's page (kept to one at a time - several open together was cheaper for a
    // person to click through, but far more prone to tab-count/cursor-contention bugs than the
    // benefit was worth). Links are matched to games by the title ID inside them, so the order
    // they are clicked in doesn't matter.
    constexpr int kPagesAhead = 1;
    std::set<std::string> passed = collectSkipped_;
    for (int i = 0; i < kPagesAhead; ++i) {
        const auto game = gth::application::NextGameNeedingLink(games, known, passed);
        if (!game) break;
        passed.insert(game->first);
        if (collectOpened_.insert(game->first).second) {
            openUrl_(L"https://prosperopatches.com/" + Utf8ToWide(game->first));
            // Show which Google account is active, once per session on the first page only.
            // Firing this on every page (as it used to) drives the real mouse cursor for ~2-3
            // seconds each time, at unpredictable moments throughout the session - including
            // while the person is mid-click opening a patch elsewhere, which can yank their
            // cursor away and corrupt that click. The account doesn't change mid-session, so
            // once, before any clicking starts, covers the actual need.
            if (!collectAccountRevealShown_) {
                collectAccountRevealShown_ = true;
                CaptchaContextService::Instance().ShowAccountAndWiFiOnCaptcha();
            }
        }
    }

    showCollectStatus(lead);
}

void UpdatesView::finishCollecting(const std::wstring& how, bool completed) {
    KillTimer(kCollectTimerId);
    KillTimer(kCollectWatchdogTimerId);
    collecting_ = false;
    collectPending_.clear();
    // The session is over either way (finished or cancelled) - close every tab it opened rather
    // than leaving a browser full of tracker pages behind. See Stop()'s own doc comment.
    sonyLinkCopyWatcher_.Stop(/*closeAllTabs=*/true);
    patchAutoClicker_.Stop();
    skipGameButton_.ShowWindow(SW_HIDE);
    liveRows_.clear();  // drop any "needs you"/"waiting for its link" row markers from this session
    refresh();

    setBusy(anythingRunning());

    const std::wstring status =
        std::format(L"{} Saved {} link(s), skipped {} game(s).", how, collectLearned_, collectSkipped_.size());
    statusLabel_.SetWindowText(status.c_str());
    GTH_LOG_INFO("Collecting Sony links finished: {}", WideToUtf8(status));

    // The press of "Update All Games" that led here carries on: the new links go straight into the line. Only
    // when the session ran to its end - Cancel means stop, and a stop shouldn't start gigabytes of downloads.
    const bool carryOn = updateAllAfterCollect_ && completed && collectLearned_ > 0 && !anythingRunning();
    updateAllAfterCollect_ = false;
    if (carryOn) {
        updateAllChain_ = false;  // the second line doesn't offer the collection again
        startSync("", gth::application::SyncMode::Full, /*automatic=*/false);
        if (syncInProgress_.load()) {
            statusLabel_.SetWindowText(std::format(L"{}\r\nUpdating the games that just got a link...", status).c_str());
        }
        return;
    }

    // With new links in place the check has something to say.
    if (collectLearned_ > 0 && !anythingRunning()) {
        BOOL handled = TRUE;
        OnCheckUpdatesClick(0, 0, nullptr, handled);
        if (updateInProgress_.load()) statusLabel_.SetWindowText(std::format(L"{}\r\nChecking those games against Sony now...", status).c_str());
    }
}

void UpdatesView::collectTick() {
    if (!collecting_) return;

    // Always watch the clipboard for something new, regardless of whether an earlier catch is
    // still waiting its turn in the queue below - otherwise clicking through a second and third
    // patch while the first one's game is off downloading would have nowhere to land.
    const DWORD sequence = ::GetClipboardSequenceNumber();
    if (sequence != collectLastSequence_) {
        collectLastSequence_ = sequence;
        if (const auto clipboard = readClipboardText(m_hWnd);
            clipboard.has_value() && !gth::network::ExtractSonyAddresses(*clipboard).empty()) {
            collectPending_.push_back(*clipboard);
        }
    }

    if (collectPending_.empty()) return;
    // If another job is running, leave it at the front of the queue and try again a moment later.
    if (startLinkImport(collectPending_.front(), /*guided=*/true)) collectPending_.pop_front();
}

void UpdatesView::onGuidedImportDone(const LinksJobOutcome& outcome) {
    const auto& result = outcome.importResult;
    collectLearned_ += static_cast<int>(result.learned);

    if (result.saveFailed) {
        finishCollecting(std::format(L"Couldn't write the links to {} - stopped.", sonyLinksPath_.wstring()));
        return;
    }
    if (result.learned > 0) {
        appendCollectedLinksToDesktop(result.titleIdsLearned);
        std::wstring ids;
        for (size_t i = 0; i < result.titleIdsLearned.size() && i < 4; ++i) ids += (i ? L", " : L"") + Utf8ToWide(result.titleIdsLearned[i]);
        for (const auto& titleId : result.titleIdsLearned) {
            queueAutoDownload(titleId);
            liveRows_.erase(titleId);  // resolved - stop showing any "needs you"/"waiting" marker for it
        }
        refresh();  // clears those rows' markers immediately rather than waiting for the next unrelated refresh
        collectNext(std::format(L"Saved the link for {}. ", ids));
        return;
    }
    if (result.failed + result.gamePiecesOnly > 0 && !result.notes.empty()) {
        // Nothing usable: say why, and keep waiting for the same game.
        showCollectStatus(std::format(L"{}\r\n", Utf8ToWide(result.notes.front())));
        return;
    }
    // Already known (the person copied a game that has a link): carry on with whatever is next.
    collectNext(L"That game already had a link. ");
}

std::string UpdatesView::variantIdForTitleId(const std::string& titleId) const {
    for (const auto& entry : entries_) {
        if (entry.titleId == titleId && entry.variantStatus == gth::core::GameVariantStatus::Active) return entry.gameVariantId;
    }
    return {};
}

void UpdatesView::queueAutoDownload(const std::string& titleId) {
    const auto variantId = variantIdForTitleId(titleId);
    if (variantId.empty()) return;  // not in the library under this exact title ID - nothing to queue yet

    // Never queue the same game twice - clicking the same patch again, or re-collecting a link
    // the archive already has, would otherwise queue a second, redundant download of it. The
    // sync itself would find nothing to do the second time (it always checks Sony's version
    // before downloading anything), but there's no reason to burn a second check on it, and a
    // duplicate row in the Downloads queue would just be confusing.
    if (const auto live = shared_->liveDownloads.find(titleId); live != shared_->liveDownloads.end()) return;
    for (const auto& queued : shared_->queuedDownloads) {
        if (queued.gameVariantId == variantId) return;
    }

    std::string title = titleId;
    for (const auto& entry : entries_) {
        if (entry.titleId == titleId && !entry.title.empty()) {
            title = entry.title;
            break;
        }
    }
    shared_->queuedDownloads.push_back({variantId, titleId, title});
    tryStartNextAutoDownload();
}

void UpdatesView::tryStartNextAutoDownload() {
    if (shared_->queuedDownloads.empty() || anythingRunning()) return;
    const auto next = shared_->queuedDownloads.front();
    shared_->queuedDownloads.pop_front();
    // checkAfter=false: re-asking Sony about the whole library after every single newly-gathered
    // link would turn a guided session into dozens of full library checks instead of one.
    startSync(next.gameVariantId, gth::application::SyncMode::Full, /*automatic=*/false, /*checkAfter=*/false);
}

void UpdatesView::showToolsMenu(POINT screenPoint) {
    const bool idle = !anythingRunning() && !collecting_;
    const auto* selected = selectedEntry();
    HMENU menu = ::CreatePopupMenu();
    const auto item = [&](UINT id, const wchar_t* text, bool enabled) {
        ::AppendMenuW(menu, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), id, text);
    };
    item(IDC_UPDATES_SONY_LINKS_BUTTON, L"Sony update links...", idle);
    item(IDC_UPDATES_IMPORT_VERSIONS_BUTTON, L"Import a list of installed versions...", idle);
    item(IDC_UPDATES_OPEN_TRACKER_BUTTON, L"Open the selected game's tracker page",
         selected != nullptr && !selected->titleId.empty());
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    item(IDC_UPDATES_GUIDE_BUTTON, L"How this tab works", true);

    // This menu is opened from the board's context menu, keeping secondary
    // tools available without another permanent button in the page header.
    ::TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, screenPoint.x, screenPoint.y, 0, m_hWnd, nullptr);
    ::DestroyMenu(menu);
}

LRESULT UpdatesView::OnSkipGameClick(WORD, WORD, HWND, BOOL&) {
    if (!collecting_) return 0;
    if (!collectCurrent_.empty()) collectSkipped_.insert(collectCurrent_);
    collectNext(L"Skipped. ");
    return 0;
}

LRESULT UpdatesView::OnLinksProgress(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto line = gth::app::detail::takeOwnedMessage<std::wstring>(lParam);
    if (linksInProgress_.load()) statusLabel_.SetWindowText(line->c_str());
    return 0;
}

LRESULT UpdatesView::OnLinksDone(UINT, WPARAM, LPARAM lParam, BOOL&) {
    auto outcome = gth::app::detail::takeOwnedMessage<LinksJobOutcome>(lParam);
    linksInProgress_.store(false);
    shared_->endJob();
    setBusy(false);
    tryStartNextAutoDownload();  // the one-job slot just freed - let a queued game take it

    if (outcome->imported && outcome->guided && collecting_) {
        onGuidedImportDone(*outcome);
        return 0;
    }

    if (outcome->imported) {
        const auto& result = outcome->importResult;
        std::wstring status = std::format(L"Read {} Sony address(es): {} new link(s) saved, {} already known",
                                          result.addressesFound, result.learned, result.alreadyKnown);
        if (result.failed + result.gamePiecesOnly > 0) {
            status += std::format(L", {} couldn't be used", result.failed + result.gamePiecesOnly);
        }
        status += result.cancelled ? L" (cancelled). " : L". ";
        if (result.saveFailed) status += L"Couldn't write the links to " + sonyLinksPath_.wstring() + L". ";
        status += std::format(L"Now {} of your {} PS5 games have a Sony link.", outcome->covered,
                              outcome->ps5Games);
        statusLabel_.SetWindowText(status.c_str());
        GTH_LOG_INFO("Sony address import: {} | {}", WideToUtf8(status), result.notes.empty() ? "" : result.notes.front());

        if (!result.notes.empty()) {
            std::wstring detail = L"These addresses couldn't be turned into a link:\r\n\r\n";
            for (size_t i = 0; i < result.notes.size() && i < 12; ++i) detail += L"- " + Utf8ToWide(result.notes[i]) + L"\r\n";
            if (result.notes.size() > 12) {
                detail += std::format(L"... and {} more (see the log).\r\n", result.notes.size() - 12);
            }
            for (size_t i = 12; i < result.notes.size(); ++i) GTH_LOG_INFO("Sony address import: {}", result.notes[i]);
            ::MessageBoxW(m_hWnd, detail.c_str(), L"Add links", MB_OK | MB_ICONINFORMATION);
        }

        if (result.learned > 0) {
            BOOL handled = TRUE;
            OnCheckUpdatesClick(0, 0, nullptr, handled);
            if (updateInProgress_.load()) statusLabel_.SetWindowText(std::format(L"{}\r\nChecking those games against Sony now...", status).c_str());
        }
        return 0;
    }

    std::wstring status;
    if (outcome->ok) {
        status = std::format(L"Downloaded {} Sony links ({} saved in total). They cover {} of your {} PS5 games",
                             outcome->downloaded, outcome->total, outcome->covered, outcome->ps5Games);
        const int missing = outcome->ps5Games - outcome->covered;
        if (missing > 0) {
            status += std::format(
                L"; {} still need a link (the public list only has the titles somebody has shared - right-click the updates grid and choose Sony update links > "
                L"Add links from the clipboard with each game's SC link; the Guide explains where to copy it).",
                missing);
        } else {
            status += L".";
        }
    } else {
        status = L"Couldn't download the public Sony link list: " + Utf8ToWide(outcome->error);
    }
    statusLabel_.SetWindowText(status.c_str());
    GTH_LOG_INFO("Sony link list: {}", WideToUtf8(status));

    // With links in place the check has something to say - run it straight away.
    if (outcome->ok && outcome->covered > 0) {
        BOOL handled = TRUE;
        OnCheckUpdatesClick(0, 0, nullptr, handled);
        if (updateInProgress_.load()) {
            statusLabel_.SetWindowText(std::format(L"{}\r\nChecking those games against Sony now...", status).c_str());
        }
    }
    return 0;
}

void UpdatesView::applyVersionToSelected(const std::string& version) {
    const auto* entry = selectedEntry();
    if (!entry || !indexer_ || anythingRunning()) return;

    const std::string variantId = entry->gameVariantId;
    const std::string title = entry->title;
    const auto result = indexer_->setVersion(variantId, version);
    if (!result.success) {
        statusLabel_.SetWindowText(std::format(L"Couldn't save the version for {}: {}", Utf8ToWide(title),
                                               Utf8ToWide(result.errorMessage)).c_str());
        GTH_LOG_WARN("Set version for '{}' failed: {}", title, result.errorMessage);
        return;
    }

    refresh();
    selectVariant(variantId);
    const std::wstring message = version.empty()
        ? std::format(L"Cleared the version for {} - it will be detected again at the next scan.", Utf8ToWide(title))
        : std::format(L"Saved version {} for {}, next to its files, so a rescan keeps it.", Utf8ToWide(version),
                      Utf8ToWide(title));
    statusLabel_.SetWindowText(message.c_str());
    GTH_LOG_INFO("Set version for '{}' to '{}'", title, version);
}

LRESULT UpdatesView::OnSetVersionClick(WORD, WORD, HWND, BOOL&) {
    std::array<wchar_t, 64> text{};
    versionEdit_.GetWindowText(text.data(), static_cast<int>(text.size()));
    applyVersionToSelected(WideToUtf8(text.data()));
    return 0;
}

LRESULT UpdatesView::OnResetVersionClick(WORD, WORD, HWND, BOOL&) {
    applyVersionToSelected("");
    return 0;
}

LRESULT UpdatesView::OnOpenTrackerClick(WORD, WORD, HWND, BOOL&) {
    const auto* entry = selectedEntry();
    if (!entry || entry->titleId.empty()) return 0;

    // The tracker lists a game's patches for a person to read; it's the quick
    // way to see which versions exist when a game has no Sony link yet.
    const std::wstring site = entry->platform == "PS4" ? L"https://orbispatches.com/" : L"https://prosperopatches.com/";
    ::ShellExecuteW(m_hWnd, L"open", (site + Utf8ToWide(entry->titleId)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return 0;
}

void UpdatesView::importSonyLinks(const std::string& text, std::wstring& report) {
    const auto merged = gth::network::MergeSonyVersionLinks(sonyLinksPath_, text);
    if (merged.incoming == 0) return;

    if (merged.written) {
        report += std::format(L"Saved {} Sony version link(s) - {} in total.\r\n", merged.incoming, merged.total);
    } else {
        report += L"Couldn't write the Sony links to " + sonyLinksPath_.wstring() + L".\r\n";
    }
}

LRESULT UpdatesView::OnImportVersionsClick(WORD, WORD, HWND, BOOL&) {
    if (!indexer_ || anythingRunning()) return 0;

    std::array<wchar_t, MAX_PATH> file{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = m_hWnd;
    dialog.lpstrFile = file.data();
    dialog.nMaxFile = static_cast<DWORD>(file.size());
    dialog.lpstrTitle = L"Import versions or Sony links";
    dialog.lpstrFilter = L"Lists (*.txt;*.csv;*.tsv)\0*.txt;*.csv;*.tsv\0All files (*.*)\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!::GetOpenFileNameW(&dialog)) return 0;

    WaitCursor wait;
    const std::string text = readListFile(file.data());

    std::wstring report;
    importSonyLinks(text, report);
    const bool importedLinks = !report.empty();

    const auto result = indexer_->importVersions(text);
    if (result.applied > 0 || result.failed > 0 || !importedLinks) {
        report += std::format(L"Set the version of {} game(s), next to their files.\r\n", result.applied);
    }
    if (!result.unmatchedTitleIds.empty()) {
        std::wstring first;
        for (size_t i = 0; i < result.unmatchedTitleIds.size() && i < 5; ++i) {
            first += (i ? L", " : L"") + Utf8ToWide(result.unmatchedTitleIds[i]);
        }
        report += std::format(L"{} title ID(s) in the list aren't in this library ({}{}).\r\n",
                              result.unmatchedTitleIds.size(), first,
                              result.unmatchedTitleIds.size() > 5 ? L", ..." : L"");
    }
    if (result.failed > 0) {
        report += std::format(L"{} couldn't be saved - first: {}\r\n", result.failed, Utf8ToWide(result.firstError));
    }
    if (result.linesIgnored > 0 && !importedLinks) {
        report += std::format(L"{} line(s) had no recognizable title ID and version.\r\n", result.linesIgnored);
    }

    refresh();
    GTH_LOG_INFO("Import list: {}", WideToUtf8(report));
    ::MessageBoxW(m_hWnd, report.c_str(), L"Import list", MB_OK | MB_ICONINFORMATION);
    return 0;
}
