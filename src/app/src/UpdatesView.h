#pragma once

#include <atomic>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "WtlCommon.h"
#include "ControlIds.h"
#include "ModernButton.h"
#include "ModernCheckBox.h"
#include "SharedArchiveState.h"
#include "Theme.h"
#include "gth/application/indexing/IGameIndexer.h"
#include "gth/application/indexing/IGameLibraryService.h"
#include "gth/application/indexing/IPackageSync.h"
#include "gth/application/indexing/IUpdateChecker.h"
#include "gth/application/indexing/DownloadHistory.h"
#include "gth/application/indexing/LinkCollection.h"
#include "gth/application/indexing/SonyAutomation.h"
#include "gth/application/indexing/IGamePosterService.h"
#include "gth/application/storage/IStorageService.h"
#include "gth/network/SonyLinkImport.h"
#include "CaptchaContextService.h"
#include "PatchAutoClicker.h"
#include "PosterCache.h"
#include "SonyLinkCopyWatcher.h"

// Posted when the Sony update check finishes; lParam is an owned
// gth::application::UpdateCheckSummary*.
constexpr UINT kUpdateCheckDoneMessage = WM_APP + 43;

// Progress text from a running Sony sync (lParam: owned std::wstring*), and its
// outcome when it ends (lParam: owned SyncJobOutcome*).
constexpr UINT kSyncProgressMessage = WM_APP + 44;
constexpr UINT kSyncDoneMessage = WM_APP + 45;

// Posted when the public Sony link list has been downloaded (lParam: owned LinksJobOutcome*).
constexpr UINT kLinksDoneMessage = WM_APP + 46;

// A change to one game's row while the download line runs (lParam: owned RowUpdate*).
constexpr UINT kSyncRowMessage = WM_APP + 47;

// The library gained games while the download line was being set up (lParam unused): rebuild the list.
constexpr UINT kSyncListMessage = WM_APP + 49;

// Posted by SonyLinkCopyWatcher right after it copies a patch's SC link (wParam/lParam unused) -
// visible proof in the status line that the copy actually happened, without needing the log file.
constexpr UINT kSonyLinkCopiedMessage = WM_APP + 50;

// Posted by PatchAutoClicker after it clicks a patch (wParam 0) or after the site's captcha check
// rejects that automated click (wParam 1) - lParam unused either way.
constexpr UINT kPatchAutoClickMessage = WM_APP + 51;

struct RowUpdate {
    std::string titleId;
    std::wstring text;
};

// Progress line from a running link import (lParam: owned std::wstring*).
constexpr UINT kLinksProgressMessage = WM_APP + 48;

struct LinksJobOutcome {
    bool ok = false;
    std::string error;
    size_t downloaded = 0;  // links in the list
    size_t total = 0;       // links saved afterwards
    int ps5Games = 0;       // PS5 games in the library
    int covered = 0;        // ...that now have a link

    // Set when the job was "Add links from ..." rather than the public list download.
    bool imported = false;
    // ...and when it came from a collection session (More > Sony update links > Collect links), which carries on by itself.
    bool guided = false;
    gth::network::SonyLinkImportResult import;
};

// Everything a finished Sony sync job has to tell the UI.
struct SyncJobOutcome {
    bool automatic = false;
    bool ranSync = false;
    bool oneGame = false;
    gth::application::SyncResult single;
    gth::application::SyncBatchResult batch;
    int storagesRescanned = 0;
    bool scannedForNewGames = false;   // the storages were searched for new games first
    int newGamesFound = 0;
    int storagesUnreachable = 0;       // ...and this many couldn't be reached, so they were left exactly as they are
    int linksLearnedFromFiles = 0;     // Sony links read out of metadata pieces already in the archive
    bool checked = false;
    gth::application::UpdateCheckSummary check;
    std::time_t startedAt = 0;
    std::chrono::seconds took{0};
};

// The Updates tab: everything that keeps the archive current - the Sony check,
// syncing games with Sony, the automation switches, the links Sony needs, and
// the tools for saying which version a game is.
class UpdatesView : public CWindowImpl<UpdatesView> {
public:
    DECLARE_WND_CLASS(L"GthUpdatesView")

