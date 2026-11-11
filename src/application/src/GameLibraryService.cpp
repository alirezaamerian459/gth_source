#include "gth/application/indexing/GameLibraryService.h"

#include <unordered_map>
#include <unordered_set>

#include "gth/application/indexing/ContentIds.h"

namespace gth::application {

using core::GameVariantStatus;

GameLibraryService::GameLibraryService(core::IGameRepository& gameRepository,
                                        core::IGameVariantRepository& variantRepository,
                                        core::IGameVersionRepository& versionRepository,
                                        core::IStorageRepository* storageRepository)
    : gameRepository_(gameRepository),
      variantRepository_(variantRepository),
      versionRepository_(versionRepository),
      storageRepository_(storageRepository) {}

std::vector<GameLibraryEntry> GameLibraryService::getAllEntries() {
    struct Row {
        GameLibraryEntry entry;
        std::vector<std::string> versionPaths;
    };
    std::vector<Row> rows;
    std::unordered_set<std::string> activeVersionPaths;

    // Keep indexed history when a removable drive is unplugged, but do not
    // present those rows as playable library entries until the storage is
    // reachable again. StorageService updates this persisted availability at
    // each local rescan, so the Games and Updates pages agree immediately
    // without deleting the user's index.
    std::unordered_map<std::string, bool> storageAvailability;
    if (storageRepository_ != nullptr) {
        for (const auto& storage : storageRepository_->getAll()) {
            storageAvailability.emplace(storage.storageId,
                                        storage.isAvailable && storage.status == core::StorageStatus::Online);
        }
    }

    for (const auto& game : gameRepository_.getAll()) {
        for (const auto& variant : variantRepository_.getByGameId(game.gameId)) {
            if (storageRepository_ != nullptr) {
                const auto storage = storageAvailability.find(variant.storageId);
                if (storage == storageAvailability.end() || !storage->second) continue;
            }
            Row row;
            GameLibraryEntry& entry = row.entry;
            entry.gameId = game.gameId;
            entry.title = game.title;
            entry.gameVariantId = variant.gameVariantId;
            entry.platform = variant.platform;
            entry.region = variant.region;
            entry.variantStatus = variant.status;

            for (const auto& version : versionRepository_.getByVariantId(variant.gameVariantId)) {
                row.versionPaths.push_back(version.relativePath);
                if (variant.status == GameVariantStatus::Active && version.status == GameVariantStatus::Active) {
                    activeVersionPaths.insert(version.relativePath);
                }
                if (version.isCurrent) {
                    entry.currentVersion = version.version;
                    entry.currentVersionSizeBytes = version.sizeBytes;
                    entry.titleId = titleIdFromVersionPath(version.relativePath);
                }
            }

            rows.push_back(std::move(row));
        }
    }

    // An older database can still hold a Missing row for a release that a
    // newer scan re-created as a fresh Active variant under a different path
    // (see GameIndexerService's adoption of such rows) - same files, so it's
    // a stale duplicate carrying an outdated title, not a game that's gone.
    // Rows for releases that really are gone stay listed as Missing.
    std::vector<GameLibraryEntry> entries;
    for (auto& row : rows) {
        if (row.entry.variantStatus == GameVariantStatus::Missing && !row.versionPaths.empty()) {
            bool supersededByActive = true;
            for (const auto& path : row.versionPaths) {
                if (!activeVersionPaths.contains(path)) {
                    supersededByActive = false;
                    break;
                }
            }
            if (supersededByActive) continue;
        }
        entries.push_back(std::move(row.entry));
    }

    return entries;
}

}  // namespace gth::application
