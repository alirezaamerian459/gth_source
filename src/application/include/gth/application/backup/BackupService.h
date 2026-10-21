#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "gth/core/verification/IFileHasher.h"

namespace gth::application {

struct BackupGame {
    std::string id;
    std::string title;
    std::string platform;
    std::string version;
};

struct BackupCallbacks {
    // Return false to stop. Called between files/chunks and passed through to
    // source/transport adapters so active transfers can also be cancelled.
    std::function<bool()> shouldCancel;
    std::function<void(const std::string& stage, long long bytesDone, long long bytesTotal)> onProgress;
};

struct BackupResult {
    bool success = false;
    bool cancelled = false;
    std::string backupId;
    std::string manifestObjectId;
    long long bytesTransferred = 0;
    std::string error;
};

struct BackupManifestRef {
    std::string objectId;
    std::string backupId;
    std::string title;
    std::string platform;
    std::string version;
    std::string createdAt;
    long long totalBytes = 0;
};

struct BackupBatchResult {
    int backedUp = 0;
    int failed = 0;
    bool cancelled = false;
    std::vector<BackupResult> games;
};

// The configured source stages a selected game into the supplied directory.
// Hashing, chunking, upload, and publication remain in BackupService so a
// source change cannot weaken restore verification.
class IBackupSource {
public:
    virtual ~IBackupSource() = default;
    virtual bool downloadGame(const BackupGame& game, const std::filesystem::path& destination,
                              const BackupCallbacks& callbacks, std::string& error) = 0;
};

// Implemented by the configured Telegram adapter. Uploaded object IDs are
// recorded in the manifest, so restore does not require Telegram history
// access. The final manifest is uploaded only after every data chunk succeeds.
class IBackupTransport {
public:
    virtual ~IBackupTransport() = default;
    virtual std::optional<std::string> uploadObject(const std::string& name,
                                                     const std::filesystem::path& localPath,
                                                     long long sizeBytes, const BackupCallbacks& callbacks,
                                                     std::string& error) = 0;
    virtual bool downloadObject(const std::string& objectId, const std::filesystem::path& destination,
                                const BackupCallbacks& callbacks, std::string& error) = 0;
};

class BackupService {
public:
    // Safely below Telegram Bot API's per-file transfer limits for both
    // uploads and restores.
    static constexpr long long kChunkBytes = 16LL * 1024 * 1024;

    BackupService(IBackupSource& source, IBackupTransport& transport, core::IFileHasher& hasher,
                  std::filesystem::path workingDirectory);

    BackupResult createBackup(const BackupGame& game, const BackupCallbacks& callbacks = {});
    BackupBatchResult createBackups(const std::vector<BackupGame>& games, const BackupCallbacks& callbacks = {});
    // The server/Telegram transport returns opaque object IDs. GTH keeps a
    // local catalog so previously uploaded manifests remain selectable for
    // restore without relying on Telegram channel-history APIs.
    std::vector<BackupManifestRef> listBackups() const;
    BackupResult restoreBackup(const std::string& manifestObjectId, const std::filesystem::path& destinationRoot,
                               const BackupCallbacks& callbacks = {});

private:
    IBackupSource& source_;
    IBackupTransport& transport_;
    core::IFileHasher& hasher_;
    std::filesystem::path workingDirectory_;
};

}  // namespace gth::application
