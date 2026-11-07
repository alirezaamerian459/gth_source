#include "SonyLinkCopyWatcher.h"
#include "BrowserWindow.h"
#include "ComPtr.h"
#include "PatchAutoClickLogic.h"

#include <windows.h>
#include <UIAutomationClient.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <ctime>
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "UIAutomationCore.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

// Finds the first descendant whose accessible Name is exactly "SC" and reads its href
// straight from its Value/LegacyIAccessible pattern - the same underlying data a click would
// eventually copy, but read directly instead of by clicking through Chrome's own (locale-
// dependent!) context menu. "Copy link address" is Chrome UI text: on a non-English Chrome
// it's translated, so matching that exact English string there would silently never find it,
// forever, on that install - this sidesteps the whole problem by never depending on menu text.
//
// Filters by Name via a UIA property condition (evaluated by the provider) rather than pulling
// every element on the page across the COM boundary just to compare its Name in-process - this
// runs every 700ms for the entire session, unconditionally, so it's the single most expensive call
// in this module; scoping it here is a real, measurable win rather than a cosmetic one.
bool FindScLinkValue(IUIAutomation* automation, IUIAutomationElement* root, std::wstring& outUrl) {
    VARIANT nameVar;
    VariantInit(&nameVar);
    nameVar.vt = VT_BSTR;
    nameVar.bstrVal = ::SysAllocString(L"SC");
    ComPtr<IUIAutomationCondition> nameCondition;
    const HRESULT hr = automation->CreatePropertyCondition(UIA_NamePropertyId, nameVar, &nameCondition);
    ::VariantClear(&nameVar);
    if (FAILED(hr) || !nameCondition) return false;

    ComPtr<IUIAutomationElementArray> all;
    if (FAILED(root->FindAll(TreeScope_Descendants, nameCondition, &all)) || !all) return false;

    int count = 0;
    all->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(all->GetElement(i, &element)) || !element) continue;

        // (v5.8.3 tried skipping anything IUIAutomation reports as offscreen here, to dodge a
        // stale/closed dialog's node - that broke the *real*, currently-open link too, so a
        // whole game's SC link went uncopied instead of just the stale one. Reverted in v5.8.4:
        // do not reintroduce an offscreen/visibility filter here without a way to verify it
        // against the real site first.)

        // Chromium computes one accessibility value for a link's href, but which COM pattern
        // exposes it isn't guaranteed the same across Chrome builds: try native UIA's
        // ValuePattern first, then fall back to LegacyIAccessiblePattern (the IAccessible2/MSAA
        // accValue screen readers have used to read link URLs for decades). Either one, when
        // present, mirrors the same href "Copy link address" would put on the clipboard - this
        // never guesses or reconstructs the URL itself.
        std::wstring candidate;

        ComPtr<IUIAutomationValuePattern> valuePattern;
        if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern))) && valuePattern) {
            BSTR value = nullptr;
            if (SUCCEEDED(valuePattern->get_CurrentValue(&value)) && value) {
                candidate.assign(value);
                ::SysFreeString(value);
            }
        }

        if (candidate.empty()) {
            ComPtr<IUIAutomationLegacyIAccessiblePattern> legacyPattern;
            if (SUCCEEDED(element->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId, IID_PPV_ARGS(&legacyPattern))) &&
                legacyPattern) {
                BSTR value = nullptr;
                if (SUCCEEDED(legacyPattern->get_CurrentValue(&value)) && value) {
                    candidate.assign(value);
                    ::SysFreeString(value);
                }
            }
        }

        if (!candidate.starts_with(L"http")) continue;  // neither pattern gave a usable URL - keep looking
        outUrl = std::move(candidate);
        return true;
    }
    return false;
}

// Sony's game-update URLs all put the title ID immediately before "_00/" (see
// FindScLinkValue's callers and gth::network::ExtractSonyAddresses, which parses the same
// shape) - e.g. ".../PPSA31219_00/app/info/...". Pulled out locally rather than taking a
// dependency on gth_network just for this one substring.
std::wstring ExtractTitleIdFromScUrl(const std::wstring& url) {
    const size_t marker = url.find(L"_00/");
    if (marker == std::wstring::npos) return {};
    size_t start = marker;
    while (start > 0 && std::iswalnum(url[start - 1])) --start;
    if (start == marker) return {};
    return url.substr(start, marker - start);
}

