#include "UpdatesView.h"

#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>

#include "gth/application/indexing/ContentIds.h"
#include "gth/core/enums/GameVariantStatus.h"
#include "gth/logging/Logging.h"
#include "gth/network/SonyUpdateService.h"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
constexpr int kMargin = 24;

// Header, top to bottom: the main buttons, the three automation switches, the
// version tools, then the status text; the list fills what is left.
constexpr int kRowHeight = 28;
constexpr int kMainRowTop = 24;
constexpr int kSwitchRowTop = kMainRowTop + kRowHeight + 10;
constexpr int kVersionRowTop = kSwitchRowTop + kRowHeight + 6;
constexpr int kStatusTop = kVersionRowTop + kRowHeight + 8;
constexpr int kStatusHeight = 88;  // five lines: the note about a finished run can be long
constexpr int kListTop = kStatusTop + kStatusHeight + 8;

// Once a minute: asks whether the Sony automation is due (see IsSonyAutomationDue).
constexpr UINT_PTR kAutomationTimerId = 2;
constexpr UINT kAutomationTickMs = 60 * 1000;

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
    wchar_t buffer[48];
    if (bytes >= 1024LL * 1024 * 1024) {
        swprintf_s(buffer, L"%.2f GB", bytes / 1024.0 / 1024.0 / 1024.0);
    } else {
        swprintf_s(buffer, L"%.1f MB", bytes / 1024.0 / 1024.0);
    }
    return buffer;
}

