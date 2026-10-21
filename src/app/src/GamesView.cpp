#include "GamesView.h"

#include <algorithm>
#include <memory>

#include "gth/core/enums/GameVariantStatus.h"
#include "gth/logging/Logging.h"

namespace {
constexpr int kTopAreaHeight = 104;
constexpr int kMargin = 24;

// Only runs while a background scan is in flight (started in OnScanClick,
// killed once it's picked up completion) - bridges the scan thread's result
// back onto the UI thread without introducing a general polling convention.
constexpr UINT_PTR kScanPollTimerId = 1;
constexpr UINT kScanPollIntervalMs = 200;
}  // namespace

LRESULT GamesView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CRect titleRect(kMargin, 24, kMargin + 300, 60);
    titleLabel_.Create(m_hWnd, titleRect, L"Games", WS_CHILD | WS_VISIBLE);
    titleLabel_.SetFont(theme::TitleFont());

    CRect scanRect(kMargin + 320, 28, kMargin + 480, 60);
    scanButton_.Create(m_hWnd, scanRect, L"Scan All Storages", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0,
                        IDC_SCAN_GAMES_BUTTON);
    scanButton_.SetFont(theme::BodyFont());
    scanButton_.setKind(ModernButton::Kind::Primary);

    CRect testRect(kMargin + 320, 66, kMargin + 480, 92);
    testLookupButton_.Create(m_hWnd, testRect, L"Test Online Lookup",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_GAMES_TEST_LOOKUP_BUTTON);
    testLookupButton_.SetFont(theme::BodyFont());
    testLookupButton_.setKind(ModernButton::Kind::Subtle);

    CRect statusRect(kMargin + 500, 12, kMargin + 900, 100);
    statusLabel_.Create(m_hWnd, statusRect, L"", WS_CHILD | WS_VISIBLE);
    statusLabel_.SetFont(theme::BodyFont());

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect listRect(kMargin, kTopAreaHeight, clientRect.right - kMargin, clientRect.bottom - kMargin);
    // LVS_ICON: a poster grid (box art on top, its label wrapped beneath) like the Sony
    // storefronts this is modeled on - not the plain report-view rows/columns lists elsewhere in
    // the app use. LVS_AUTOARRANGE keeps tiles snapped into a tidy grid as the pane resizes.
    listView_.Create(m_hWnd, listRect, L"",
                      WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_ICON | LVS_AUTOARRANGE | LVS_SINGLESEL, 0,
                      IDC_GAMES_LISTVIEW);
    listView_.SetFont(theme::BodyFont());
    theme::ApplyListViewTheme(listView_);
    listView_.SetIconSpacing(PosterCache::kTileWidth + 24, PosterCache::kTileHeight + 52);
    if (posterCache_) posterCache_->subscribe(m_hWnd);

    refresh();
    return 0;
}

LRESULT GamesView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    KillTimer(kScanPollTimerId);
    // A scan in flight can run for a long time on a large library; joining
    // here briefly delays shutdown rather than risking a detached thread
    // touching this object's members after it's destroyed.
    if (scanThread_.joinable()) scanThread_.join();
    if (testThread_.joinable()) testThread_.join();
    bHandled = FALSE;
    return 0;
}

LRESULT GamesView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
    const int width = LOWORD(lParam);
    const int height = HIWORD(lParam);
    if (statusLabel_.IsWindow()) {
        constexpr int kStatusLeft = kMargin + 500;
        statusLabel_.MoveWindow(kStatusLeft, 12, std::max(200, width - kStatusLeft - kMargin), 88);
    }
    if (listView_.IsWindow()) {
        listView_.MoveWindow(kMargin, kTopAreaHeight, std::max(100, width - 2 * kMargin),
                              std::max(100, height - kTopAreaHeight - kMargin));
    }
    return 0;
}

LRESULT GamesView::OnCtlColorStatic(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleCtlColorStatic(wParam); }

bool GamesView::anythingRunning() const { return shared_ != nullptr && shared_->jobRunning(); }

void GamesView::setBusy(bool busy) {
    const BOOL enabled = busy ? FALSE : TRUE;
    scanButton_.EnableWindow(enabled);
    testLookupButton_.EnableWindow(enabled);
}

