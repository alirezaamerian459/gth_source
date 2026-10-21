#pragma once

#include <string>

#include "gth/application/storage/IGameSearchService.h"
#include "gth/core/entities/Storage.h"

namespace gth {
namespace core {
class IGameRepository;
class IGameVariantRepository;
class IGameVersionRepository;
class IStorageRepository;
struct Storage;
}  // namespace core
}  // namespace gth

namespace gth::application {

class GameSearchService : public IGameSearchService {
public:
    GameSearchService(gth::core::IGameRepository& gameRepo, gth::core::IGameVariantRepository& variantRepo,
                      gth::core::IGameVersionRepository& versionRepo, gth::core::IStorageRepository& storageRepo);

    std::optional<GameSearchResult> searchByTitle(const std::string& partialTitle) override;
    std::optional<GameSearchResult> searchByExactTitle(const std::string& title) override;
    std::vector<GameSearchResult> searchByPlatformAndRegion(const std::string& platform,
                                                             const std::string& region) override;
    std::string extractTitleFromUrl(const std::string& url) override;

private:
    // Helper to check if a variant's current version exists and build full path
    std::optional<std::pair<core::GameVersion, std::string>> getActiveVersionPath(
        const core::GameVariant& variant, const core::Storage* storage);

    // Helper to convert search results to full paths
    GameSearchResult buildSearchResult(const core::Game& game, const core::GameVariant& variant,
                                       const core::GameVersion& version, const std::string& fullPath);

    gth::core::IGameRepository& gameRepo_;
    gth::core::IGameVariantRepository& variantRepo_;
    gth::core::IGameVersionRepository& versionRepo_;
    gth::core::IStorageRepository& storageRepo_;
};

}  // namespace gth::application
