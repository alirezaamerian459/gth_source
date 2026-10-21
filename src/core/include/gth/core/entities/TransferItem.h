#pragma once

#include <optional>
#include <string>

#include "gth/core/Clock.h"
#include "gth/core/Guid.h"
#include "gth/core/enums/TransferStatus.h"

namespace gth::core {

// One file copy within a TransferJob. SourcePath/DestinationPath are
// whatever the chosen ITransferProvider understands - for the Local/SMB
// provider, plain or UNC filesystem paths (\\NAS\Games\... works exactly
// like a local path through the Win32 file APIs).
struct TransferItem {
    std::string transferItemId = Guid::newGuid();
    std::string transferJobId;

    std::string sourcePath;
    std::string destinationPath;
    long long sizeBytes = 0;
    long long bytesTransferred = 0;

    TransferStatus status = TransferStatus::Queued;
    std::optional<std::string> errorMessage;

    std::string createdAt = Clock::utcNowIso8601();
    std::string updatedAt = Clock::utcNowIso8601();
};

}  // namespace gth::core
