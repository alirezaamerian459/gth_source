#include "MouseInput.h"

#include <windows.h>

#include <chrono>
#include <thread>

namespace {

struct MouseHelper {
    static void SetCursorPos(int x, int y) { ::SetCursorPos(x, y); }

    static bool GetCursorPos(int& outX, int& outY) {
        POINT pt{};
        if (!::GetCursorPos(&pt)) return false;
        outX = pt.x;
        outY = pt.y;
        return true;
    }

    static void MouseEvent(DWORD dwFlags) { mouse_event(dwFlags, 0, 0, 0, 0); }
};

}  // namespace

void MouseInput::MoveMouseSmooth(int targetX, int targetY, int durationMs) {
    int startX = 0, startY = 0;
    if (!MouseHelper::GetCursorPos(startX, startY)) {
        // Fallback: just set position directly
        MouseHelper::SetCursorPos(targetX, targetY);
        return;
    }

    const int steps = 25;
    const int stepDurationMs = durationMs / steps;

    for (int i = 1; i <= steps; ++i) {
        const double progress = static_cast<double>(i) / steps;
        const int x = static_cast<int>(startX + (targetX - startX) * progress);
        const int y = static_cast<int>(startY + (targetY - startY) * progress);

        MouseHelper::SetCursorPos(x, y);
        std::this_thread::sleep_for(std::chrono::milliseconds(stepDurationMs));
    }

    // Ensure exact final position
    MouseHelper::SetCursorPos(targetX, targetY);
}

void MouseInput::ClickMouse() {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    MouseHelper::MouseEvent(MOUSEEVENTF_LEFTDOWN);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    MouseHelper::MouseEvent(MOUSEEVENTF_LEFTUP);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void MouseInput::ClickAtPosition(int x, int y) {
    MoveMouseSmooth(x, y, 700);
    ClickMouse();
}