// The list's own text for a game's answer from Sony.
std::wstring updateStateText(const gth::application::UpdateCheckEntry& entry) {
    using gth::application::UpdateState;
    const wchar_t* label = L"";
    switch (entry.state) {
        case UpdateState::UpToDate: label = L"Up to date"; break;
        case UpdateState::UpdateAvailable: label = L"Update available"; break;
        case UpdateState::InstalledVersionUnknown: label = L"Version unknown"; break;
        case UpdateState::NoSonyData: label = L"No Sony link"; break;
        case UpdateState::CheckFailed: label = L"Check failed"; break;
    }
    std::wstring text = label;
    if (!entry.detail.empty()) text += L" - " + Utf8ToWide(entry.detail);
    return text;
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
        const std::wstring wide(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
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

LRESULT UpdatesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CRect titleRect(kMargin, kMainRowTop - 2, kMargin + 170, kMainRowTop + 36);
    titleLabel_.Create(m_hWnd, titleRect, L"Updates", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    // Row 1: the actions.
    int x = kMargin + 190;
    const auto placeButton = [&](ModernButton& button, int rowTop, const wchar_t* text, int width, UINT id,
                                 ModernButton::Kind kind) {
        CRect rect(x, rowTop, x + width, rowTop + kRowHeight);
        button.Create(m_hWnd, rect, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, id);
        button.SetFont(theme::BodyFont());
        button.setKind(kind);
        x += width + 8;
    };
    placeButton(downloadAllButton_, kMainRowTop, L"Update All Games", 176, IDC_UPDATES_DOWNLOAD_ALL_BUTTON,
                ModernButton::Kind::Primary);
    placeButton(checkUpdatesButton_, kMainRowTop, L"Check Sony for Updates", 176, IDC_UPDATES_CHECK_SONY_BUTTON,
                ModernButton::Kind::Secondary);
    placeButton(cancelSyncButton_, kMainRowTop, L"Cancel", 70, IDC_UPDATES_CANCEL_SYNC_BUTTON, ModernButton::Kind::Danger);
    placeButton(moreButton_, kMainRowTop, L"More \x25BE", 84, IDC_UPDATES_MORE_BUTTON, ModernButton::Kind::Subtle);

    // Row 2: what runs by itself.
    x = kMargin;
    CRect automaticRect(x, kSwitchRowTop + 5, x + 118, kSwitchRowTop + kRowHeight);
    automaticCaption_.Create(m_hWnd, automaticRect, L"Run automatically:", WS_CHILD | WS_VISIBLE);
    automaticCaption_.SetFont(theme::BodyFont());
    x += 122;
    const auto placeCheckBox = [&](ModernCheckBox& box, const wchar_t* text, int width, UINT id) {
        CRect rect(x, kSwitchRowTop + 2, x + width, kSwitchRowTop + kRowHeight - 2);
        box.Create(m_hWnd, rect, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, id);
        box.SetFont(theme::BodyFont());
        x += width + 8;
    };
    placeCheckBox(autoCheckBox_, L"Check for updates", 150, IDC_UPDATES_AUTO_CHECK_CHECKBOX);
    placeCheckBox(autoMetadataBox_, L"Add missing metadata (small)", 210, IDC_UPDATES_AUTO_METADATA_CHECKBOX);
    placeCheckBox(autoUpdatesBox_, L"Download updates (many GB)", 200, IDC_UPDATES_AUTO_UPDATES_CHECKBOX);
    placeCheckBox(autoScanBox_, L"Find new games first", 170, IDC_UPDATES_AUTO_SCAN_CHECKBOX);
    applyAutomationSettingsToControls();

    // Row 3: everything that acts on the game selected in the list.
    x = kMargin;
    CRect captionRect(x, kVersionRowTop + 5, x + 100, kVersionRowTop + kRowHeight);
    versionCaption_.Create(m_hWnd, captionRect, L"Selected game:", WS_CHILD | WS_VISIBLE);
    versionCaption_.SetFont(theme::BodyFont());
    x += 104;

    CRect editRect(x, kVersionRowTop, x + 110, kVersionRowTop + kRowHeight);
    versionEdit_.Create(m_hWnd, editRect, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0,
                        IDC_UPDATES_VERSION_EDIT);
    versionEdit_.SetFont(theme::BodyFont());
    versionEdit_.SendMessage(EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"01.300.000"));
    x += 118;

    placeButton(setVersionButton_, kVersionRowTop, L"Set", 56, IDC_UPDATES_SET_VERSION_BUTTON, ModernButton::Kind::Secondary);
    placeButton(resetVersionButton_, kVersionRowTop, L"Reset", 64, IDC_UPDATES_RESET_VERSION_BUTTON,
                ModernButton::Kind::Subtle);
    placeButton(syncSelectedButton_, kVersionRowTop, L"Sync with Sony", 130, IDC_UPDATES_SYNC_SELECTED_BUTTON,
                ModernButton::Kind::Secondary);
    placeButton(skipGameButton_, kVersionRowTop, L"Skip Game", 100, IDC_UPDATES_SKIP_GAME_BUTTON,
                ModernButton::Kind::Secondary);
    skipGameButton_.ShowWindow(SW_HIDE);  // only while collecting links

    CRect statusRect(kMargin, kStatusTop, kMargin + 900, kStatusTop + kStatusHeight);
    // What the last run did is worth seeing the moment the tab opens.
    const std::string lastRun = gth::application::LastSonyRunSummary(reportPath_);
    const std::wstring opening = lastRun.empty() ? std::wstring(L"Press Guide for a short walkthrough.")
                                                 : L"Last run - " + Utf8ToWide(lastRun);
    statusLabel_.Create(m_hWnd, statusRect, opening.c_str(), WS_CHILD | WS_VISIBLE);
    statusLabel_.SetFont(theme::BodyFont());

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(kMargin, kListTop, clientRect.right - kMargin, clientRect.bottom - kMargin);
    // LVS_ICON: a poster grid (box art on top, its label wrapped beneath), matching the Games
    // tab and the Sony storefronts both are modeled on - see composeLabel() for what the label's
    // three lines show (title, version/size, and this game's live status while a job is running).
    listView_.Create(m_hWnd, listRect, L"",
                      WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_ICON | LVS_AUTOARRANGE | LVS_SINGLESEL, 0,
                      IDC_UPDATES_LISTVIEW);
    listView_.SetFont(theme::BodyFont());
    theme::ApplyListViewTheme(listView_);
    listView_.SetIconSpacing(PosterCache::kTileWidth + 24, PosterCache::kTileHeight + 48);
    if (posterCache_) posterCache_->subscribe(m_hWnd);

    refresh();
    setBusy(false);  // sets every button's enabled state, including Cancel (off until a sync runs)
    SetTimer(kAutomationTimerId, kAutomationTickMs);
    return 0;
}

LRESULT UpdatesView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    KillTimer(kAutomationTimerId);
    KillTimer(kCollectTimerId);
    KillTimer(kCollectWatchdogTimerId);
    syncCancel_.store(true);  // a sync in flight stops at its next check instead of holding shutdown up
    linksCancel_.store(true);
    // Joining here briefly delays shutdown rather than risking a detached
    // thread touching this object's members after it's destroyed.
    if (updateThread_.joinable()) updateThread_.join();
    if (syncThread_.joinable()) syncThread_.join();
    if (linksThread_.joinable()) linksThread_.join();
    bHandled = FALSE;
    return 0;
}

LRESULT UpdatesView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    if (statusLabel_.IsWindow()) {
        statusLabel_.MoveWindow(kMargin, kStatusTop, std::max(200, width - 2 * kMargin), kStatusHeight);
    }
    if (listView_.IsWindow()) {
        listView_.MoveWindow(kMargin, kListTop, std::max(100, width - 2 * kMargin),
                              std::max(100, height - kListTop - kMargin));
    }
    return 0;
}

LRESULT UpdatesView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorStatic(wParam); }

bool UpdatesView::anythingRunning() const { return shared_ != nullptr && shared_->jobRunning(); }

void UpdatesView::setBusy(bool busy) {
    // A collection session keeps the other buttons off between its imports; only Skip Game and Cancel stay live.
    const BOOL enabled = (busy || collecting_) ? FALSE : TRUE;
    checkUpdatesButton_.EnableWindow(enabled);
    downloadAllButton_.EnableWindow(enabled);
    cancelSyncButton_.EnableWindow(syncInProgress_.load() || linksInProgress_.load() || collecting_ ? TRUE : FALSE);
    if (skipGameButton_.IsWindow()) skipGameButton_.EnableWindow(collecting_ ? TRUE : FALSE);
    updateVersionControls();
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
}

