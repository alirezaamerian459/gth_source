#include "PatchAutoClicker.h"
#include "BrowserWindow.h"
#include "ComPtr.h"
#include "MouseAutomationLock.h"
#include "MouseInput.h"
#include "PatchAutoClickLogic.h"

#include <UIAutomationClient.h>

#include <chrono>
#include <string>
#include <unordered_map>
#include <set>
#include <vector>

#pragma comment(lib, "UIAutomationCore.lib")
#pragma comment(lib, "ole32.lib")

namespace {

// Finds the first open tab (in tab-strip order) whose titleId isn't in `clickedTitles`, switches
// Chrome to it if it isn't already the active one (a real tab switch via the same UI Automation
// SelectionItem pattern TryCloseFinishedTab already reads IsSelected from elsewhere in this
// module - verified live this session that invoking Select() on a background tab really does
// bring it to the front and make its content readable), and returns its titleId. False if every
// open tab in this window has already been handled, or none has a usable title at all.
bool SelectNextUnclickedTab(IUIAutomation* automation, IUIAutomationElement* windowElement,
                            const std::set<std::wstring>& clickedTitles, std::wstring& outTitleId) {
    VARIANT tabType;
    VariantInit(&tabType);
    tabType.vt = VT_I4;
    tabType.lVal = UIA_TabItemControlTypeId;

    ComPtr<IUIAutomationCondition> tabCondition;
    if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, tabType, &tabCondition)) || !tabCondition) {
        return false;
    }
    ComPtr<IUIAutomationElementArray> tabs;
    if (FAILED(windowElement->FindAll(TreeScope_Descendants, tabCondition, &tabs)) || !tabs) return false;

    int count = 0;
    tabs->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> tab;
        if (FAILED(tabs->GetElement(i, &tab)) || !tab) continue;

        BSTR name = nullptr;
        if (FAILED(tab->get_CurrentName(&name)) || !name) continue;
        const std::wstring titleId = gth::captcha::TitleIdFromWindowTitle(name);
        ::SysFreeString(name);
        if (titleId.empty() || clickedTitles.contains(titleId)) continue;

        ComPtr<IUIAutomationSelectionItemPattern> selectionItem;
        if (SUCCEEDED(tab->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selectionItem))) && selectionItem) {
            BOOL isSelected = FALSE;
            selectionItem->get_CurrentIsSelected(&isSelected);
            if (!isSelected) selectionItem->Select();
        }
        outTitleId = titleId;
        return true;
    }
    return false;
}

// Every "Patch <version>" heading is its own Text-control element (verified live against the real
// site) - scoped to that control type rather than an unscoped scan, matching how
// FindProfileButtonRect/TryCloseFinishedTab elsewhere in this module already scope their own
// searches by control type. Picks the highest version among them; false if none are patch headings
// at all (page still warming up its accessibility tree, not a tracker page, or genuinely no
// patches shown).
bool FindNewestPatchRect(IUIAutomation* automation, IUIAutomationElement* root, RECT& outRect) {
    VARIANT textType;
    VariantInit(&textType);
    textType.vt = VT_I4;
    textType.lVal = UIA_TextControlTypeId;

    ComPtr<IUIAutomationCondition> textCondition;
    if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, textType, &textCondition)) || !textCondition) {
        return false;
    }
    ComPtr<IUIAutomationElementArray> texts;
    if (FAILED(root->FindAll(TreeScope_Descendants, textCondition, &texts)) || !texts) return false;

    int count = 0;
    texts->get_Length(&count);
    if (count <= 0) return false;

    std::vector<std::wstring> names(static_cast<size_t>(count));
    std::vector<RECT> rects(static_cast<size_t>(count));
    std::vector<bool> valid(static_cast<size_t>(count), false);

    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(texts->GetElement(i, &element)) || !element) continue;

        BSTR name = nullptr;
        if (FAILED(element->get_CurrentName(&name)) || !name) continue;
        names[static_cast<size_t>(i)].assign(name);
        ::SysFreeString(name);

        RECT rect{};
        if (FAILED(element->get_CurrentBoundingRectangle(&rect))) continue;
        if (rect.right <= rect.left || rect.bottom <= rect.top) continue;  // offscreen/zero-size - not clickable
        rects[static_cast<size_t>(i)] = rect;
        valid[static_cast<size_t>(i)] = true;
    }

    const auto best = gth::captcha::IndexOfNewestPatch(names);
    if (!best || !valid[*best]) return false;
    outRect = rects[*best];
    return true;
}

