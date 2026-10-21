#pragma once

// Shared OS-level mouse synthesis for every native mouse-driving automation in this module
// (CaptchaContextService's account-reveal click, PatchAutoClicker's patch click) - one place so
// timing/behavior can't quietly drift between copies. A real SetCursorPos + mouse_event, not a
// DOM .click() - indistinguishable to a page's JS from a genuine click.
//
// Every caller must hold MouseAutomationMutex() (MouseAutomationLock.h) around its whole click
// sequence; this class does no locking of its own, so two callers could otherwise drive the real
// cursor at the same moment and send each other's click to the wrong place.
class MouseInput {
public:
    static void MoveMouseSmooth(int targetX, int targetY, int durationMs = 700);
    static void ClickMouse();
    static void ClickAtPosition(int x, int y);
};
