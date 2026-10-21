#pragma once

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Watches the foreground browser tab, during guided Sony link collection, for a patch's "SC"
// download button becoming visible - which only happens once the person opens that patch
// themselves (this never clicks a patch heading). The moment it appears, reads its href
// directly off the accessibility element - trying UIA's native Value pattern first, then
// falling back to LegacyIAccessiblePattern (the IAccessible2/MSAA accValue screen readers
// have used to read link URLs for decades), since which pattern a given Chrome build exposes
// for a plain hyperlink isn't guaranteed - and writes it straight to the clipboard for the
// app's existing clipboard watcher to pick up - no click, no context menu. An earlier version
// right-clicked and matched Chrome's "Copy link address" menu item by name, which is Chrome UI
// text: on a non-English Chrome that string is translated, so it would silently never match
// and never copy anything on such an install. Reading the value directly has no dependency on
// menu wording at all. This never interacts with the site's own captcha check on the
// patch-opening click - only with content the person has already revealed. Re-copies whenever
// the found link differs from the last one copied, so a second, third, ... game's link each get
// picked up in turn across a whole guided session, not just the first one. Once a game's link
// is copied, its tab is left alone for a minute in case it's still being looked at; past that,
// if it isn't the tab currently selected, this closes it (via its own close button, found
// structurally - never by clicking page content or driving the mouse) so a long session doesn't
// quietly accumulate dozens of idle tabs' worth of memory. A tab still waiting for its own patch
// click is never touched. Separately, this also caps the total number of our own tabs open at
// once at 15 - the moment a 16th would exist, it closes whichever *already-finished* tab (its own
// link already copied) has been open longest, never one still in progress and never the selected
// one; if none are finished yet, it simply doesn't close anything that tick.
class SonyLinkCopyWatcher {
public:
    SonyLinkCopyWatcher();
    ~SonyLinkCopyWatcher();

    // `notifyWindow`/`notifyMessage`: posted (PostMessageW, wParam/lParam unused) right after
    // each successful copy, so the caller can show it actually happened - the only way to see
    // this from the UI without reading the log file. `logFilePath`, when non-empty: every
    // copied link is also appended there (one "<local timestamp>\t<url>" line each), plain text,
    // created if missing - so there's a durable record of every link this watcher ever put on
    // the clipboard, independent of whether the app's own import/verification step later
    // succeeds for it. The first time that file is written on a given machine, a "GTH Latest
    // Update URL" shortcut to it is also created on the current user's Desktop.
    void Start(HWND notifyWindow = nullptr, UINT notifyMessage = 0, std::filesystem::path logFilePath = {});
    // `closeAllTabs`: the guided session is truly over (finished or cancelled, not a stall-
    // recovery restart) - before the worker thread actually exits, it closes every one of our own
    // tabs it can still find, including whichever one is currently selected (the one safeguard the
    // normal idle/15-cap sweeps always keep - here the whole point is that nothing is left open).
    // Best effort: a tab Chrome won't let go of (e.g. mid-navigation) is simply left for the person
    // to close themselves.
    void Stop(bool closeAllTabs = false);
    bool IsRunning() const;

    // Every one of our own (PS5 title ID) tabs this watcher's own scan actually saw open on its
    // last tick, as plain UTF-8 title IDs - a snapshot, not a live query, so this is cheap and
    // safe to call from any thread (in practice, the UI thread). Empty before the first tick, or
    // whenever this isn't running. The guided-collection loop (UpdatesView::collectNext) uses
    // this to notice a game whose tab has closed without its link ever being learned - one this
    // watcher itself closed via the 15-tab cap while it was still stuck (a permanently-failed
    // "needs you" tab never has its own link, so that cap never touches it, but the person can
    // still have closed it by hand, or the browser could have restarted) - so it can be reopened
    // instead of being silently never tried again for the rest of the session.
    std::vector<std::string> CurrentlyOpenTitleIds() const;

private:
    void Worker();

    std::atomic<bool> running_{false};
    std::atomic<bool> closeTabsOnStop_{false};
    std::unique_ptr<std::thread> thread_;
    HWND notifyWindow_ = nullptr;
    UINT notifyMessage_ = 0;
    std::filesystem::path logFilePath_;

    mutable std::mutex openTabsMutex_;
    std::vector<std::string> openTabsSnapshot_;
};