void GamesView::refresh() {
    if (!library_ || !listView_.IsWindow()) return;

    theme::ApplyListViewTheme(listView_);
    listView_.DeleteAllItems();
    const auto entries = library_->getAllEntries();

    // Rebuilt from scratch every refresh - simplest way to keep it exactly in step with the list
    // (added/removed/reordered games), and cheap: it only holds icon-sized bitmaps ImageList_Add
    // copies out of PosterCache's own (already cached, reused) tiles, not the tiles themselves.
    if (posterImageList_.IsNull()) {
        posterImageList_.Create(PosterCache::kTileWidth, PosterCache::kTileHeight, ILC_COLOR32, 0,
                                 static_cast<int>(entries.size()) + 1);
    } else {
        posterImageList_.RemoveAll();
    }
    listView_.SetImageList(posterImageList_, LVSIL_NORMAL);

    int row = 0;
    for (const auto& entry : entries) {
        // "-" for a game whose files are gone; "Unknown" for one that's here
        // but whose package and file name don't state a version - unless the
        // Updates tab's Sony check identified it from the package's file sizes.
        std::wstring versionText;
        const gth::application::UpdateCheckEntry* sonyVerdict = nullptr;
        if (shared_) {
            const auto found = shared_->updateByVariant.find(entry.gameVariantId);
            if (found != shared_->updateByVariant.end()) sonyVerdict = &found->second;
        }
        if (!entry.currentVersion.empty()) {
            versionText = Utf8ToWide(entry.currentVersion);
        } else if (sonyVerdict != nullptr && sonyVerdict->installedVersionInferred) {
            versionText = Utf8ToWide(sonyVerdict->installedVersion);
        } else {
            versionText = entry.variantStatus == gth::core::GameVariantStatus::Active ? L"Unknown" : L"-";
        }

        // A poster tile's label has room for two short lines only (see UpdatesView::composeLabel's
        // identical note) - version alone, not the size too, which the Storage/Downloads tabs
        // already cover in their own detail views.
        const std::wstring label = Utf8ToWide(entry.title) + L"\r\n" + versionText;

        const int imageIndex = posterCache_
            ? posterImageList_.Add(posterCache_->posterFor(entry.titleId, entry.title, entry.platform,
                                                            posterService_, entry.gameVariantId))
            : -1;

        listView_.InsertItem(row, label.c_str(), imageIndex);
        row++;
    }

    setBusy(anythingRunning());  // a job started on the Updates tab keeps these buttons off too
}

LRESULT GamesView::OnScanClick(WORD, WORD, HWND, BOOL&) {
    if (shared_ && shared_->jobRunning() && !scanInProgress_.load()) {
        statusLabel_.SetWindowText(L"Another job is running (see the Updates tab). Try again when it has finished.");
        return 0;
    }
    startScan();
    return 0;
}

LRESULT GamesView::OnTestLookupClick(WORD, WORD, HWND, BOOL&) {
    if (!titleLookup_ || !shared_->tryBeginJob()) return 0;
    if (testThread_.joinable()) testThread_.join();

    // The main window's title carries the app version - included in the
    // result so it's unambiguous which build produced it.
    wchar_t appTitle[128]{};
    ::GetWindowTextW(::GetAncestor(m_hWnd, GA_ROOT), appTitle, 128);

    testInProgress_.store(true);
    setBusy(true);
    statusLabel_.SetWindowText(L"Testing the online title lookup...");

    testThread_ = std::thread([this, hwnd = m_hWnd, header = std::wstring(appTitle)] {
        struct Probe {
            const char* platform;
            const char* titleId;
            const wchar_t* site;
        };
        const Probe probes[] = {{"PS5", "PPSA02343", L"prosperopatches.com"},
                                {"PS4", "CUSA07410", L"orbispatches.com"}};

        std::wstring text = header + L"\r\n\r\n";
        for (const auto& probe : probes) {
            std::wstring line = Utf8ToWide(probe.platform) + L" " + Utf8ToWide(probe.titleId) + L"  (" + probe.site +
                                L"):  ";
            const auto title = titleLookup_->lookupTitle(probe.platform, probe.titleId);
            if (title) {
                line += L"found - " + Utf8ToWide(*title);
            } else if (const auto why = titleLookup_->lastFailureReason(); !why.empty()) {
                line += L"FAILED - " + Utf8ToWide(why);
            } else {
                line += L"reached the site, but it has no such title";
            }
            text += line + L"\r\n";
        }
        text += L"\r\nIf both say FAILED, this PC can't get to the title sites, and the reason above says why.";

        ::PostMessageW(hwnd, kTestLookupDoneMessage, 0, reinterpret_cast<LPARAM>(new std::wstring(std::move(text))));
    });
    return 0;
}

LRESULT GamesView::OnTestLookupDone(UINT, WPARAM, LPARAM lParam, BOOL&) {
    std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
    testInProgress_.store(false);
    shared_->endJob();
    setBusy(false);
    statusLabel_.SetWindowText(L"");

    GTH_LOG_INFO("Online title lookup test: {}", WideToUtf8(*text));
    ::MessageBoxW(m_hWnd, text->c_str(), L"Online title lookup test", MB_OK | MB_ICONINFORMATION);
    return 0;
}