enum class CloseTabOutcome { NotFound, StillActive, Closed, ClosedFailed };

// Finds the open tab whose title starts with "<titleId>:" (Chrome's tab strip shows exactly the
// page's own <title>, which prosperopatches.com sets to "<TITLEID>: <game name>") and closes it
// by invoking its own close (x) button - by default never the currently-selected tab, so nothing
// is ever pulled out from under someone actually looking at it (the one caller that ever passes
// `evenIfSelected=true` is the final "the whole session is over, close everything" sweep, where
// that's exactly what should happen instead). Structural throughout (control type and selection
// state, never a button's text label), so unlike a "Copy link address" menu match this doesn't
// depend on Chrome's UI language. Never drives the mouse or keyboard, and never switches which
// tab is active, so it can't interrupt a click in progress on another tab.
CloseTabOutcome TryCloseFinishedTab(IUIAutomation* automation, IUIAutomationElement* windowElement,
                                    const std::wstring& titleIdPrefix, bool evenIfSelected = false) {
    VARIANT tabType;
    VariantInit(&tabType);
    tabType.vt = VT_I4;
    tabType.lVal = UIA_TabItemControlTypeId;

    ComPtr<IUIAutomationCondition> tabCondition;
    if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, tabType, &tabCondition)) || !tabCondition) {
        return CloseTabOutcome::NotFound;
    }

    ComPtr<IUIAutomationElementArray> tabs;
    if (FAILED(windowElement->FindAll(TreeScope_Descendants, tabCondition, &tabs)) || !tabs) return CloseTabOutcome::NotFound;

    int count = 0;
    tabs->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> tab;
        if (FAILED(tabs->GetElement(i, &tab)) || !tab) continue;

        BSTR name = nullptr;
        if (FAILED(tab->get_CurrentName(&name)) || !name) continue;
        const bool matches = wcsncmp(name, titleIdPrefix.c_str(), titleIdPrefix.size()) == 0;
        ::SysFreeString(name);
        if (!matches) continue;

        BOOL isSelected = FALSE;
        ComPtr<IUIAutomationSelectionItemPattern> selectionItem;
        if (SUCCEEDED(tab->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selectionItem))) && selectionItem) {
            selectionItem->get_CurrentIsSelected(&isSelected);
        }
        if (isSelected && !evenIfSelected) return CloseTabOutcome::StillActive;

        VARIANT buttonType;
        VariantInit(&buttonType);
        buttonType.vt = VT_I4;
        buttonType.lVal = UIA_ButtonControlTypeId;
        ComPtr<IUIAutomationCondition> buttonCondition;
        if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, buttonType, &buttonCondition)) ||
            !buttonCondition) {
            return CloseTabOutcome::ClosedFailed;
        }
        ComPtr<IUIAutomationElement> closeButton;
        if (FAILED(tab->FindFirst(TreeScope_Descendants, buttonCondition, &closeButton)) || !closeButton) {
            return CloseTabOutcome::ClosedFailed;
        }
        ComPtr<IUIAutomationInvokePattern> invoke;
        if (FAILED(closeButton->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) || !invoke) {
            return CloseTabOutcome::ClosedFailed;
        }
        return SUCCEEDED(invoke->Invoke()) ? CloseTabOutcome::Closed : CloseTabOutcome::ClosedFailed;
    }
    return CloseTabOutcome::NotFound;
}

// "PPSA" + exactly 5 digits - a PS5 title ID, and specifically not just "anything with a colon in
// its title" (which any unrelated tab the person happens to have open could also have). Used to
// keep the 15-tab cap below from ever mistaking someone else's tab for one of ours.
bool LooksLikePs5TitleId(const std::wstring& s) {
    if (s.size() != 9) return false;
    if (towupper(s[0]) != L'P' || towupper(s[1]) != L'P' || towupper(s[2]) != L'S' || towupper(s[3]) != L'A') return false;
    for (size_t i = 4; i < s.size(); ++i) {
        if (!iswdigit(s[i])) return false;
    }
    return true;
}

