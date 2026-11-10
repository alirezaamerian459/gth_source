#include "CaptchaContextService.h"
#include "BrowserWindow.h"
#include "ComPtr.h"
#include "MouseAutomationLock.h"
#include "MouseInput.h"

#include <windows.h>
#include <UIAutomationClient.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#pragma comment(lib, "UIAutomationCore.lib")

namespace {

// Anchors on the "Back" navigation button's row (always present) and takes the two
// rightmost buttons in that row: the rightmost is always the browser's menu (3-dot)
// button, and the profile/avatar button sits immediately to its left. This adapts to
// window size/position/pinned extensions instead of relying on fixed pixel offsets.
bool FindProfileButtonRect(IUIAutomation* automation, IUIAutomationElement* windowElement, RECT& outRect) {
    VARIANT buttonType;
    VariantInit(&buttonType);
    buttonType.vt = VT_I4;
    buttonType.lVal = UIA_ButtonControlTypeId;

    ComPtr<IUIAutomationCondition> buttonCondition;
    if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, buttonType, &buttonCondition)) ||
        !buttonCondition) {
        return false;
    }

    ComPtr<IUIAutomationElementArray> buttons;
    if (FAILED(windowElement->FindAll(TreeScope_Descendants, buttonCondition, &buttons)) || !buttons) {
        return false;
    }

    int count = 0;
    buttons->get_Length(&count);
    if (count <= 0) return false;

    std::vector<RECT> rects(count);
    std::vector<bool> valid(count, false);
    bool haveBackTop = false;
    LONG backTop = 0;

    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> element;
        if (FAILED(buttons->GetElement(i, &element)) || !element) continue;

        RECT rect{};
        if (FAILED(element->get_CurrentBoundingRectangle(&rect))) continue;
        rects[i] = rect;
        valid[i] = true;

        BSTR name = nullptr;
        if (SUCCEEDED(element->get_CurrentName(&name)) && name) {
            if (!haveBackTop && wcscmp(name, L"Back") == 0) {
                backTop = rect.top;
                haveBackTop = true;
            }
            ::SysFreeString(name);
        }
    }

    if (!haveBackTop) return false;

    std::vector<RECT> row;
    for (int i = 0; i < count; ++i) {
        if (valid[i] && std::abs(static_cast<long>(rects[i].top - backTop)) < 10) {
            row.push_back(rects[i]);
        }
    }
    if (row.size() < 2) return false;

    std::sort(row.begin(), row.end(), [](const RECT& a, const RECT& b) { return a.right > b.right; });
    outRect = row[1];  // row[0] is the browser's menu button; row[1] is the profile button
    return true;
}

}  // namespace

CaptchaContextService& CaptchaContextService::Instance() {
    static CaptchaContextService instance;
    return instance;
}

CaptchaContextService::CaptchaContextService() = default;

CaptchaContextService::~CaptchaContextService() = default;

CaptchaContextService::ScreenInfo CaptchaContextService::GetScreenDimensions() {
    ScreenInfo info;
    info.Width = GetSystemMetrics(SM_CXSCREEN);
    info.Height = GetSystemMetrics(SM_CYSCREEN);
    return info;
}

bool CaptchaContextService::LocateBrowserProfileButton(int& outX, int& outY) {
    HWND browserWindow = WaitForBrowserWindow();
    if (!browserWindow) return false;

    const HRESULT comInit = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(comInit);
    struct ComGuard {
        bool active;
        ~ComGuard() { if (active) ::CoUninitialize(); }
    } comGuard{comInitialized};
    if (!comInitialized && comInit != RPC_E_CHANGED_MODE) return false;

    ComPtr<IUIAutomation> automation;
    if (FAILED(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                   reinterpret_cast<void**>(&automation))) ||
        !automation) {
        return false;
    }

    ComPtr<IUIAutomationElement> windowElement;
    if (FAILED(automation->ElementFromHandle(browserWindow, &windowElement)) || !windowElement) {
        return false;
    }

    RECT profileRect{};
    if (!FindProfileButtonRect(automation, windowElement, profileRect)) return false;

    outX = (profileRect.left + profileRect.right) / 2;
    outY = (profileRect.top + profileRect.bottom) / 2;
    return true;
}

void CaptchaContextService::PressEscapeKey() {
    // This is a *global* keypress, not scoped to our dropdown - it goes wherever Windows
    // currently has keyboard focus. Only ever press it right after our own click, never on
    // a delay, so it can't land on something the person is now interacting with instead
    // (e.g. cancelling a reCAPTCHA challenge in another tab).
    keybd_event(VK_ESCAPE, 0, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    keybd_event(VK_ESCAPE, KEYEVENTF_KEYUP, 0, 0);
}

void CaptchaContextService::ClickAccountAndClose(int accountX, int accountY) {
    // Keep the lock's lifetime in a small helper. This makes the ownership
    // boundary explicit to static analysis while preserving the existing
    // serialized click/escape sequence.
    std::lock_guard<std::mutex> mouseLock(MouseAutomationMutex());
    MouseInput::ClickAtPosition(accountX, accountY);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    PressEscapeKey();
}

bool CaptchaContextService::IsBrowserWindowActive(const std::wstring& title) {
    HWND hwnd = FindWindowW(nullptr, title.c_str());
    return hwnd != nullptr && IsWindowVisible(hwnd);
}

void CaptchaContextService::SetEnabled(bool enabled) {
    enabled_ = enabled;
}

bool CaptchaContextService::IsEnabled() const {
    return enabled_;
}

void CaptchaContextService::ExecuteShowAccountAndWiFi() {
    if (!enabled_ || isRunning_.exchange(true)) {
        return;  // Already running or disabled
    }

    auto cleanup = [this]() { isRunning_ = false; };

    try {
        const auto screen = GetScreenDimensions();
        if (screen.Width <= 0 || screen.Height <= 0) {
            cleanup();
            return;
        }

        // Look up the browser's actual profile button via UI Automation; fall back to the
        // top-right corner (correct only when the browser is maximized with no pinned
        // extensions) if no browser window or button can be found in time.
        int accountX = screen.Width - 40;
        int accountY = 40;
        LocateBrowserProfileButton(accountX, accountY);

        // Click the account button, wait for the person to glance at it, then close it.
        // Deliberately doesn't also touch the WiFi icon: that click lands in the taskbar,
        // a different top-level window, which steals OS focus away from the browser - right
        // when a captcha might be loading or already in progress in another tab. Losing
        // that focus can both drop a page's clipboard write (the userscript falls back to
        // navigator.clipboard.writeText, which requires document focus) and, since Escape is
        // a global keypress, risks cancelling a reCAPTCHA challenge if focus lands there.
        //
        // Held only for this click sequence (not the earlier lookup), so it can't starve
        // SonyLinkCopyWatcher, which needs the same lock to copy a link the moment one
        // appears - without this, the two could drive the mouse at the same instant and
        // send each other's click to the wrong place.
        ClickAccountAndClose(accountX, accountY);

        cleanup();
    } catch (...) {
        cleanup();
    }
}

void CaptchaContextService::ShowAccountAndWiFiOnCaptcha() {
    if (!enabled_) return;

    // Run on worker thread to avoid blocking UI
    std::thread worker([this]() {
        ExecuteShowAccountAndWiFi();
    });
    worker.detach();
}
