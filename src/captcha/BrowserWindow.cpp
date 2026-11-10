#include "BrowserWindow.h"

#include <array>
#include <cwchar>
#include <thread>

HWND ForegroundBrowserWindow() {
    HWND candidate = ::GetForegroundWindow();
    std::array<wchar_t, 64> className{};
    if (candidate && ::GetClassNameW(candidate, className.data(), static_cast<int>(className.size())) &&
        wcscmp(className.data(), L"Chrome_WidgetWin_1") == 0) {
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