// Every currently open tab's title ID, in tab-strip (left-to-right, i.e. open-order-ish) order.
// Used to track how many of our own tabs are open right now and which has been open longest - the
// close-by-titleId-prefix action itself is still TryCloseFinishedTab above, targeted at one ID.
std::vector<std::wstring> OpenPs5TitleIdsInStripOrder(IUIAutomation* automation, IUIAutomationElement* windowElement) {
    std::vector<std::wstring> ids;

    VARIANT tabType;
    VariantInit(&tabType);
    tabType.vt = VT_I4;
    tabType.lVal = UIA_TabItemControlTypeId;

    ComPtr<IUIAutomationCondition> tabCondition;
    if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, tabType, &tabCondition)) || !tabCondition) {
        return ids;
    }
    ComPtr<IUIAutomationElementArray> tabs;
    if (FAILED(windowElement->FindAll(TreeScope_Descendants, tabCondition, &tabs)) || !tabs) return ids;

    int count = 0;
    tabs->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> tab;
        if (FAILED(tabs->GetElement(i, &tab)) || !tab) continue;
        BSTR name = nullptr;
        if (FAILED(tab->get_CurrentName(&name)) || !name) continue;
        const std::wstring titleId = gth::captcha::TitleIdFromWindowTitle(name);
        ::SysFreeString(name);
        if (LooksLikePs5TitleId(titleId)) ids.push_back(titleId);
    }
    return ids;
}

// Puts `text` on the clipboard directly (no click involved at all).
bool SetClipboardText(const std::wstring& text) {
    if (!::OpenClipboard(nullptr)) return false;
    ::EmptyClipboard();

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        ::CloseClipboard();
        return false;
    }
    if (void* dst = ::GlobalLock(mem)) {
        memcpy(dst, text.c_str(), bytes);
        ::GlobalUnlock(mem);
    }
    // On success the clipboard now owns `mem`; only free it if the handoff failed.
    const bool ok = ::SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
    if (!ok) ::GlobalFree(mem);
    ::CloseClipboard();
    return ok;
}

// "YYYY-MM-DD HH:MM:SS" in local time, for a human skimming the log file - not parsed by
// anything, so no format needs to match elsewhere.
std::string LocalTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::array<char, 32> buffer;
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", &local);
    return buffer.data();
}

// Creates a Desktop shortcut to `targetFile`, named "GTH Latest Update URL.lnk", the first time
// this ever runs on this machine - so whoever's running the app can get to the log without
// knowing it lives under %LocalAppData%, or digging through the app's own menu. Resolves the
// current user's real Desktop folder itself (never a hardcoded path), so this is correct
// wherever the app happens to run. A no-op once the shortcut exists (checked by path, not by
// re-validating where it points), and best effort throughout: nothing here is allowed to affect
// the clipboard copy or the log write it rides along with.
void EnsureDesktopShortcut(const std::filesystem::path& targetFile) {
    PWSTR desktopPath = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath)) || !desktopPath) return;
    const std::filesystem::path shortcutPath = std::filesystem::path(desktopPath) / L"GTH Latest Update URL.lnk";
    ::CoTaskMemFree(desktopPath);

    std::error_code ec;
    if (std::filesystem::exists(shortcutPath, ec)) return;

    ComPtr<IShellLinkW> shellLink;
    if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                   reinterpret_cast<void**>(&shellLink))) ||
        !shellLink) {
        return;
    }
    shellLink->SetPath(targetFile.c_str());
    shellLink->SetDescription(L"GTH: every SC link the guided collector has copied");

    ComPtr<IPersistFile> persistFile;
    if (FAILED(shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile))) || !persistFile) {
        return;
    }
    persistFile->Save(shortcutPath.c_str(), TRUE);
}

