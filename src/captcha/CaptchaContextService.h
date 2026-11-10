#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

// Handles automatic mouse movements to show Google account & WiFi during captcha solving.
// When a captcha is detected during guided link collection, this service:
// 1. Moves mouse to Google account profile (top-right of browser)
// 2. Clicks account button to show which account is logged in
// 3. Waits 2 seconds for user to verify
// 4. Returns control to user for captcha solving

class CaptchaContextService {
public:
    static CaptchaContextService& Instance();

    // Called when guided link collection detects a captcha on screen.
    // Automatically moves mouse and clicks account + WiFi, then returns.
    // Safe to call from any thread.
    void ShowAccountAndWiFiOnCaptcha();

    // Enable/disable the service (default: enabled).
    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    // For testing: check if a process window is visible.
    static bool IsBrowserWindowActive(const std::wstring& title);

    // Cleanup resources.
    ~CaptchaContextService();

private:
    CaptchaContextService();

    struct ScreenInfo {
        int Width = 0;
        int Height = 0;
    };

    // Get current screen dimensions.
    static ScreenInfo GetScreenDimensions();

    // Finds the browser's account/profile button via UI Automation (works regardless of
    // window position, size or pinned extensions) and returns its center point.
    // Returns false if no browser window or button could be found.
    static bool LocateBrowserProfileButton(int& outX, int& outY);

    // Press Escape key to close menus.
    static void PressEscapeKey();

    // Serialize the click/escape sequence with other mouse automation.
    static void ClickAccountAndClose(int accountX, int accountY);

    // Internal execution on worker thread.
    void ExecuteShowAccountAndWiFi();

    std::atomic<bool> enabled_{true};
    std::atomic<bool> isRunning_{false};
};