void UpdatesView::refresh() {
    if (!library_ || !listView_.IsWindow()) return;

    // Keep the same game selected across a refresh (setting a version
    // refreshes the list, and losing your place every time would be maddening).
    std::string keepSelected;
    if (const auto* selected = selectedEntry()) keepSelected = selected->gameVariantId;

    theme::ApplyListViewTheme(listView_);
    listView_.SetRedraw(FALSE);  // a library of thousands would repaint after every row otherwise
    listView_.DeleteAllItems();
    rowsByTitle_.clear();

    // Only games whose files are there: a game that has gone missing can't be updated.
    entries_.clear();
    for (auto& entry : library_->getAllEntries()) {
        if (entry.variantStatus == gth::core::GameVariantStatus::Active) entries_.push_back(std::move(entry));
    }

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

        const int imageIndex = posterCache_
            ? posterImageList_.Add(posterCache_->posterFor(entry.titleId, entry.title, entry.platform,
                                                            posterService_, entry.gameVariantId))
            : -1;
        listView_.InsertItem(row, composeLabel(entry).c_str(), imageIndex);
        row++;
    }

    listView_.SetRedraw(TRUE);
    listView_.Invalidate();

    if (!keepSelected.empty()) selectVariant(keepSelected);
    updateVersionControls();
    setBusy(anythingRunning());  // a job started on the Games tab keeps these buttons off too
}

