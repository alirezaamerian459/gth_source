#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace gth::core {

struct TransferOutcome {
    bool success = false;
    std::string errorMessage;
};

// Abstraction over where a file copy actually goes: today, Local/SMB (a
// plain path or a \\host\share UNC path - Windows' file APIs treat both the
// same way, so one provider covers both). FTP and other protocols are
// future providers behind this same interface - see ARCHITECTURE.md for why
// FTP isn't implemented yet (it needs Credential Manager-backed password
// storage done properly, not rushed alongside this milestone).
class ITransferProvider {
public:
    virtual ~ITransferProvider() = default;

    virtual std::string name() const = 0;

    // cancelFlag, if non-null and set to true during the copy, aborts the
    // operation at the next opportunity the underlying API offers.
    virtual TransferOutcome copyFile(const std::string& sourcePath, const std::string& destinationPath,
                                      const std::function<void(long long, long long)>& onProgress,
                                      std::atomic<bool>* cancelFlag) = 0;
};

}  // namespace gth::core
