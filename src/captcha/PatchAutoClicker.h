#pragma once

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

// Auto-clicks the newest (highest-version) patch on a prosperopatches.com page during guided Sony
// link collection - replacing the click half of gth-sony-link-collector.user.js (the copy half is
// already replaced by the sibling SonyLinkCopyWatcher, which reads the SC link straight off the
// page's accessibility tree once a patch is open). Drives a real OS-level mouse click
// (MouseInput::ClickAtPosition - SetCursorPos + mouse_event), never a DOM .click(), so the site's
// own invisible captcha check runs on this exactly as it would on a real click.
//
// Same poll-loop shape as SonyLinkCopyWatcher, but proactive rather than purely reactive: each tick
// it works through every tab open in whichever browser window currently has OS focus - not just
// whichever one the person happens to be looking at - switching Chrome to the next not-yet-clicked
// tab itself (a real tab switch via UI Automation's SelectionItem pattern) so pre-opened background
// tabs (guided collection opens several ahead of time) get their patch opened without the person
// needing to click through them by hand. Identifies a tab by its own title ("<TITLEID>: <game
// name>"), so switching to an already-clicked tab is never clicked twice. Finds every "Patch
// <version>" heading via UI Automation, picks the highest by gth::core::compareVersions
// (PatchAutoClickLogic.h), and clicks its center point under MouseAutomationMutex(). Afterward,
// stays on that tab until either the SC link appears, the site's captcha rejects the click, or a
// bounded wait passes with neither - every one of those three outcomes is reported (see Start()),
// and a title that keeps failing is retried a bounded number of times, then left alone for the
// rest of the session - see failureCounts_.
class PatchAutoClicker {
public:
    PatchAutoClicker();
    ~PatchAutoClicker();

    // `notifyWindow`/`notifyMessage`: posted (PostMessageW) right after a click (wParam 0), and for
    // one of three outcomes once the wait afterward ends: the site's captcha rejected it (wParam 1),
    // no patch heading was ever found after several warm-up retries - genuinely no patches, or a
    // page that never loaded right (wParam 2), or neither the SC link nor a rejection ever showed up
    // within the wait - couldn't confirm what happened (wParam 3). Any of 1-3 happening repeatedly
    // for the *same* title, across any number of Stop()/Start() restarts, means one more
    // notification with wParam 4 instead - this title is being left alone for the rest of the
    // session, not retried again - so a page that will never resolve on its own can't make this
    // silently re-click the same tab forever every time something restarts it (see failureCounts_).
    // lParam is an owned `std::wstring*` holding that game's title ID in every case, so the caller
    // can say (and show, persistently, not just in a transient status line) *which* game needs a
    // person's own attention - the receiver takes ownership and must delete it.
    void Start(HWND notifyWindow = nullptr, UINT notifyMessage = 0);
    void Stop();
    bool IsRunning() const;

private:
    void Worker();

    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> thread_;
    HWND notifyWindow_ = nullptr;
    UINT notifyMessage_ = 0;

    // Consecutive non-success outcomes (rejected, no patch found, or timed out) per title, kept
    // here rather than as a Worker()-local so it survives a Stop()/Start() restart - the watchdog in
    // UpdatesView restarts this whenever nothing has visibly happened for a while, and without this
    // surviving that, a page that will never resolve (site-side, not something a retry fixes) would
    // just get re-clicked again every restart, forever, since a fresh Worker() has no memory of
    // having already tried it. Cleared only when this object is destroyed, i.e. the whole session
    // ending - a title is never given more total tries across the session than kMaxAttemptsPerTitle
    // (defined in the .cpp), no matter how many times the watchdog restarts in between.
    std::unordered_map<std::wstring, int> failureCounts_;
};
