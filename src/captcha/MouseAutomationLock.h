


#pragma once

#include <mutex>

// A single lock shared by every piece of native mouse automation in this module
// (CaptchaContextService's account reveal, SonyLinkCopyWatcher's link copying). Without it,
// two independent automations can drive the real OS cursor at the same moment - one moving
// it away mid-click of the other - causing a right-click or menu-item click to land wherever
// the other one just put the cursor instead of its intended target.
std::mutex& MouseAutomationMutex();