// Whether the patch's "SC" download button is visible yet - the same exact-Name match
// SonyLinkCopyWatcher's own FindScLinkValue looks for before it reads and copies the link.
// Existence only (no value read - SonyLinkCopyWatcher, polling independently, owns that): this is
// just the signal that lets the caller know it's now safe to switch away from this tab without
// racing SonyLinkCopyWatcher's own poll, which only ever reads whichever tab happens to be
// foreground *at the moment it ticks* and never revisits one it's already switched away from.
bool ScLinkElementShown(IUIAutomation* automation, IUIAutomationElement* root) {
    VARIANT nameVar;
    VariantInit(&nameVar);
    nameVar.vt = VT_BSTR;
    nameVar.bstrVal = ::SysAllocString(L"SC");

    ComPtr<IUIAutomationCondition> nameCondition;
    const HRESULT hr = automation->CreatePropertyCondition(UIA_NamePropertyId, nameVar, &nameCondition);
    ::VariantClear(&nameVar);
    if (FAILED(hr) || !nameCondition) return false;

    ComPtr<IUIAutomationElement> found;
    return SUCCEEDED(root->FindFirst(TreeScope_Descendants, nameCondition, &found)) && found;
}

// The site's own "Sorry, but the reCAPTCHA failed" message, shown instead of the patch's DP/SC/CRC
// links when its check doesn't accept an automated click - matched by substring against the exact
// phrase the userscript this replaces checked for via its ".alert-danger" element (that CSS class
// isn't reachable via UI Automation; the rendered text is). Unscoped scan: unlike the patch
// heading, this text's control type wasn't verified live, so this doesn't assume any one type.
bool CaptchaRejectionShown(IUIAutomation* automation, IUIAutomationElement* root) {
    ComPtr<IUIAutomationCondition> trueCondition;
    if (FAILED(automation->CreateTrueCondition(&trueCondition)) || !trueCondition) return false;
    ComPtr<IUIAutomationElementArray> all;
    if (FAILED(root->FindAll(TreeScope_Descendants, trueCondition, &all)) || !all) return false;

    int count = 0;
    all->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(all->GetElement(i, &element)) || !element) continue;
        BSTR name = nullptr;
        if (FAILED(element->get_CurrentName(&name)) || !name) continue;
        const bool matches = wcsstr(name, L"reCAPTCHA failed") != nullptr;
        ::SysFreeString(name);
        if (matches) return true;
    }
    return false;
}

}  // namespace

PatchAutoClicker::PatchAutoClicker() = default;

PatchAutoClicker::~PatchAutoClicker() { Stop(); }

void PatchAutoClicker::Start(HWND notifyWindow, UINT notifyMessage) {
    if (running_.exchange(true)) return;
    notifyWindow_ = notifyWindow;
    notifyMessage_ = notifyMessage;
    thread_ = std::make_unique<std::thread>([this] { Worker(); });
}

void PatchAutoClicker::Stop() {
    running_ = false;
    if (thread_ && thread_->joinable()) thread_->join();
    thread_.reset();
}

bool PatchAutoClicker::IsRunning() const { return running_; }