LRESULT GamesView::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (wParam != kScanPollTimerId) return 0;
    if (scanInProgress_.load()) return 0;

    KillTimer(kScanPollTimerId);
    if (scanThread_.joinable()) scanThread_.join();

    refresh();

    wchar_t buf[256];
    swprintf_s(buf, L"Scanned %d storage(s): %d games added, %d versions added, %d updated.",
               scanStoragesScanned_, scanTotalGamesAdded_, scanTotalVersionsAdded_, scanTotalVersionsUpdated_);
    std::wstring status = buf;
    // One fact per line, and the online-titles line is ALWAYS present: when
    // no lookup was attempted it says why, so a missing line can't be
    // mistaken for "the feature isn't there".
    const int attempted = scanLookupsResolved_ + scanLookupsNotFound_ + scanLookupsFailed_;
    wchar_t lookupBuf[320];
    if (attempted > 0) {
        swprintf_s(lookupBuf, L"\r\nOnline titles: %d looked up - %d found, %d not listed, %d failed.", attempted,
                   scanLookupsResolved_, scanLookupsNotFound_, scanLookupsFailed_);
    } else {
        swprintf_s(lookupBuf, L"\r\nOnline titles: no lookups were needed (%d named from the game files, %d not PS4/PS5).",
                   scanReleasesNamedFromFiles_, scanReleasesNotEligible_);
    }
    status += lookupBuf;
    if (scanLookupsFailed_ > 0 && !scanLookupFailure_.empty()) status += L"\r\nReason: " + scanLookupFailure_;
    if (scanVariantsFailed_ > 0) {
        status += L"\r\n" + std::to_wstring(scanVariantsFailed_) + L" release(s) couldn't be saved - first: " +
                  scanFirstVariantError_;
    }
    if (!scanLastError_.empty()) {
        status += L"\r\nFailed: " + scanLastError_;
    }
    statusLabel_.SetWindowText(status.c_str());

    std::string logLine = WideToUtf8(status);
    std::replace(logLine.begin(), logLine.end(), '\r', ' ');
    std::replace(logLine.begin(), logLine.end(), '\n', '|');
    GTH_LOG_INFO("Games scan finished: {}", logLine);
    shared_->endJob();
    setBusy(false);
    return 0;
}

void GamesView::startScan() {
    if (!storageService_ || !indexer_ || !shared_->tryBeginJob()) return;

    if (scanThread_.joinable()) scanThread_.join();  // previous run's thread, already finished

    scanInProgress_.store(true);
    setBusy(true);
    statusLabel_.SetWindowText(L"Scanning...");

    scanThread_ = std::thread([this] {
        int totalGamesAdded = 0, totalVersionsAdded = 0, totalVersionsUpdated = 0;
        int storagesScanned = 0;
        int lookupsResolved = 0, lookupsNotFound = 0, lookupsFailed = 0;
        int releasesNamedFromFiles = 0, releasesNotEligible = 0;
        int variantsFailed = 0;
        std::wstring firstVariantError;
        std::wstring lookupFailure;
        std::wstring lastError;

        // Prunes any storage that's no longer valid (most notably a stale
        // system-drive row from before that got excluded, or one this
        // machine hasn't rescanned since) before touching a single one -
        // otherwise a user who scans games without ever visiting the
        // Storage tab would keep re-scanning a storage that should've been
        // removed, since nothing else on this path runs that sync.
        storageService_->detectAndSyncLocalStorages();

        for (const auto& storage : storageService_->getAllStorages()) {
            const auto result = indexer_->scanStorage(storage.storageId);
            // The lookup and save-failure numbers are worth reporting even
            // when the storage as a whole failed - they say how far it got.
            lookupsResolved += result.titleLookupsResolved;
            lookupsNotFound += result.titleLookupsNotFound;
            lookupsFailed += result.titleLookupsFailed;
            releasesNamedFromFiles += result.releasesNamedFromFiles;
            releasesNotEligible += result.releasesNotEligible;
            variantsFailed += result.variantsFailed;
            if (!result.lastTitleLookupFailure.empty()) lookupFailure = Utf8ToWide(result.lastTitleLookupFailure);
            if (firstVariantError.empty() && !result.firstVariantError.empty()) {
                firstVariantError = Utf8ToWide(result.firstVariantError);
            }

            if (result.success) {
                totalGamesAdded += result.gamesAdded;
                totalVersionsAdded += result.versionsAdded;
                totalVersionsUpdated += result.versionsUpdated;
                storagesScanned++;
            } else {
                lastError = Utf8ToWide(storage.name + ": " + result.errorMessage);
            }
        }

        scanTotalGamesAdded_ = totalGamesAdded;
        scanTotalVersionsAdded_ = totalVersionsAdded;
        scanTotalVersionsUpdated_ = totalVersionsUpdated;
        scanStoragesScanned_ = storagesScanned;
        scanLookupsResolved_ = lookupsResolved;
        scanLookupsNotFound_ = lookupsNotFound;
        scanLookupsFailed_ = lookupsFailed;
        scanReleasesNamedFromFiles_ = releasesNamedFromFiles;
        scanReleasesNotEligible_ = releasesNotEligible;
        scanVariantsFailed_ = variantsFailed;
        scanFirstVariantError_ = firstVariantError;
        scanLookupFailure_ = lookupFailure;
        scanLastError_ = lastError;
        scanInProgress_.store(false);
    });
    SetTimer(kScanPollTimerId, kScanPollIntervalMs);
}