// Appends one "<timestamp>\t<url>" line, creating the file (and its folder) if needed, and makes
// sure the Desktop shortcut to it exists. Best effort throughout: a failure here shouldn't stop
// the clipboard copy that already succeeded, so none of this is ever treated as the operation's
// result or allowed to throw.
void AppendToLogFile(const std::filesystem::path& path, const std::wstring& url) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::wofstream file(path, std::ios::app);
    if (!file) return;
    file << LocalTimestamp().c_str() << L"\t" << url << L"\n";
    file.close();

    EnsureDesktopShortcut(path);
}

}  // namespace

SonyLinkCopyWatcher::SonyLinkCopyWatcher() = default;

SonyLinkCopyWatcher::~SonyLinkCopyWatcher() {
    Stop();
}

void SonyLinkCopyWatcher::Start(HWND notifyWindow, UINT notifyMessage, std::filesystem::path logFilePath) {
    if (running_.exchange(true)) return;
    notifyWindow_ = notifyWindow;
    notifyMessage_ = notifyMessage;
    logFilePath_ = std::move(logFilePath);
    thread_ = std::make_unique<std::thread>([this] { Worker(); });
}

void SonyLinkCopyWatcher::Stop(bool closeAllTabs) {
    if (closeAllTabs) closeTabsOnStop_.store(true);
    running_ = false;
    if (thread_ && thread_->joinable()) thread_->join();
    thread_.reset();
}

bool SonyLinkCopyWatcher::IsRunning() const {
    return running_;
}

std::vector<std::string> SonyLinkCopyWatcher::CurrentlyOpenTitleIds() const {
    std::lock_guard<std::mutex> lock(openTabsMutex_);
    return openTabsSnapshot_;
}

