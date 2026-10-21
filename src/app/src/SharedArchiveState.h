#pragma once

#include <atomic>
#include <deque>
#include <string>
#include <unordered_map>

#include "gth/application/indexing/IUpdateChecker.h"

// State the Games and Updates tabs share, owned by MainFrame.
//
// * Only one long job (a scan, a Sony check, a sync, a link download) may touch
//   the library at a time, whichever tab started it: two of them at once would
//   be two threads rescanning and rewriting the same database rows.
// * The Updates tab's latest verdict for each game is what the Games tab's
//   Version column falls back on ("01.500.000 (from file sizes)").
//
// One game's current line in an in-progress download, for the Downloads tab to show alongside
// its history while a sync is actually running - Updates already builds this same text for its
// own list (see UpdatesView::OnSyncRow), this just gives a second tab a copy of it.
struct LiveDownloadRow {
    std::string title;  // the human name if the library has one yet, else the title ID
    std::wstring statusText;
};

// One game waiting for the one-job slot so the guided collector's auto-download can start it
// (see UpdatesView::queueAutoDownload) - shared so the Downloads tab can show what's coming up
// next, and let the person remove one before it starts.
struct QueuedDownloadRow {
    std::string gameVariantId;
    std::string titleId;
    std::string title;  // the human name if known, else titleId
};

// Everything except jobRunning_ and cancelRequested is UI-thread only.
struct SharedArchiveState {
    std::unordered_map<std::string, gth::application::UpdateCheckEntry> updateByVariant;
    // Cleared at the start of each sync and once it ends; populated titleId by titleId while one runs.
    std::unordered_map<std::string, LiveDownloadRow> liveDownloads;
    // Oldest-first; UpdatesView pops the front once the job slot frees. The Downloads tab only
    // ever reads this to display it and erases a specific entry when its own Stop is pressed -
    // it never pops or reorders it itself.
    std::deque<QueuedDownloadRow> queuedDownloads;

    // True when the Downloads tab's Stop button (Live row) asks the running sync to stop - a
    // second way in alongside UpdatesView's own Cancel button, since UpdatesView::startSync's
    // shouldCancel callback runs on the sync's background thread and this flag is written from
    // the UI thread by a different view, so it has to be atomic even though almost everything
    // else here is UI-thread-only. Reset to false at the start of every sync.
    std::atomic<bool> cancelRequested{false};

    // True when the caller now owns the "a job is running" flag.
    bool tryBeginJob() {
        bool expected = false;
        return jobRunning_.compare_exchange_strong(expected, true);
    }
    void endJob() { jobRunning_.store(false); }
    bool jobRunning() const { return jobRunning_.load(); }

private:
    std::atomic<bool> jobRunning_{false};
};