std::wstring UpdatesView::composeLabel(const gth::application::GameLibraryEntry& entry) const {
    // A poster tile's label only has room for two short lines (title, then this one) - not the
    // report view's five columns, and not liveRows_'s own full sentences either (those still go
    // to statusLabel_ in full, unchanged - see OnPatchAutoClick/OnSyncRow). A running job's own
    // words win over the update-check verdict, same priority order the old "Update" column had.
    if (const auto live = liveRows_.find(entry.titleId); live != liveRows_.end()) {
        return Utf8ToWide(entry.title) + L"\r\n" + live->second;
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

    return Utf8ToWide(entry.title) + L"\r\n" + line2;
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
    if (wParam != kAutomationTimerId) return 0;
    if (collecting_ || !packageSync_ || anythingRunning()) return 0;

    const auto now = std::chrono::steady_clock::now();
    if (gth::application::IsSonyAutomationDue(automationSettings_, appStarted_, lastAutomationRun_, now)) {
        lastAutomationRun_ = now;
        GTH_LOG_INFO("Sony automation starting");
        startSync("", automationSettings_.downloadUpdates ? gth::application::SyncMode::Full
                                                          : gth::application::SyncMode::MetadataOnly,
                  /*automatic=*/true);
    }
    return 0;
}

LRESULT UpdatesView::OnGuideClick(WORD, WORD, HWND, BOOL&) {
    const std::wstring guide =
        L"HOW THE UPDATE TOOLS WORK\r\n\r\n"
        L"1. Scan first (Games tab > Scan All Storages), so the app knows your games.\r\n\r\n"
        L"2. More > Sony update links > Download the public Sony link list. A PS5 game can only be checked once the app has a "
        L"Sony \"version-file link\" for it; the public list has many older titles.\r\n\r\n"
        L"3. Check Sony for Updates. Each game gets a status:\r\n"
        L"    Up to date - it matches Sony's latest package.\r\n"
        L"    Update available - your files are older than Sony's latest.\r\n"
        L"    Version unknown - Sony's latest is known, but not which version you have.\r\n"
        L"    No Sony link - the app has no link for this game yet (step 4).\r\n\r\n"
        L"4. For games with no link: More > Sony update links > Collect links from prosperopatches.com. The app opens the first "
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
        L"6. The boxes are optional: the first three let the app do the same thing by itself every 12 hours while it is "
        L"open (downloading updates stays off unless you turn it on); \"Find new games first\" is the scan that "
        L"Update All Games starts with.\r\n\r\n"
        L"7. Version: select a game, type its version and press Set (saved next to its files); Reset goes back to "
        L"automatic. Import List loads \"TITLEID, version\" lines.";
    ::MessageBoxW(m_hWnd, guide.c_str(), L"Updates - guide", MB_OK | MB_ICONINFORMATION);
    return 0;
}

const gth::application::GameLibraryEntry* UpdatesView::selectedEntry() const {
    if (!listView_.IsWindow()) return nullptr;
    const int row = const_cast<CListViewCtrl&>(listView_).GetSelectedIndex();
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
    const auto* change = reinterpret_cast<LPNMLISTVIEW>(pnmh);
    if ((change->uChanged & LVIF_STATE) != 0 && ((change->uNewState ^ change->uOldState) & LVIS_SELECTED) != 0) {
        updateVersionControls();
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

    updateThread_ = std::thread([this, hwnd = m_hWnd] {
        checkLearnedLinks_.store(static_cast<int>(learnLinksFromArchive()));
        auto* summary = new gth::application::UpdateCheckSummary(updateChecker_->checkAll());
        ::PostMessageW(hwnd, kUpdateCheckDoneMessage, 0, reinterpret_cast<LPARAM>(summary));
    });
    return 0;
}

LRESULT UpdatesView::OnUpdateCheckDone(UINT, WPARAM, LPARAM lParam, BOOL&) {
    std::unique_ptr<gth::application::UpdateCheckSummary> summary(
        reinterpret_cast<gth::application::UpdateCheckSummary*>(lParam));
    updateInProgress_.store(false);
    shared_->endJob();

    shared_->updateByVariant.clear();
    for (const auto& entry : summary->entries) shared_->updateByVariant[entry.gameVariantId] = entry;
    setBusy(false);
    refresh();

    std::wstring status = describeCheck(*summary, sonyLinksPath_);
    if (const int learned = checkLearnedLinks_.exchange(0); learned > 0) {
        status = L"Read " + std::to_wstring(learned) + L" Sony link(s) out of metadata pieces already in the archive.\r\n" + status;
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
    wchar_t line[320];
    swprintf_s(line, L"Sony check of %d game(s): %d up to date, %d with an update available, %d version unknown, "
                     L"%d without a Sony link, %d failed.",
               static_cast<int>(summary.entries.size()), summary.upToDate, summary.updatesAvailable,
               summary.installedUnknown, summary.noSonyData, summary.failed);
    std::wstring status = line;
    if (summary.noSonyData > 0) {
        status += L"\r\n" + std::to_wstring(summary.noSonyData) +
                  L" PS5 game(s) have no Sony link yet: More > Sony update links > Add links from the clipboard (copy each game's "
                  L"SC link from its prosperopatches.com page - see Guide), or download the public list (links are "
                  L"kept in " + sonyLinksPath.wstring() + L").";
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
    if (automationStore_ && !automationStore_->save(automationSettings_)) {
        statusLabel_.SetWindowText((L"Couldn't save the automation settings to " + automationStore_->path().wstring()).c_str());
    }
    return 0;
}

LRESULT UpdatesView::OnSyncSelectedClick(WORD, WORD, HWND, BOOL&) {
    const auto* entry = selectedEntry();
    if (!entry || !packageSync_ || anythingRunning()) return 0;

    const std::wstring question =
        L"Sync \"" + Utf8ToWide(entry->title) + L"\" with Sony?\r\n\r\n"
        L"This adds the small metadata piece the game is missing. If the game is out of date, it also downloads the pieces "
        L"that changed (can be many GB) and replaces them - each one only after it matches Sony's checksum, and nothing in "
        L"the archive is touched until then.";
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
        L"Games the app has no Sony link for can't be asked about; when the line is done it offers to collect those "
        L"links, and then carries on updating by itself.\r\n\r\n"
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

void UpdatesView::startSync(const std::string& gameVariantId, gth::application::SyncMode mode, bool automatic,
                            bool checkAfter) {
    if (!packageSync_ || !indexer_ || !shared_->tryBeginJob()) return;
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
    // Looking for new games is part of a press of "Update All Games"; an unattended run only does it when it is
    // going to download updates anyway - a background scan of every drive every few hours is not what it is for.
    const bool lookForNewGames = gameVariantId.empty() && automationSettings_.scanForNewGames &&
                                 (!automatic || automationSettings_.downloadUpdates);

    syncThread_ = std::thread([this, hwnd = m_hWnd, gameVariantId, mode, automatic, checkAfter, settings = automationSettings_,
                               titles = std::move(titles), lookForNewGames] {
        // A long download line must not be interrupted by Windows going to sleep on its own.
        struct KeepAwake {
            KeepAwake() { ::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED); }
            ~KeepAwake() { ::SetThreadExecutionState(ES_CONTINUOUS); }
        } keepAwake;

        const auto began = std::chrono::steady_clock::now();
        auto* outcome = new SyncJobOutcome;
        outcome->automatic = automatic;
        outcome->startedAt = std::time(nullptr);

        auto names = titles;  // grows when the scan below finds games the list didn't have
        const auto nameOf = [&](const std::string& titleId) {
            const auto found = names.find(titleId);
            return Utf8ToWide(found != names.end() && !found->second.empty() ? found->second : titleId);
        };
        const auto post = [&](const std::wstring& text) {
            ::PostMessageW(hwnd, kSyncProgressMessage, 0, reinterpret_cast<LPARAM>(new std::wstring(text)));
        };
        const auto postRow = [&](const std::string& titleId, const std::wstring& text) {
            ::PostMessageW(hwnd, kSyncRowMessage, 0, reinterpret_cast<LPARAM>(new RowUpdate{titleId, text}));
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
                                          ? L"Queued - adds the small metadata piece (" + formatSize(item.bytesToDownload) + L")"
                                          : L"Queued - at least " + formatSize(item.bytesToDownload) + L" to download");
            }
            post(L"The line has " + std::to_wstring(line.size()) + L" game(s), at least " + formatSize(total) +
                 L" to download. Starting...");
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
                wchar_t percent[24];
                swprintf_s(percent, L"  %d%%  ", static_cast<int>(100.0 * progress.bytesDone / progress.bytesTotal));
                amount = percent + formatSize(progress.bytesDone) + L" of " + formatSize(progress.bytesTotal);
            }
            if (progress.gameCount > 0) {
                gameStarted.try_emplace(progress.titleId, std::chrono::steady_clock::now());
                post(L"[" + std::to_wstring(progress.gameIndex) + L"/" + std::to_wstring(progress.gameCount) + L"] " +
                     nameOf(progress.titleId) + L": " + step + amount);
                postRow(progress.titleId, step + amount);
            } else {
                post(step + amount);
            }
        };

        callbacks.onGameDone = [&](const gth::application::SyncGameOutcome& game) {
            const std::wstring what = Utf8ToWide(game.message);
            if (game.skippedNeedsUpdate) {
                postRow(game.titleId, L"Not downloaded - " + what);
            } else if (game.success) {
                postRow(game.titleId, L"Done - " + what);
            } else {
                postRow(game.titleId, L"FAILED - " + what);
            }

            const auto outcome = game.skippedNeedsUpdate ? gth::application::DownloadHistoryOutcome::SkippedNeedsUpdate
                                 : game.success           ? gth::application::DownloadHistoryOutcome::Success
                                                           : gth::application::DownloadHistoryOutcome::Failed;
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
                post(L"Looking for new games - storage " + std::to_wstring(position) + L" of " + std::to_wstring(storages.size()) +
                     L": " + Utf8ToWide(storage.name));
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

            const auto result = outcome->single.cancelled           ? gth::application::DownloadHistoryOutcome::Cancelled
                                : outcome->single.skippedNeedsUpdate ? gth::application::DownloadHistoryOutcome::SkippedNeedsUpdate
                                : outcome->single.success            ? gth::application::DownloadHistoryOutcome::Success
                                                                      : gth::application::DownloadHistoryOutcome::Failed;
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
        ::PostMessageW(hwnd, kSyncDoneMessage, 0, reinterpret_cast<LPARAM>(outcome));
    });
}

LRESULT UpdatesView::OnSyncProgress(UINT, WPARAM, LPARAM lParam, BOOL&) {
    std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
    if (syncInProgress_.load()) statusLabel_.SetWindowText(text->c_str());  // a late line must not overwrite the result
    return 0;
}

LRESULT UpdatesView::OnSyncRow(UINT, WPARAM, LPARAM lParam, BOOL&) {
    std::unique_ptr<RowUpdate> update(reinterpret_cast<RowUpdate*>(lParam));
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
    std::unique_ptr<std::wstring> titleIdOwned(reinterpret_cast<std::wstring*>(lParam));
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
    std::unique_ptr<SyncJobOutcome> outcome(reinterpret_cast<SyncJobOutcome*>(lParam));
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
    }
    setBusy(false);
    refresh();

    std::wstring status;
    if (outcome->scannedForNewGames) {
        status = L"Looked for new games: " + std::to_wstring(outcome->newGamesFound) + L" found";
        if (outcome->storagesUnreachable > 0) {
            status += L" (" + std::to_wstring(outcome->storagesUnreachable) + L" storage(s) weren't reachable and were left as they are)";
        }
        status += L".";
    }
    if (outcome->linksLearnedFromFiles > 0) {
        status += (status.empty() ? L"" : L" ") + std::wstring(L"Read ") + std::to_wstring(outcome->linksLearnedFromFiles) +
                  L" Sony link(s) out of files already in the archive.";
    }
    if (!status.empty()) status += L"\r\n";
    if (outcome->oneGame) {
        status += Utf8ToWide(outcome->single.titleId) + L": " + Utf8ToWide(outcome->single.message);
    } else if (outcome->ranSync) {
        const auto& b = outcome->batch;
        wchar_t line[400];
        swprintf_s(line, L"Download line %s: %d updated, %d metadata added, %d already current, %d not downloaded, %d without a "
                         L"usable Sony link, %d failed. Downloaded %s in %d min.",
                   b.cancelled ? L"stopped" : L"finished", b.updated, b.completed, b.alreadyFine, b.needUpdate, b.unavailable,
                   b.failed, formatSize(b.bytesDownloaded).c_str(), static_cast<int>(outcome->took.count() / 60));
        status += line;
        if (b.recoveredOnRetry > 0) {
            status += L" " + std::to_wstring(b.recoveredOnRetry) + L" game(s) failed at first and worked on a retry.";
        }
        if (b.failed > 0) {
            status += L"\r\n" + std::to_wstring(b.failed) + L" game(s) failed (first: " + Utf8ToWide(b.firstFailure) +
                      L"). Press Update All Games again to retry - finished files are kept and downloads resume.";
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
    const std::wstring question =
        std::to_wstring(gamesWithoutLink) + L" game(s) have no Sony link, so the app can't tell whether they are up to "
        L"date.\r\n\r\n"
        L"Collect their links now? The app opens each game's page on prosperopatches.com in your browser, opens the "
        L"newest patch there for you and copies its link (solve the captcha if one shows up), then picks up the link "
        L"and opens the next game. When that is done, the games that just got a link are updated automatically.\r\n\r\n"
        L"Choose No to do this later from More > Sony update links > Collect links.";
    if (::MessageBoxW(m_hWnd, question.c_str(), L"Update All Games", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    startCollecting();
    updateAllAfterCollect_ = collecting_;
}

std::vector<std::filesystem::path> UpdatesView::linkFiles() const {
    std::vector<std::filesystem::path> files;
    wchar_t exePath[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        files.push_back(std::filesystem::path(exePath).parent_path() / "sony-version-links.tsv");
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
    moreButton_.GetWindowRect(&rect);
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
    statusLabel_.SetWindowText((std::to_wstring(needed.size()) + L" of " + std::to_wstring(ps5Games().size()) +
                                L" PS5 game(s) still need a Sony link. Opened " + neededPath.wstring() +
                                L" - put each game's version-file link at the end of its line, save, then press Check Sony "
                                L"for Updates.").c_str());
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

    linksThread_ = std::thread([this, hwnd = m_hWnd, titleIds, files = linkFiles(), target = sonyLinksPath_] {
        auto* outcome = new LinksJobOutcome;
        const auto fetch = gth::network::MakeSonyFetcher();
        const auto response = fetch(kPublicLinkListUrl);
        if (response.status == 0) {
            outcome->error = response.failure;
        } else if (response.status != 200) {
            outcome->error = "the server answered HTTP " + std::to_string(response.status);
        } else {
            const auto merged = gth::network::MergeSonyVersionLinks(target, response.body);
            if (merged.incoming == 0) {
                outcome->error = "the list had no links in it";
            } else if (!merged.written) {
                outcome->error = "couldn't save the links to " + target.string();
            } else {
                outcome->ok = true;
                outcome->downloaded = merged.incoming;
                outcome->total = merged.total;
            }
        }

        const auto known = gth::network::ReadSonyLinkTitleIds(files);
        outcome->ps5Games = static_cast<int>(titleIds.size());
        for (const auto& id : titleIds) outcome->covered += known.contains(id) ? 1 : 0;
        ::PostMessageW(hwnd, kLinksDoneMessage, 0, reinterpret_cast<LPARAM>(outcome));
    });
}

void UpdatesView::addLinksFromClipboard() {
    const std::string text = readClipboardText(m_hWnd).value_or("");
    if (gth::network::ExtractSonyAddresses(text).empty()) {
        ::MessageBoxW(m_hWnd,
                      L"The clipboard has no Sony addresses in it.\r\n\r\n"
                      L"Open the game's page on prosperopatches.com (Tracker Page opens it), click a patch, and copy the "
                      L"SC link (right-click the SC button > Copy link address) - the CRC or manifest link works too. "
                      L"Then choose this again. (More > Sony update links > Collect links from prosperopatches.com does the "
                      L"stepping from game to game for you.)",
                      L"Add links from the clipboard", MB_OK | MB_ICONINFORMATION);
        return;
    }
    startLinkImport(text);
}

void UpdatesView::addLinksFromFile() {
    if (anythingRunning()) return;

    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = m_hWnd;
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Add links from a file of Sony addresses";
    dialog.lpstrFilter = L"Lists (*.txt;*.csv;*.tsv;*.log)\0*.txt;*.csv;*.tsv;*.log\0All files (*.*)\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!::GetOpenFileNameW(&dialog)) return;

    const std::string text = readListFile(file);
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

    linksThread_ = std::thread([this, hwnd = m_hWnd, text = std::move(text), titleIds, files = linkFiles(), target = sonyLinksPath_, guided] {
        auto* outcome = new LinksJobOutcome;
        outcome->imported = true;
        outcome->guided = guided;

        gth::network::SonyLinkImporter importer(target, files);
        outcome->import = importer.import(
            text, [this] { return linksCancel_.load(); },
            [hwnd](const std::string& line) {
                ::PostMessageW(hwnd, kLinksProgressMessage, 0, reinterpret_cast<LPARAM>(new std::wstring(Utf8ToWide(line))));
            });
        outcome->ok = !outcome->import.saveFailed;

        const auto known = gth::network::ReadSonyLinkTitleIds(files);
        outcome->ps5Games = static_cast<int>(titleIds.size());
        for (const auto& id : titleIds) outcome->covered += known.contains(id) ? 1 : 0;
        ::PostMessageW(hwnd, kLinksDoneMessage, 0, reinterpret_cast<LPARAM>(outcome));
    });
    return true;
}

// ---- Guided collection of Sony links ---------------------------------------------------------------------------------

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
    lastAutoclickProgressAt_ = std::chrono::steady_clock::now();
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
        if (game.first == collectCurrent_) title = Utf8ToWide(game.second) + L" (" + Utf8ToWide(game.first) + L")";
    }
    const int position = collectLearned_ + static_cast<int>(collectSkipped_.size()) + 1;
    std::wstring text = lead + L"Collecting Sony links - game " + std::to_wstring(position) + L" of " +
                        std::to_wstring(collectTotal_) + L": " + title +
                        L".\r\nThis game's page and the next few are open in your browser. The app opens the newest patch for "
                        L"whichever page is in front and copies its link for you - just solve the captcha if the site shows "
                        L"one (or, if it doesn't open on its own, click the newest patch yourself and right-click SC > Copy "
                        L"link address). Skip Game if this game has no patch; Cancel stops.";
    statusLabel_.SetWindowText(text.c_str());
}

void UpdatesView::collectNext(const std::wstring& lead) {
    const auto known = gth::network::ReadSonyLinkTitleIds(linkFiles());
    const auto games = ps5Games();
    const auto next = gth::application::NextGameNeedingLink(games, known, collectSkipped_);
    if (!next) {
        finishCollecting(lead + L"Every game is covered or skipped.", /*completed=*/true);
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

    const std::wstring status = how + L" Saved " + std::to_wstring(collectLearned_) + L" link(s), skipped " +
                                std::to_wstring(collectSkipped_.size()) + L" game(s).";
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
            statusLabel_.SetWindowText((status + L"\r\nUpdating the games that just got a link...").c_str());
        }
        return;
    }

    // With new links in place the check has something to say.
    if (collectLearned_ > 0 && !anythingRunning()) {
        BOOL handled = TRUE;
        OnCheckUpdatesClick(0, 0, nullptr, handled);
        if (updateInProgress_.load()) statusLabel_.SetWindowText((status + L"\r\nChecking those games against Sony now...").c_str());
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
        if (const auto clipboard = readClipboardText(m_hWnd); clipboard && !gth::network::ExtractSonyAddresses(*clipboard).empty()) {
            collectPending_.push_back(*clipboard);
        }
    }

    if (collectPending_.empty()) return;
    // If another job is running, leave it at the front of the queue and try again a moment later.
    if (startLinkImport(collectPending_.front(), /*guided=*/true)) collectPending_.pop_front();
}

void UpdatesView::onGuidedImportDone(const LinksJobOutcome& outcome) {
    const auto& result = outcome.import;
    collectLearned_ += static_cast<int>(result.learned);

    if (result.saveFailed) {
        finishCollecting(L"Couldn't write the links to " + sonyLinksPath_.wstring() + L" - stopped.");
        return;
    }
    if (result.learned > 0) {
        std::wstring ids;
        for (size_t i = 0; i < result.titleIdsLearned.size() && i < 4; ++i) ids += (i ? L", " : L"") + Utf8ToWide(result.titleIdsLearned[i]);
        for (const auto& titleId : result.titleIdsLearned) {
            queueAutoDownload(titleId);
            liveRows_.erase(titleId);  // resolved - stop showing any "needs you"/"waiting" marker for it
        }
        refresh();  // clears those rows' markers immediately rather than waiting for the next unrelated refresh
        collectNext(L"Saved the link for " + ids + L". ");
        return;
    }
    if (result.failed + result.gamePiecesOnly > 0 && !result.notes.empty()) {
        // Nothing usable: say why, and keep waiting for the same game.
        showCollectStatus(Utf8ToWide(result.notes.front()) + L"\r\n");
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

LRESULT UpdatesView::OnMoreClick(WORD, WORD, HWND, BOOL&) {
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

    RECT rect{};
    moreButton_.GetWindowRect(&rect);
    // No TPM_RETURNCMD: the chosen item arrives as a WM_COMMAND with the id of the button that used to do this.
    ::TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, rect.left, rect.bottom, 0, m_hWnd, nullptr);
    ::DestroyMenu(menu);
    return 0;
}

LRESULT UpdatesView::OnSkipGameClick(WORD, WORD, HWND, BOOL&) {
    if (!collecting_) return 0;
    if (!collectCurrent_.empty()) collectSkipped_.insert(collectCurrent_);
    collectNext(L"Skipped. ");
    return 0;
}

LRESULT UpdatesView::OnLinksProgress(UINT, WPARAM, LPARAM lParam, BOOL&) {
    std::unique_ptr<std::wstring> line(reinterpret_cast<std::wstring*>(lParam));
    if (linksInProgress_.load()) statusLabel_.SetWindowText(line->c_str());
    return 0;
}

LRESULT UpdatesView::OnLinksDone(UINT, WPARAM, LPARAM lParam, BOOL&) {
    std::unique_ptr<LinksJobOutcome> outcome(reinterpret_cast<LinksJobOutcome*>(lParam));
    linksInProgress_.store(false);
    shared_->endJob();
    setBusy(false);
    tryStartNextAutoDownload();  // the one-job slot just freed - let a queued game take it

    if (outcome->imported && outcome->guided && collecting_) {
        onGuidedImportDone(*outcome);
        return 0;
    }

    if (outcome->imported) {
        const auto& result = outcome->import;
        std::wstring status = L"Read " + std::to_wstring(result.addressesFound) + L" Sony address(es): " +
                              std::to_wstring(result.learned) + L" new link(s) saved, " + std::to_wstring(result.alreadyKnown) +
                              L" already known";
        if (result.failed + result.gamePiecesOnly > 0) {
            status += L", " + std::to_wstring(result.failed + result.gamePiecesOnly) + L" couldn't be used";
        }
        status += result.cancelled ? L" (cancelled). " : L". ";
        if (result.saveFailed) status += L"Couldn't write the links to " + sonyLinksPath_.wstring() + L". ";
        status += L"Now " + std::to_wstring(outcome->covered) + L" of your " + std::to_wstring(outcome->ps5Games) +
                  L" PS5 games have a Sony link.";
        statusLabel_.SetWindowText(status.c_str());
        GTH_LOG_INFO("Sony address import: {} | {}", WideToUtf8(status), result.notes.empty() ? "" : result.notes.front());

        if (!result.notes.empty()) {
            std::wstring detail = L"These addresses couldn't be turned into a link:\r\n\r\n";
            for (size_t i = 0; i < result.notes.size() && i < 12; ++i) detail += L"- " + Utf8ToWide(result.notes[i]) + L"\r\n";
            if (result.notes.size() > 12) detail += L"... and " + std::to_wstring(result.notes.size() - 12) + L" more (see the log).\r\n";
            for (size_t i = 12; i < result.notes.size(); ++i) GTH_LOG_INFO("Sony address import: {}", result.notes[i]);
            ::MessageBoxW(m_hWnd, detail.c_str(), L"Add links", MB_OK | MB_ICONINFORMATION);
        }

        if (result.learned > 0) {
            BOOL handled = TRUE;
            OnCheckUpdatesClick(0, 0, nullptr, handled);
            if (updateInProgress_.load()) statusLabel_.SetWindowText((status + L"\r\nChecking those games against Sony now...").c_str());
        }
        return 0;
    }

    std::wstring status;
    if (outcome->ok) {
        status = L"Downloaded " + std::to_wstring(outcome->downloaded) + L" Sony links (" + std::to_wstring(outcome->total) +
                 L" saved in total). They cover " + std::to_wstring(outcome->covered) + L" of your " +
                 std::to_wstring(outcome->ps5Games) + L" PS5 games";
        const int missing = outcome->ps5Games - outcome->covered;
        if (missing > 0) {
            status += L"; " + std::to_wstring(missing) +
                      L" still need a link (the public list only has the titles somebody has shared - use More > Sony update links > "
                      L"Add links from the clipboard with each game's SC link; the Guide explains where to copy it).";
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
            statusLabel_.SetWindowText((status + L"\r\nChecking those games against Sony now...").c_str());
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
        statusLabel_.SetWindowText((L"Couldn't save the version for " + Utf8ToWide(title) + L": " +
                                    Utf8ToWide(result.errorMessage)).c_str());
        GTH_LOG_WARN("Set version for '{}' failed: {}", title, result.errorMessage);
        return;
    }

    refresh();
    selectVariant(variantId);
    const std::wstring message = version.empty()
        ? L"Cleared the version for " + Utf8ToWide(title) + L" - it will be detected again at the next scan."
        : L"Saved version " + Utf8ToWide(version) + L" for " + Utf8ToWide(title) +
              L", next to its files, so a rescan keeps it.";
    statusLabel_.SetWindowText(message.c_str());
    GTH_LOG_INFO("Set version for '{}' to '{}'", title, version);
}

LRESULT UpdatesView::OnSetVersionClick(WORD, WORD, HWND, BOOL&) {
    wchar_t text[64]{};
    versionEdit_.GetWindowText(text, 64);
    applyVersionToSelected(WideToUtf8(text));
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
        report += L"Saved " + std::to_wstring(merged.incoming) + L" Sony version link(s) - " + std::to_wstring(merged.total) +
                  L" in total.\r\n";
    } else {
        report += L"Couldn't write the Sony links to " + sonyLinksPath_.wstring() + L".\r\n";
    }
}

LRESULT UpdatesView::OnImportVersionsClick(WORD, WORD, HWND, BOOL&) {
    if (!indexer_ || anythingRunning()) return 0;

    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = m_hWnd;
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Import versions or Sony links";
    dialog.lpstrFilter = L"Lists (*.txt;*.csv;*.tsv)\0*.txt;*.csv;*.tsv\0All files (*.*)\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!::GetOpenFileNameW(&dialog)) return 0;

    WaitCursor wait;
    const std::string text = readListFile(file);

    std::wstring report;
    importSonyLinks(text, report);
    const bool importedLinks = !report.empty();

    const auto result = indexer_->importVersions(text);
    if (result.applied > 0 || result.failed > 0 || !importedLinks) {
        report += L"Set the version of " + std::to_wstring(result.applied) + L" game(s), next to their files.\r\n";
    }
    if (!result.unmatchedTitleIds.empty()) {
        std::wstring first;
        for (size_t i = 0; i < result.unmatchedTitleIds.size() && i < 5; ++i) {
            first += (i ? L", " : L"") + Utf8ToWide(result.unmatchedTitleIds[i]);
        }
        report += std::to_wstring(result.unmatchedTitleIds.size()) + L" title ID(s) in the list aren't in this library (" +
                  first + (result.unmatchedTitleIds.size() > 5 ? L", ..." : L"") + L").\r\n";
    }
    if (result.failed > 0) {
        report += std::to_wstring(result.failed) + L" couldn't be saved - first: " + Utf8ToWide(result.firstError) + L"\r\n";
    }
    if (result.linesIgnored > 0 && !importedLinks) {
        report += std::to_wstring(result.linesIgnored) + L" line(s) had no recognizable title ID and version.\r\n";
    }

    refresh();
    GTH_LOG_INFO("Import list: {}", WideToUtf8(report));
    ::MessageBoxW(m_hWnd, report.c_str(), L"Import list", MB_OK | MB_ICONINFORMATION);
    return 0;
}