void SonyLinkCopyWatcher::Worker() {
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

    // Tracks the URL last put on the clipboard by this watcher (not just whether *something*
    // was visible), so it copies again whenever a *different* game's link appears - including
    // when a stale element for the previous game never truly goes away (see FindScLinkValue) -
    // while never redundantly re-copying the same link every tick while its dialog stays open.
    std::wstring lastCopiedUrl;

    // Games whose link has already been copied, and when their tab becomes eligible to close
    // (copy time + 1 minute) if it's just sitting there afterward. Only tabs this watcher itself
    // finished with ever land here - a tab still waiting for its own patch click never does.
    constexpr auto kCloseIdleAfter = std::chrono::seconds(60);
    std::unordered_map<std::wstring, std::chrono::steady_clock::time_point> pendingClose;

    // Every one of our own tabs still open, oldest-first (approximated by when each was first seen
    // in the tab strip - close enough at this poll cadence). A separate, harder cap from the
    // minute-after-its-own-link-is-copied rule just above: that one only ever closes a tab once its
    // own job is done, so on its own nothing stops the total climbing well past a sensible number
    // if tabs open faster than links come in. This instead closes the single oldest tab - once a
    // 16th would otherwise be open - but ONLY if that tab is already in `pendingClose` (its link
    // already copied, just waiting out its own grace period): a tab whose patch is still loading,
    // still waiting for its own click, or whose SC link hasn't been read yet must never be closed
    // by this, or its link is lost the moment the page closes, with no way to get it back. (An
    // earlier version of this closed strictly by open time with no such check, which is exactly
    // what could make auto-copying silently stop working partway through a long session - the
    // oldest tab is often the very one just clicked and still waiting for its reveal.) If every
    // tracked tab is still in progress, this simply doesn't close anything that tick - a session
    // can briefly run past 15 tabs rather than ever risk losing a game's link.
    constexpr size_t kMaxOpenTabs = 15;
    std::vector<std::wstring> openOrder;

    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(700));

        HWND browserWindow = ForegroundBrowserWindow();
        if (!browserWindow) continue;

        ComPtr<IUIAutomationElement> windowElement;
        if (FAILED(automation->ElementFromHandle(browserWindow, &windowElement)) || !windowElement) continue;

        std::wstring scUrl;
        if (FindScLinkValue(automation, windowElement, scUrl) && scUrl != lastCopiedUrl) {
            // A new patch's download links were just revealed (by the person's own click) - no
            // mouse or menu involved here at all, just read and copy.
            if (SetClipboardText(scUrl)) {
                lastCopiedUrl = scUrl;
                AppendToLogFile(logFilePath_, scUrl);
                if (const auto titleId = ExtractTitleIdFromScUrl(scUrl); !titleId.empty()) {
                    pendingClose[titleId] = std::chrono::steady_clock::now() + kCloseIdleAfter;
                }
                if (notifyWindow_) ::PostMessageW(notifyWindow_, notifyMessage_, 0, 0);
            }
            // If the clipboard write itself failed (another app briefly owns it), leave
            // lastCopiedUrl unset so the next tick retries instead of missing this reveal.
        }

        // Sweep tabs whose grace period is up. A tab still selected (the person is looking at
        // it right now, whatever the clock says) is left alone and checked again later; every
        // other outcome - closed, or not found because it's already gone - stops tracking it.
        const auto now = std::chrono::steady_clock::now();
        for (auto it = pendingClose.begin(); it != pendingClose.end();) {
            if (now < it->second) {
                ++it;
                continue;
            }
            if (TryCloseFinishedTab(automation, windowElement, it->first + L":") == CloseTabOutcome::StillActive) {
                ++it;
                continue;
            }
            it = pendingClose.erase(it);
        }

        // The 15-tab cap: refresh which of our tabs are still open (dropping any this watcher, the
        // sweep above, or the person themselves has already closed), add any newly-opened one to
        // the back, then - only once there are 15 or more - close the single oldest. Left at one
        // close per tick on purpose: if a burst of new tabs ever pushes the count well past 15,
        // this brings it down by one every ~700ms rather than all at once.
        const auto currentTabs = OpenPs5TitleIdsInStripOrder(automation, windowElement);
        {
            // Every title ID is "PPSA" + 5 digits (LooksLikePs5TitleId already guarantees pure
            // ASCII) - a plain per-character narrow-cast is exact here, not just an approximation
            // a real UTF-8 conversion would be needed for.
            std::vector<std::string> snapshot;
            snapshot.reserve(currentTabs.size());
            for (const auto& id : currentTabs) snapshot.emplace_back(id.begin(), id.end());
            std::lock_guard<std::mutex> lock(openTabsMutex_);
            openTabsSnapshot_ = std::move(snapshot);
        }
        const std::set<std::wstring> stillOpen(currentTabs.begin(), currentTabs.end());
        openOrder.erase(std::remove_if(openOrder.begin(), openOrder.end(),
                                       [&](const std::wstring& id) { return !stillOpen.contains(id); }),
                        openOrder.end());
        const std::set<std::wstring> alreadyTracked(openOrder.begin(), openOrder.end());
        for (const auto& id : currentTabs) {
            if (!alreadyTracked.contains(id)) openOrder.push_back(id);
        }

        if (openOrder.size() >= kMaxOpenTabs) {
            for (auto it = openOrder.begin(); it != openOrder.end(); ++it) {
                if (!pendingClose.contains(*it)) continue;  // link not copied yet - never close this one here
                if (TryCloseFinishedTab(automation, windowElement, *it + L":") == CloseTabOutcome::StillActive) {
                    continue;  // the one being looked at right now - try the next-oldest instead
                }
                openOrder.erase(it);
                break;  // exactly one close per tick, whether it succeeded or genuinely couldn't be found
            }
        }
    }

    // The session is over (Stop(true) - see its own doc comment): close every one of our own tabs
    // still open, this time including whichever is currently selected. One pass, best effort - a
    // tab Chrome won't let go of (e.g. mid-navigation) is simply left for the person to close
    // themselves rather than retried, since nothing is ticking anymore to retry it later.
    if (closeTabsOnStop_.exchange(false)) {
        if (HWND browserWindow = ForegroundBrowserWindow()) {
            ComPtr<IUIAutomationElement> windowElement;
            if (SUCCEEDED(automation->ElementFromHandle(browserWindow, &windowElement)) && windowElement) {
                for (const auto& id : OpenPs5TitleIdsInStripOrder(automation, windowElement)) {
                    TryCloseFinishedTab(automation, windowElement, id + L":", /*evenIfSelected=*/true);
                }
            }
        }
    }
}
