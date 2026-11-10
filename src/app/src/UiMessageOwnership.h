#pragma once

#include <bit>
#include <memory>

#include "WtlCommon.h"

namespace gth::app::detail {

// Ownership transfers to the message handler only after Windows accepts the
// post. A failed post leaves the payload in this function and frees it safely.
template <class T>
bool postOwnedMessage(HWND window, UINT message, std::unique_ptr<T> payload) {
    if (!window || !payload) return false;
    static_assert(sizeof(LPARAM) == sizeof(T*));
    if (!::PostMessageW(window, message, 0, std::bit_cast<LPARAM>(payload.get()))) return false;
    payload.release();
    return true;
}

template <class T>
std::unique_ptr<T> takeOwnedMessage(LPARAM payload) noexcept {
    static_assert(sizeof(LPARAM) == sizeof(T*));
    return std::unique_ptr<T>(std::bit_cast<T*>(payload));
}

}  // namespace gth::app::detail
