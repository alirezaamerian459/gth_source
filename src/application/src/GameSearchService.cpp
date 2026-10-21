#include "GameSearchService.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

#include "gth/core/entities/Game.h"
#include "gth/core/entities/GameVariant.h"
#include "gth/core/entities/GameVersion.h"
#include "gth/core/entities/Storage.h"
#include "gth/core/enums/GameVariantStatus.h"
#include "gth/core/index/IGameRepository.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/index/IGameVersionRepository.h"
#include "gth/core/storage/IStorageRepository.h"

namespace gth::application {

GameSearchService::GameSearchService(gth::core::IGameRepository& gameRepo,
                                     gth::core::IGameVariantRepository& variantRepo,
                                     gth::core::IGameVersionRepository& versionRepo,
                                     gth::core::IStorageRepository& storageRepo)
    : gameRepo_(gameRepo), variantRepo_(variantRepo), versionRepo_(versionRepo), storageRepo_(storageRepo) {}

static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::optional<GameSearchResult> GameSearchService::searchByTitle(const std::string& partialTitle) {
    if (partialTitle.empty()) return std::nullopt;

    const auto allGames = gameRepo_.getAll();
    const auto lowerSearchTerm = toLower(partialTitle);

    // Find the best match (closest match by length difference)
    std::optional<core::Game> bestMatch;
    size_t bestMatchDiff = std::string::npos;

    for (const auto& game : allGames) {
        const auto lowerTitle = toLower(game.title);
        if (lowerTitle.find(lowerSearchTerm) != std::string::npos) {
            // Found a match - prefer shorter titles (more specific matches)
            if (bestMatch.has_value()) {
                if (lowerTitle.length() < toLower(bestMatch->title).length()) {
                    bestMatch = game;
                }
            } else {
                bestMatch = game;
            }
        }
    }

    if (!bestMatch.has_value()) return std::nullopt;

    // Get all storages for path resolution
    const auto allStorages = storageRepo_.getAll();

    // Get active variants for this game
    const auto variants = variantRepo_.getByGameId(bestMatch->gameId);
    for (const auto& variant : variants) {
        if (variant.status != core::GameVariantStatus::Active) continue;

        // Find storage for this variant
        const core::Storage* storage = nullptr;
        for (const auto& s : allStorages) {
            if (s.storageId == variant.storageId) {
                storage = &s;
                break;
            }
        }

        if (storage) {
            auto versionPath = getActiveVersionPath(variant, storage);
            if (versionPath.has_value()) {
                return buildSearchResult(*bestMatch, variant, versionPath->first, versionPath->second);
            }
        }
    }

    return std::nullopt;
}

std::optional<GameSearchResult> GameSearchService::searchByExactTitle(const std::string& title) {
    if (title.empty()) return std::nullopt;

    const auto allGames = gameRepo_.getAll();
    const auto lowerTitle = toLower(title);
    const auto allStorages = storageRepo_.getAll();

    for (const auto& game : allGames) {
        if (toLower(game.title) == lowerTitle) {
            // Found exact match - get first active variant with version
            const auto variants = variantRepo_.getByGameId(game.gameId);
            for (const auto& variant : variants) {
                if (variant.status != core::GameVariantStatus::Active) continue;

                const core::Storage* storage = nullptr;
                for (const auto& s : allStorages) {
                    if (s.storageId == variant.storageId) {
                        storage = &s;
                        break;
                    }
                }

                if (storage) {
                    auto versionPath = getActiveVersionPath(variant, storage);
                    if (versionPath.has_value()) {
                        return buildSearchResult(game, variant, versionPath->first, versionPath->second);
                    }
                }
            }
        }
    }

    return std::nullopt;
}

std::vector<GameSearchResult> GameSearchService::searchByPlatformAndRegion(const std::string& platform,
                                                                            const std::string& region) {
    std::vector<GameSearchResult> results;
    const auto lowerPlatform = toLower(platform);
    const auto lowerRegion = toLower(region);
    const auto allGames = gameRepo_.getAll();
    const auto allStorages = storageRepo_.getAll();

    for (const auto& game : allGames) {
        const auto variants = variantRepo_.getByGameId(game.gameId);
        for (const auto& variant : variants) {
            if (toLower(variant.platform) != lowerPlatform || toLower(variant.region) != lowerRegion ||
                variant.status != core::GameVariantStatus::Active) {
                continue;
            }

            const core::Storage* storage = nullptr;
            for (const auto& s : allStorages) {
                if (s.storageId == variant.storageId) {
                    storage = &s;
                    break;
                }
            }

            if (storage) {
                auto versionPath = getActiveVersionPath(variant, storage);
                if (versionPath.has_value()) {
                    results.push_back(buildSearchResult(game, variant, versionPath->first, versionPath->second));
                }
            }
        }
    }

    return results;
}

std::string GameSearchService::extractTitleFromUrl(const std::string& url) {
    // PSN URLs typically have format like:
    // https://[region]-psn-cdn.psn.com/download/[title-id]/[content-id]/[filename]
    // or game title might be in URL path: /games/[game-title]/...
    // This is a basic extraction - may need to be enhanced based on actual URL formats

    std::string extracted;
    const auto lastSlash = url.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
        const auto beforeSlash = url.substr(0, lastSlash);
        const auto secondLastSlash = beforeSlash.find_last_of('/');
        if (secondLastSlash != std::string::npos) {
            extracted = url.substr(secondLastSlash + 1, lastSlash - secondLastSlash - 1);
        }
    }

    // Clean up extracted text: remove hyphens/underscores, replace with spaces
    if (!extracted.empty()) {
        std::replace(extracted.begin(), extracted.end(), '-', ' ');
        std::replace(extracted.begin(), extracted.end(), '_', ' ');
        // Remove numbers/hashes at the end
        while (!extracted.empty() && (std::isdigit(extracted.back()) || extracted.back() == '#')) {
            extracted.pop_back();
        }
    }

    return extracted;
}

std::optional<std::pair<core::GameVersion, std::string>> GameSearchService::getActiveVersionPath(
    const core::GameVariant& variant, const core::Storage* storage) {
    if (!storage) return std::nullopt;

    const auto versions = versionRepo_.getByVariantId(variant.gameVariantId);

    for (const auto& version : versions) {
        if (version.status != core::GameVariantStatus::Active) continue;
        if (!version.isCurrent) continue;

        // Build full path: storage root + variant path + version path
        std::filesystem::path fullPath = storage->rootPath;
        fullPath /= variant.relativePath;
        fullPath /= version.relativePath;
        return std::make_pair(version, fullPath.string());
    }

    return std::nullopt;
}

GameSearchResult GameSearchService::buildSearchResult(const core::Game& game, const core::GameVariant& variant,
                                                       const core::GameVersion& version,
                                                       const std::string& fullPath) {
    GameSearchResult result;
    result.game = game;
    result.variant = variant;
    result.version = version;
    result.fullPath = fullPath;
    return result;
}

}  // namespace gth::application
