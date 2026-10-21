#include "gth/core/enums/NasStatus.h"

namespace gth::core {

std::string toString(NasStatus status) {
    switch (status) {
        case NasStatus::Online: return "Online";
        case NasStatus::Warning: return "Warning";
        case NasStatus::Offline: return "Offline";
        case NasStatus::Unreachable: return "Unreachable";
        case NasStatus::AuthenticationFailed: return "AuthenticationFailed";
        default: return "Offline";
    }
}

NasStatus nasStatusFromString(std::string_view text) {
    if (text == "Online") return NasStatus::Online;
    if (text == "Warning") return NasStatus::Warning;
    if (text == "Unreachable") return NasStatus::Unreachable;
    if (text == "AuthenticationFailed") return NasStatus::AuthenticationFailed;
    return NasStatus::Offline;
}

}  // namespace gth::core