void PatchAutoClicker::Worker() {
    const HRESULT comInit = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(comInit);
    struct ComGuard {
        bool active;
        ~ComGuard() { if (active) ::CoUninitialize(); }
    } comGuard{comInitialized};
    if (!comInitialized && comInit != RPC_E_CHANGED_MODE) {
        running_ = false;
        return;
    }

    ComPtr<IUIAutomation> automation;
    if (FAILED(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                   reinterpret_cast<void**>(&automation))) ||
        !automation) {
        running_ = false;
        return;
    }

    std::set<std::wstring> clickedTitles;  // never click the same page's patch twice this session

    // Consecutive times a selected tab's own patch heading couldn't be found yet - a freshly
    // switched-to tab needs a moment to render and warm up its accessibility tree (same lazy
    // behavior observed for the very first tab, just paid for again per tab here), so this isn't
    // treated as "no patches" until it's failed enough times in a row to rule that out. Bounds how
    // long one stubborn (or genuinely patch-less) tab can hold up every other open tab behind it.
    std::unordered_map<std::wstring, int> warmupAttempts;
    constexpr int kMaxWarmupAttempts = 5;  // ~3.5s of retries (5 * the 700ms tick below)

    // Matches this app's own retry convention elsewhere (packageSync's own batch retries: the first
    // try, then two more) - three total tries at a title before leaving it alone for the rest of the
    // session. See failureCounts_'s own comment for why this has to survive a restart, not just
    // last one Worker() invocation the way clickedTitles/warmupAttempts above do.
    constexpr int kMaxAttemptsPerTitle = 3;

    // Sends the right notification for one non-success outcome (rejected, no patch, or timed out)
    // and tracks the running total for `titleId` - once it reaches kMaxAttemptsPerTitle, sends
    // wParam 4 (permanently giving up) instead of `attemptWParam`, so the person is told plainly
    // once that this one needs them, rather than the same "will retry" message repeating forever
    // for a page that was never going to resolve on its own.
    const auto reportFailure = [this, kMaxAttemptsPerTitle](const std::wstring& titleId, WPARAM attemptWParam) {
        const int count = ++failureCounts_[titleId];
        if (!notifyWindow_) return;
        const WPARAM wParam = (count >= kMaxAttemptsPerTitle) ? 4 : attemptWParam;
        ::PostMessageW(notifyWindow_, notifyMessage_, wParam, reinterpret_cast<LPARAM>(new std::wstring(titleId)));
    };

    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(700));

        HWND browserWindow = ForegroundBrowserWindow();
        if (!browserWindow) continue;  // only ever acts while a Chromium window genuinely has OS focus

        ComPtr<IUIAutomationElement> windowElement;
        if (FAILED(automation->ElementFromHandle(browserWindow, &windowElement)) || !windowElement) continue;

        // Picks up wherever the last tick left off: the same not-yet-clicked tab if it's still
        // warming up, or the next one in the strip once this one is done (clicked, or given up on).
        std::wstring titleId;
        if (!SelectNextUnclickedTab(automation, windowElement, clickedTitles, titleId)) continue;

        // Already failed kMaxAttemptsPerTitle times, possibly across an earlier restart of this
        // whole object - leave it alone for the rest of the session (silently; the wParam-4
        // notification already went out the moment it crossed the threshold, see reportFailure).
        if (const auto it = failureCounts_.find(titleId); it != failureCounts_.end() && it->second >= kMaxAttemptsPerTitle) {
            clickedTitles.insert(titleId);
            continue;
        }

        RECT patchRect{};
        if (!FindNewestPatchRect(automation, windowElement, patchRect)) {
            if (++warmupAttempts[titleId] >= kMaxWarmupAttempts) {
                clickedTitles.insert(titleId);  // give up this attempt - move on
                warmupAttempts.erase(titleId);
                reportFailure(titleId, 2);  // otherwise silent: nothing else ever says why this game never gets a link
            }
            continue;
        }

        clickedTitles.insert(titleId);  // counts as attempted whether or not the site accepts it
        warmupAttempts.erase(titleId);

        const int x = (patchRect.left + patchRect.right) / 2;
        const int y = (patchRect.top + patchRect.bottom) / 2;
        {
            std::lock_guard<std::mutex> mouseLock(MouseAutomationMutex());
            ::SetForegroundWindow(browserWindow);  // reassert - focus could have shifted since the checks above
            MouseInput::ClickAtPosition(x, y);
        }
        if (notifyWindow_) {
            ::PostMessageW(notifyWindow_, notifyMessage_, 0, reinterpret_cast<LPARAM>(new std::wstring(titleId)));
        }

        // Stay on this tab - don't let the next tick switch away yet - until either the SC link
        // becomes visible (SonyLinkCopyWatcher, polling independently, can only ever read it while
        // this tab is the one in front, so it must get a real chance to see it here) or the site's
        // captcha rejects the click, whichever happens first; give up after a bounded wait in case
        // the page is just being slow, rather than holding up every other tab behind this one.
        // Moving on before either of these actually showed up was exactly what let a game's link go
        // silently missing after several others had already succeeded normally.
        // ScLinkElementShown is a single scoped exact-Name lookup (cheap); CaptchaRejectionShown
        // is an unscoped scan of the whole page (its rejection text's own control type was never
        // verified live, so it can't be scoped the way the patch heading search above is) - on the
        // happy path, where SC shows up within a couple of seconds, checking that expensive one on
        // every single 100ms tick would burn several full-page scans for nothing. Checked only
        // every 5th tick (~500ms) instead: a rejection banner appearing doesn't need 100ms-precision
        // detection, so trading a little latency there for a lot less work here is free.
        constexpr int kMaxPostClickChecks = 60;  // 60 * 100ms = 6s
        bool linkShown = false;
        bool rejected = false;
        for (int i = 0; i < kMaxPostClickChecks && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (ScLinkElementShown(automation, windowElement)) {
                linkShown = true;
                break;
            }
            if (i % 5 == 4 && CaptchaRejectionShown(automation, windowElement)) {
                rejected = true;
                break;
            }
        }
        // linkShown: success - SonyLinkCopyWatcher owns reporting that. Otherwise - rejected, or
        // neither ever showed up within the wait (wParam 3: couldn't confirm what happened) - report
        // it, so this doesn't just silently sit there the way it used to when neither outcome fired.
        if (running_ && !linkShown) {
            reportFailure(titleId, rejected ? 1 : 3);
        }
    }
}
