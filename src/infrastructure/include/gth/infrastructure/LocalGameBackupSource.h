#pragma once

#include "gth/application/backup/BackupService.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/storage/IStorageRepository.h"

namespace gth::infrastructure {

// Stages the complete GTH game-variant folder from its registered local
// storage. The backup service then hashes, chunks, and uploads that snapshot.
class LocalGameBackupSource final : public application::IBackupSource {
public:
    LocalGameBackupSource(core::IGameVariantRepository& variants, core::IStorageRepository& storages)
        : variants_(variants), storages_(storages) {}

    bool downloadGame(const application::BackupGame& game, const std::filesystem::path& destination,
                      const application::BackupCallbacks& callbacks, std::string& error) override;

private:
    core::IGameVariantRepository& variants_;
    core::IStorageRepository& storages_;
};

}  // namespace gth::infrastructure
