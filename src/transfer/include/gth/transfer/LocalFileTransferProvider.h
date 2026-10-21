#pragma once

#include "gth/core/transfer/ITransferProvider.h"

namespace gth::transfer {

// Covers both plain local paths and SMB/UNC paths (\\host\share\...) - the
// Win32 file APIs treat them identically, so one CopyFileEx-based provider
// is all either case needs. Progress and cancellation both come from
// CopyFileEx's native progress-routine callback, not a hand-rolled read/
// write loop.
class LocalFileTransferProvider : public core::ITransferProvider {
public:
    std::string name() const override;

    core::TransferOutcome copyFile(const std::string& sourcePath, const std::string& destinationPath,
                                    const std::function<void(long long, long long)>& onProgress,
                                    std::atomic<bool>* cancelFlag) override;
};

}  // namespace gth::transfer
