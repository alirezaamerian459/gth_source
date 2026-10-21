#include "gth/core/enums/TransferStatus.h"

namespace gth::core {

std::string toString(TransferStatus status) {
    switch (status) {
        case TransferStatus::Queued: return "Queued";
        case TransferStatus::Preparing: return "Preparing";
        case TransferStatus::Transferring: return "Transferring";
        case TransferStatus::Paused: return "Paused";
        case TransferStatus::Verifying: return "Verifying";
        case TransferStatus::Completed: return "Completed";
        case TransferStatus::Failed: return "Failed";
        case TransferStatus::Cancelled: return "Cancelled";
        default: return "Queued";
    }
}

TransferStatus transferStatusFromString(std::string_view text) {
    if (text == "Queued") return TransferStatus::Queued;
    if (text == "Preparing") return TransferStatus::Preparing;
    if (text == "Transferring") return TransferStatus::Transferring;
    if (text == "Paused") return TransferStatus::Paused;
    if (text == "Verifying") return TransferStatus::Verifying;
    if (text == "Completed") return TransferStatus::Completed;
    if (text == "Failed") return TransferStatus::Failed;
    if (text == "Cancelled") return TransferStatus::Cancelled;
    return TransferStatus::Queued;
}

}  // namespace gth::core