    BEGIN_MSG_MAP(UpdatesView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(kUpdateCheckDoneMessage, OnUpdateCheckDone)
        MESSAGE_HANDLER(kSyncProgressMessage, OnSyncProgress)
        MESSAGE_HANDLER(kSyncDoneMessage, OnSyncDone)
        MESSAGE_HANDLER(kLinksDoneMessage, OnLinksDone)
        MESSAGE_HANDLER(kLinksProgressMessage, OnLinksProgress)
        MESSAGE_HANDLER(kSyncRowMessage, OnSyncRow)
        MESSAGE_HANDLER(kSyncListMessage, OnSyncList)
        MESSAGE_HANDLER(kSonyLinkCopiedMessage, OnSonyLinkCopied)
        MESSAGE_HANDLER(kPatchAutoClickMessage, OnPatchAutoClick)
        MESSAGE_HANDLER(PosterCache::kPosterReadyMessage, OnPosterReady)
        COMMAND_ID_HANDLER(IDC_UPDATES_CHECK_SONY_BUTTON, OnCheckUpdatesClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_SET_VERSION_BUTTON, OnSetVersionClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_RESET_VERSION_BUTTON, OnResetVersionClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_OPEN_TRACKER_BUTTON, OnOpenTrackerClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_IMPORT_VERSIONS_BUTTON, OnImportVersionsClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_SYNC_SELECTED_BUTTON, OnSyncSelectedClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_DOWNLOAD_ALL_BUTTON, OnDownloadAllClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_CANCEL_SYNC_BUTTON, OnCancelSyncClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_SONY_LINKS_BUTTON, OnSonyLinksClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_SKIP_GAME_BUTTON, OnSkipGameClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_GUIDE_BUTTON, OnGuideClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_MORE_BUTTON, OnMoreClick)
        COMMAND_ID_HANDLER(IDC_UPDATES_AUTO_CHECK_CHECKBOX, OnAutomationSettingClicked)
        COMMAND_ID_HANDLER(IDC_UPDATES_AUTO_METADATA_CHECKBOX, OnAutomationSettingClicked)
        COMMAND_ID_HANDLER(IDC_UPDATES_AUTO_UPDATES_CHECKBOX, OnAutomationSettingClicked)
        COMMAND_ID_HANDLER(IDC_UPDATES_AUTO_SCAN_CHECKBOX, OnAutomationSettingClicked)
        NOTIFY_HANDLER(IDC_UPDATES_LISTVIEW, LVN_ITEMCHANGED, OnListSelectionChanged)
    END_MSG_MAP()

    void init(gth::application::IGameLibraryService& library, gth::application::IGameIndexer& indexer,
              gth::application::IUpdateChecker& updateChecker, gth::application::IPackageSync& packageSync,
              gth::application::IStorageService& storageService, std::filesystem::path sonyLinksPath,
              std::filesystem::path sonyAutomationPath, SharedArchiveState& shared, PosterCache& posterCache,
              gth::application::IGamePosterService& posterService) {
        library_ = &library;
        indexer_ = &indexer;
        updateChecker_ = &updateChecker;
        packageSync_ = &packageSync;
        storageService_ = &storageService;
        sonyLinksPath_ = std::move(sonyLinksPath);
        reportPath_ = sonyLinksPath_.parent_path() / "sony-automation-log.txt";
        downloadHistoryPath_ = sonyLinksPath_.parent_path() / "download-history.tsv";
        automationStore_.emplace(std::move(sonyAutomationPath));
        automationSettings_ = automationStore_->load();
        shared_ = &shared;
        posterCache_ = &posterCache;
        posterService_ = &posterService;
    }

    void refresh();

private:
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT, WPARAM wParam, LPARAM, BOOL&) { return HandleEraseBkgndWithContentBg(m_hWnd, wParam); }
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCheckUpdatesClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnUpdateCheckDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSetVersionClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnResetVersionClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnOpenTrackerClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnImportVersionsClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnSyncSelectedClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnDownloadAllClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnCancelSyncClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnAutomationSettingClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnSyncProgress(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSyncRow(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSyncList(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSonyLinkCopied(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPatchAutoClick(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPosterReady(UINT, WPARAM, LPARAM, BOOL&) { refresh(); return 0; }
    LRESULT OnSyncDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSonyLinksClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnLinksDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLinksProgress(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSkipGameClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnGuideClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnMoreClick(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
    LRESULT OnListSelectionChanged(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);

    // The list entry behind the selected row, or nullptr when nothing is selected.
    const gth::application::GameLibraryEntry* selectedEntry() const;
    void selectVariant(const std::string& gameVariantId);
    void updateVersionControls();
    void applyVersionToSelected(const std::string& version);
    void importSonyLinks(const std::string& text, std::wstring& report);
    // The full multi-line poster-tile label for one entry (title, version/size, and - when one is
    // set - the live status line liveRows_ carries for its titleId). Shared by refresh() and every
    // place that updates a row's live status afterward, so both build the exact same text.
    std::wstring composeLabel(const gth::application::GameLibraryEntry& entry) const;

    // Runs a Sony sync on a background thread: one game (when `gameVariantId`
    // is set) or every game, then a rescan of whatever changed and a fresh
    // check. `automatic` runs follow the saved automation settings. `checkAfter=false` skips the
    // library-wide "ask Sony about every game" pass afterward - for the auto-download queue,
    // where doing that after every single newly-gathered link would mean re-checking the whole
    // library dozens of times in one guided session instead of once.
    void startSync(const std::string& gameVariantId, gth::application::SyncMode mode, bool automatic,
                   bool checkAfter = true);
    void applyAutomationSettingsToControls();
    // Reads the Sony version link out of every "_sc.pkg" metadata piece already in the archive (no network) and
    // saves the ones the app didn't have. Safe on a worker thread. Returns how many links were learned.
    size_t learnLinksFromArchive();
    void startLinkDownload();
    // Reads the Sony addresses in `text` (a metadata piece, its manifest / .crc, or a version file) and saves the
    // version-file link each one leads to.
    // Returns false (and does nothing) when another job is running.
    bool startLinkImport(std::string text, bool guided = false);
    void addLinksFromClipboard();
    void addLinksFromFile();

    // Guided collection: open the next link-less game's tracker page, wait for its SC link to appear on the
    // clipboard (the collector script copies it when the person opens a patch - or they copy it themselves),
    // read the version link out of it, save it, and move on to the next game. The person's one click per game
    // (opening a patch) is what the tracker's own check needs; nothing here talks to the tracker.
    void startCollecting();
    void collectTick();
    void collectNext(const std::wstring& lead = {});
    // `completed`: the session ran through every game (rather than being stopped or failing).
    void finishCollecting(const std::wstring& how, bool completed = false);
    // After "Update All Games" leaves games with no Sony link: offers the guided collection.
    void offerLinkCollection(int gamesWithoutLink);
    void onGuidedImportDone(const LinksJobOutcome& outcome);
    void showCollectStatus(const std::wstring& lead);
    // Looks up the game whose Sony link the guided collector just learned and puts it in line to
    // download right away - the games tab full of pre-opened patches doesn't wait for it.
    void queueAutoDownload(const std::string& titleId);
    // Starts the next queued auto-download if the one-job slot is free; a no-op otherwise (it's
    // retried from wherever the current job's own completion handler calls this again).
    void tryStartNextAutoDownload();
    // The gameVariantId for a title ID the library has, or empty if none matches.
    std::string variantIdForTitleId(const std::string& titleId) const;
    void writeNeededLinksList();
    // Where Sony version links may be kept: beside the exe, in the data folder, and the fill-in list.
    std::vector<std::filesystem::path> linkFiles() const;
    // PS5 games the library has files for, as (title ID, title), one per title ID.
    std::vector<std::pair<std::string, std::string>> ps5Games() const;
    static std::wstring describeCheck(const gth::application::UpdateCheckSummary& summary,
                                      const std::filesystem::path& sonyLinksPath);

    // Buttons that start work (or change the library) are off while any job -
    // this tab's or the Games tab's - is running.
    void setBusy(bool busy);
    bool anythingRunning() const;

    gth::application::IGameLibraryService* library_ = nullptr;
    gth::application::IGameIndexer* indexer_ = nullptr;
    gth::application::IUpdateChecker* updateChecker_ = nullptr;
    gth::application::IPackageSync* packageSync_ = nullptr;
    gth::application::IStorageService* storageService_ = nullptr;
    std::filesystem::path sonyLinksPath_;
    std::filesystem::path reportPath_;  // what each run did, for reading later
    std::filesystem::path downloadHistoryPath_;  // per-game download stats, for the Downloads tab
    std::optional<gth::application::SonyAutomationSettingsStore> automationStore_;
    gth::application::SonyAutomationSettings automationSettings_;
    SharedArchiveState* shared_ = nullptr;
    PosterCache* posterCache_ = nullptr;
    gth::application::IGamePosterService* posterService_ = nullptr;
    CImageList posterImageList_;

    // "Check Sony for Updates": two requests per game against Sony's servers,
    // so it runs off the UI thread, and its result comes back as a message.
    std::thread updateThread_;
    std::atomic<bool> updateInProgress_{false};
    std::atomic<int> checkLearnedLinks_{0};  // links the running check read out of files in the archive first

    // A Sony sync (adding metadata / updating packages): can run for a long
    // time, reports progress, and can be cancelled.
    std::thread syncThread_;
    std::atomic<bool> syncInProgress_{false};
    std::atomic<bool> syncCancel_{false};

    // Downloading the public Sony link list, or reading Sony addresses into links.
    std::thread linksThread_;
    std::atomic<bool> linksInProgress_{false};
    std::atomic<bool> linksCancel_{false};

    // A guided collection session (UI thread only).
    bool collecting_ = false;
    std::set<std::string> collectSkipped_;
    std::string collectCurrent_;   // the first game still waiting for its link
    std::set<std::string> collectOpened_;  // title IDs whose tracker page was already opened this session
    // Clipboard text(s) that arrived while another job was running, oldest first. A queue rather
    // than a single slot: with auto-download now also occupying that one-job slot for as long as
    // a game takes to download (not just the second or two an import used to take), someone
    // clicking through several pre-opened patches in that window is far more likely than before -
    // each one still needs its own turn instead of the newest simply overwriting the others.
    std::deque<std::string> collectPending_;
    int collectTotal_ = 0;         // games without a link when the session started
    int collectLearned_ = 0;
    DWORD collectLastSequence_ = 0;
    bool collectAccountRevealShown_ = false;  // the account reveal fires once per session, not per page
    SonyLinkCopyWatcher sonyLinkCopyWatcher_;  // copies each patch's SC link once the person opens it
    PatchAutoClicker patchAutoClicker_;  // opens each game's newest patch automatically once its tab is in front

    // Last time the automatic clicking/copying visibly did something (a click, or a link learned) -
    // the watchdog timer only restarts the two watchers above once this has gone quiet for a while,
    // rather than unconditionally every tick, so it never interrupts work that's actually going
    // fine just because a fixed amount of time passed.
    std::chrono::steady_clock::time_point lastAutoclickProgressAt_;

    // "Update All Games" is one job to the person who pressed it: after the line, the missing links are
    // collected, and once they are in the line runs again. updateAllChain_ = the press is still in its first
    // line (UI thread only); updateAllAfterCollect_ = the running collection was offered by that press.
    bool updateAllChain_ = false;
    bool updateAllAfterCollect_ = false;

    // How a web page is opened - the default browser; replaceable so tests never open one.
    std::function<void(const std::wstring&)> openUrl_ = [](const std::wstring& url) {
        ::ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };

    // What the download line is doing to each game right now, shown in that game's Update cell (title ID -> text).
    std::map<std::string, std::wstring> liveRows_;
    // Which list rows show each title ID (rebuilt by refresh()), so a progress message finds its rows without
    // walking the whole list.
    std::unordered_map<std::string, std::vector<int>> rowsByTitle_;

    // The automation timer's bookkeeping (UI thread only).
    std::chrono::steady_clock::time_point appStarted_ = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> lastAutomationRun_;

    // What refresh() last put in the list, row for row - the version controls
    // act on the selected row's entry, and the list only carries display text.
    std::vector<gth::application::GameLibraryEntry> entries_;

    CStatic titleLabel_;
    ModernButton checkUpdatesButton_;
    ModernButton syncSelectedButton_;
    ModernButton downloadAllButton_;
    ModernButton cancelSyncButton_;
    ModernButton moreButton_;  // Sony links, import, tracker page, captcha check, guide
    CStatic automaticCaption_;

    ModernCheckBox autoCheckBox_;
    ModernCheckBox autoMetadataBox_;
    ModernCheckBox autoUpdatesBox_;
    ModernCheckBox autoScanBox_;

    CStatic versionCaption_;
    CEdit versionEdit_;
    ModernButton setVersionButton_;
    ModernButton resetVersionButton_;
    ModernButton skipGameButton_;  // only shown while collecting links

    CStatic statusLabel_;
    CListViewCtrl listView_;
};
