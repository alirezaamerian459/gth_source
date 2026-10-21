#include "BrowserWindow.h"

#include <cwchar>
#include <thread>

HWND ForegroundBrowserWindow() {
    HWND candidate = ::GetForegroundWindow();
    wchar_t className[64] = {0};
    if (candidate && ::GetClassNameW(candidate, className, 64) &&
        wcscmp(className, L"Chrome_WidgetWin_1") == 0) {
        return candidate;
    }
    return nullptr;
}

HWND WaitForBrowserWindow(int maxAttempts, std::chrono::milliseconds pollInterval) {
    for (int i = 0; i < maxAttempts; ++i) {
        if (HWND found = ForegroundBrowserWindow()) return found;
        std::this_thread::sleep_for(pollInterval);
    }
    return nullptr;
}
