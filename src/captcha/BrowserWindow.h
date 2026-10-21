#pragma once

#include <windows.h>

#include <chrono>

// The foreground window right now, if (and only if) it's a Chromium browser window (Chrome and
// Edge both use the frame class "Chrome_WidgetWin_1") - nullptr otherwise. No waiting; see
// WaitForBrowserWindow for a version that polls briefly for one to appear.
HWND ForegroundBrowserWindow();

// Polls for a Chromium browser window to become the foreground window (e.g. right after this
// process asks Windows to open a tracker page). Gives up (nullptr) after maxAttempts * pollInterval.
HWND WaitForBrowserWindow(int maxAttempts = 40, std::chrono::milliseconds pollInterval = std::chrono::milliseconds(150));
