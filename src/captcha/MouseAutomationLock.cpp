#include "MouseAutomationLock.h"

std::mutex& MouseAutomationMutex() {
    static std::mutex mutex;
    return mutex;
}
