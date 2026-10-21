#include "gth/application/indexing/GameIndexerService.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "gth/application/indexing/ContentIds.h"
#include "gth/application/indexing/CurrentVersion.h"
#include "gth/core/Clock.h"
#include "gth/core/index/VersionComparer.h"

namespace gth::application {

namespace {

// The drive Windows itself is installed on. Scanning it would walk millions
// of OS/Program Files entries and misidentify essentially any nested folder
// as a game - this must never happen regardless of what's sitting in the
// Storages table (a stale row from before storages self-healed, a future
// bug, manual DB editing, anything). Belt-and-suspenders on top of
// LocalStorageDetector excluding it and StorageService pruning a stale
// row on sync: this is the one place actual disk scanning begins, so it's
// the backstop that can't be bypassed by any code path.
bool isSystemDrive(const std::string& rootPath) {
    wchar_t windowsDir[MAX_PATH];
    if (::GetWindowsDirectoryW(windowsDir, MAX_PATH) == 0 || wcslen(windowsDir) < 2 || windowsDir[1] != L':') {
        return false;
    }

    std::string normalized = rootPath;
    while (!normalized.empty() && (normalized.back() == '\\' || normalized.back() == '/')) normalized.pop_back();
    if (normalized.size() != 2 || normalized[1] != ':') return false;

    return std::toupper(static_cast<unsigned char>(normalized[0])) ==
           std::toupper(static_cast<unsigned char>(static_cast<char>(windowsDir[0])));
}

// "PPSA02343", "CUSA07410", "BLUS30001": four letters and five digits - a title ID, which is what a scan names a game
// when it couldn't find its real name this time.
bool looksLikeBareTitleId(const std::string& text) {
    if (text.size() != 9) return false;
    for (size_t i = 0; i < 4; ++i) {
        if (!std::isalpha(static_cast<unsigned char>(text[i]))) return false;
    }
    for (size_t i = 4; i < 9; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
    }
    return true;
}

std::string trimmed(const std::string& text) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    auto first = std::find_if_not(text.begin(), text.end(), isSpace);
    auto last = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
    return first < last ? std::string(first, last) : std::string();
}

// "01.300.000", "1.36" or "v1.2" -> the digits-and-dots form (a leading v is
// dropped); nullopt for anything else. At least one dot: a bare number is far
// more likely a typo or a build number than a version.
std::optional<std::string> normalizeVersionText(const std::string& raw) {
    std::string text = trimmed(raw);
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) text.erase(0, 1);
    if (text.empty() || text.size() > 20) return std::nullopt;

    int groups = 0;
    size_t digitsInGroup = 0;
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            if (++digitsInGroup > 4) return std::nullopt;
        } else if (c == '.' && digitsInGroup > 0) {
            ++groups;
            digitsInGroup = 0;
        } else {
            return std::nullopt;
        }
    }
    if (digitsInGroup == 0 || groups < 1 || groups > 3) return std::nullopt;
    return text;
}

// The title ID a list token stands for: a bare one ("PPSA02343"), one with
// its "_00" suffix, or a whole Content ID. Empty when it's none of those.
std::string titleIdFromToken(const std::string& token) {
    if (auto fromContentId = titleIdFromContentId(token); !fromContentId.empty()) return fromContentId;
    if (token.size() != 9 && !(token.size() == 12 && token[9] == '_')) return {};
    std::string id = token.substr(0, 9);
    for (size_t i = 0; i < id.size(); ++i) {
        const auto c = static_cast<unsigned char>(id[i]);
        if (i < 4 ? !std::isalpha(c) : !std::isdigit(c)) return {};
        id[i] = static_cast<char>(std::toupper(c));
    }
    return id;
}

struct VersionListEntry {
    std::string titleId;
    std::string version;
};

std::optional<VersionListEntry> parseVersionListLine(std::string line) {
    if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);

    std::vector<std::string> tokens;
    std::string token;
    for (char c : line) {
        if (c == ' ' || c == '\t' || c == ',' || c == ';' || c == '=' || c == '|' || c == '"' || c == '\r') {
            if (!token.empty()) tokens.push_back(std::move(token));
            token.clear();
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(std::move(token));

    VersionListEntry entry;
    size_t titleIdAt = tokens.size();
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (auto id = titleIdFromToken(tokens[i]); !id.empty()) {
            entry.titleId = std::move(id);
            titleIdAt = i;
            break;
        }
    }
    if (entry.titleId.empty()) return std::nullopt;

    // The version is the last thing on the line that reads as one, so a
    // game name between the two ("PPSA02343, It Takes Two, 01.300.000") is fine.
    for (size_t i = tokens.size(); i-- > titleIdAt + 1;) {
        if (auto version = normalizeVersionText(tokens[i])) {
            entry.version = std::move(*version);
            return entry;
        }
    }
    return std::nullopt;
}

}  // namespace

using core::Game;
using core::GameFile;
using core::GameVariant;
using core::GameVariantStatus;
using core::GameVersion;
using core::ScannedFile;
using core::ScannedVariant;
using core::ScannedVersion;
using core::ScanJobStatus;

GameIndexerService::GameIndexerService(core::IStorageRepository& storageRepository, core::IStorageScanner& scanner,
                                        core::IGameRepository& gameRepository,
                                        core::IGameVariantRepository& variantRepository,
                                        core::IGameVersionRepository& versionRepository,
                                        core::IGameFileRepository& fileRepository,
                                        core::IScanJobRepository& scanJobRepository)
    : storageRepository_(storageRepository),
      scanner_(scanner),
      gameRepository_(gameRepository),
      variantRepository_(variantRepository),
      versionRepository_(versionRepository),
      fileRepository_(fileRepository),
      scanJobRepository_(scanJobRepository) {}

bool GameIndexerService::reconcileFiles(const GameVersion& version, const std::vector<ScannedFile>& scannedFiles) {
    auto existingFiles = fileRepository_.getByVersionId(version.gameVersionId);
    std::unordered_map<std::string, GameFile> existingByPath;
    for (auto& file : existingFiles) existingByPath.emplace(file.relativePath, file);

    std::unordered_set<std::string> seenPaths;
    bool anyChange = false;

    for (const auto& scanned : scannedFiles) {
        seenPaths.insert(scanned.relativePath);

        auto it = existingByPath.find(scanned.relativePath);
        if (it != existingByPath.end()) {
            if (it->second.sizeBytes != scanned.sizeBytes || it->second.lastModified != scanned.lastModified) {
                GameFile updated = it->second;
                updated.sizeBytes = scanned.sizeBytes;
                updated.lastModified = scanned.lastModified;
                updated.updatedAt = core::Clock::utcNowIso8601();
                fileRepository_.update(updated);
                anyChange = true;
            }
        } else {
            GameFile newFile;
            newFile.gameVersionId = version.gameVersionId;
            newFile.relativePath = scanned.relativePath;
            newFile.sizeBytes = scanned.sizeBytes;
            newFile.lastModified = scanned.lastModified;
            fileRepository_.add(newFile);
            anyChange = true;
        }
    }

    for (const auto& [path, file] : existingByPath) {
        if (!seenPaths.contains(path)) {
            fileRepository_.remove(file.gameFileId);
            anyChange = true;
        }
    }

    return anyChange;
}

void GameIndexerService::markAllVersionsMissing(const std::string& gameVariantId, ScanResult& result) {
    for (auto version : versionRepository_.getByVariantId(gameVariantId)) {
        if (version.status == GameVariantStatus::Missing) continue;
        version.status = GameVariantStatus::Missing;
        version.isCurrent = false;
        version.updatedAt = core::Clock::utcNowIso8601();
        versionRepository_.update(version);
        result.versionsMissing++;
    }
}

void GameIndexerService::reconcileVersions(const GameVariant& variant,
                                            const std::vector<ScannedVersion>& scannedVersions, ScanResult& result) {
    const auto existingVersions = versionRepository_.getByVariantId(variant.gameVariantId);
    std::unordered_set<std::string> seenPaths;
    std::vector<GameVersion> activeVersions;

    for (const auto& scannedVersion : scannedVersions) {
        seenPaths.insert(scannedVersion.relativePath);

        long long totalSize = 0;
        for (const auto& file : scannedVersion.files) totalSize += file.sizeBytes;

        auto existing = versionRepository_.getByVariantAndPath(variant.gameVariantId, scannedVersion.relativePath);
        GameVersion version;
        if (existing) {
            version = *existing;
            // The version string is re-detected on every scan and can improve
            // (a real version replacing a placeholder, a hand-set one in the
            // sidecar) - it has to be written back or the list would keep
            // showing whatever an older build stored the first time.
            const bool metaChanged = version.status != GameVariantStatus::Active || version.sizeBytes != totalSize ||
                                      version.version != scannedVersion.version;
            version.version = scannedVersion.version;
            version.sizeBytes = totalSize;
            version.status = GameVariantStatus::Active;
            version.lastScan = core::Clock::utcNowIso8601();

            const bool filesChanged = reconcileFiles(version, scannedVersion.files);
            if (metaChanged || filesChanged) version.updatedAt = core::Clock::utcNowIso8601();
            versionRepository_.update(version);

            if (metaChanged || filesChanged) {
                result.versionsUpdated++;
            } else {
                result.versionsUnchanged++;
            }
        } else {
            version.gameVariantId = variant.gameVariantId;
            version.version = scannedVersion.version;
            version.relativePath = scannedVersion.relativePath;
            version.sizeBytes = totalSize;
            version.status = GameVariantStatus::Active;
            version.lastScan = core::Clock::utcNowIso8601();
            versionRepository_.add(version);

            for (const auto& scannedFile : scannedVersion.files) {
                GameFile newFile;
                newFile.gameVersionId = version.gameVersionId;
                newFile.relativePath = scannedFile.relativePath;
                newFile.sizeBytes = scannedFile.sizeBytes;
                newFile.lastModified = scannedFile.lastModified;
                fileRepository_.add(newFile);
            }

            result.versionsAdded++;
        }

        activeVersions.push_back(version);
    }

    for (const auto& existing : existingVersions) {
        if (seenPaths.contains(existing.relativePath)) continue;
        if (existing.status == GameVariantStatus::Missing) continue;

        GameVersion missing = existing;
        missing.status = GameVariantStatus::Missing;
        missing.isCurrent = false;
        missing.updatedAt = core::Clock::utcNowIso8601();
        versionRepository_.update(missing);
        result.versionsMissing++;
    }

    // Whichever remaining Active version has the highest version string
    // becomes current. This is a heuristic, not a promise from the Update
    // Manager (a later milestone) about which copy is "the" official one -
    // just the indexer's best guess from what's on disk right now.
    if (!activeVersions.empty()) {
        const auto currentIt = std::max_element(
            activeVersions.begin(), activeVersions.end(),
            [](const GameVersion& a, const GameVersion& b) { return core::compareVersions(a.version, b.version) < 0; });

        for (auto& version : activeVersions) {
            const bool shouldBeCurrent = (&version == &(*currentIt));
            if (version.isCurrent != shouldBeCurrent) {
                version.isCurrent = shouldBeCurrent;
                version.updatedAt = core::Clock::utcNowIso8601();
                versionRepository_.update(version);
            }
        }
    }
}

ScanResult GameIndexerService::scanStorage(const std::string& storageId) {
    ScanResult result;

    auto storage = storageRepository_.getById(storageId);
    if (!storage) {
        result.success = false;
        result.errorMessage = "Storage not found: " + storageId;
        return result;
    }

    if (isSystemDrive(storage->rootPath)) {
        result.success = false;
        result.errorMessage = "Refusing to scan the Windows system drive (" + storage->rootPath + ")";
        return result;
    }

    core::ScanJob job;
    job.storageId = storageId;
    scanJobRepository_.add(job);
    result.scanJobId = job.scanJobId;

    try {
        const auto scanned = scanner_.scan(storage->rootPath);
        const auto diagnostics = scanner_.lastScanDiagnostics();
        result.titleLookupsAttempted = diagnostics.titleLookupsAttempted;
        result.titleLookupsResolved = diagnostics.titleLookupsResolved;
        result.titleLookupsNotFound = diagnostics.titleLookupsNotFound;
        result.titleLookupsFailed = diagnostics.titleLookupsFailed;
        result.lastTitleLookupFailure = diagnostics.lastTitleLookupFailure;
        result.releasesNamedFromFiles = diagnostics.releasesNamedFromFiles;
        result.releasesNotEligible = diagnostics.releasesNotEligible;
        const auto existingVariants = variantRepository_.getByStorageId(storageId);
        std::unordered_set<std::string> seenVariantPaths;

        // A variant's identity is (StorageId, RelativePath), but the scanner's
        // path scheme has changed between builds (a shared "." folder, then
        // the display title, now the title ID), so an older database can hold
        // a row for a release under a path today's scan no longer produces.
        // Left alone that row gets marked Missing and a brand new variant is
        // created beside it - the user then sees the old (often placeholder)
        // name still listed next to the good one. Version folder paths
        // (folder + content ID) never changed, so index them to recognise
        // and adopt such a row instead.
        std::unordered_map<std::string, GameVariant> variantByVersionPath;
        for (const auto& existing : existingVariants) {
            for (const auto& version : versionRepository_.getByVariantId(existing.gameVariantId)) {
                auto it = variantByVersionPath.find(version.relativePath);
                if (it == variantByVersionPath.end() ||
                    (it->second.status != GameVariantStatus::Active && existing.status == GameVariantStatus::Active)) {
                    variantByVersionPath[version.relativePath] = existing;
                }
            }
        }
        std::unordered_set<std::string> handledVariantIds;

        for (const auto& scannedVariant : scanned) {
            seenVariantPaths.insert(scannedVariant.relativePath);

            // One release the database refuses must not abandon every release
            // after it (a scan isn't transactional, so aborting halfway leaves
            // the library half-updated and every name after that point
            // unchanged). Count it, remember the first reason, carry on.
            try {
                auto existingVariant = variantRepository_.getByStorageAndPath(storageId, scannedVariant.relativePath);
                if (!existingVariant) {
                    for (const auto& scannedVersion : scannedVariant.versions) {
                        auto it = variantByVersionPath.find(scannedVersion.relativePath);
                        if (it == variantByVersionPath.end() || handledVariantIds.contains(it->second.gameVariantId)) {
                            continue;
                        }
                        existingVariant = it->second;
                        existingVariant->relativePath = scannedVariant.relativePath;
                        handledVariantIds.insert(existingVariant->gameVariantId);
                        break;
                    }
                }

                GameVariant variant;
                if (existingVariant) {
                    variant = *existingVariant;

                    // The variant's own title lives on its associated Game
                    // row, not on the variant itself - keep it in sync with
                    // this scan's (possibly improved) title, or a name that
                    // only becomes known later (a param.json becoming
                    // reachable, an online lookup succeeding) never reaches
                    // the UI. Game titles are unique, though, and the
                    // Japan/Europe/US releases of one game all resolve to the
                    // same real name - renaming each of their games to it
                    // would collide - so join the game that already has that
                    // name instead of renaming. Only rename in place when
                    // nothing else shares this game, otherwise a sibling
                    // release would be renamed along with it.
                    auto currentGame = gameRepository_.getById(variant.gameId);

                    // A scan that couldn't find the name this time (offline, the share didn't answer, the tracker
                    // site was down) comes back with just the title ID. That must never overwrite a real name the
                    // game already has - a name only ever improves.
                    std::string scannedTitle = scannedVariant.title;
                    if (currentGame && !currentGame->title.empty() && looksLikeBareTitleId(scannedTitle) &&
                        !looksLikeBareTitleId(currentGame->title)) {
                        scannedTitle = currentGame->title;
                    }

                    if (!currentGame || currentGame->title != scannedTitle) {
                        if (auto sameTitle = gameRepository_.getByTitle(scannedTitle)) {
                            variant.gameId = sameTitle->gameId;
                        } else if (currentGame && variantRepository_.getByGameId(currentGame->gameId).size() <= 1) {
                            currentGame->title = scannedTitle;
                            currentGame->updatedAt = core::Clock::utcNowIso8601();
                            gameRepository_.update(*currentGame);
                        } else {
                            Game newGame;
                            newGame.title = scannedTitle;
                            gameRepository_.add(newGame);
                            variant.gameId = newGame.gameId;
                            result.gamesAdded++;
                        }
                    }

                    const bool metaChanged = variant.platform != scannedVariant.platform ||
                                              variant.region != scannedVariant.region ||
                                              variant.gameId != existingVariant->gameId ||
                                              variant.status != GameVariantStatus::Active;
                    variant.platform = scannedVariant.platform;
                    variant.region = scannedVariant.region;
                    variant.status = GameVariantStatus::Active;
                    variant.lastScan = core::Clock::utcNowIso8601();
                    if (metaChanged) variant.updatedAt = core::Clock::utcNowIso8601();
                    variantRepository_.update(variant);
                } else {
                    auto game = gameRepository_.getByTitle(scannedVariant.title);
                    if (!game) {
                        Game newGame;
                        newGame.title = scannedVariant.title;
                        gameRepository_.add(newGame);
                        game = newGame;
                        result.gamesAdded++;
                    }

                    variant.gameId = game->gameId;
                    variant.platform = scannedVariant.platform;
                    variant.region = scannedVariant.region;
                    variant.storageId = storageId;
                    variant.relativePath = scannedVariant.relativePath;
                    variant.status = GameVariantStatus::Active;
                    variant.lastScan = core::Clock::utcNowIso8601();
                    variantRepository_.add(variant);
                    result.variantsAdded++;
                }

                handledVariantIds.insert(variant.gameVariantId);
                reconcileVersions(variant, scannedVariant.versions, result);
            } catch (const std::exception& ex) {
                ++result.variantsFailed;
                if (result.firstVariantError.empty()) {
                    result.firstVariantError = scannedVariant.title + ": " + ex.what();
                }
            }
        }

        for (const auto& existing : existingVariants) {
            if (seenVariantPaths.contains(existing.relativePath)) continue;
            if (handledVariantIds.contains(existing.gameVariantId)) continue;  // adopted under a new path above

            if (existing.status != GameVariantStatus::Missing) {
                GameVariant missing = existing;
                missing.status = GameVariantStatus::Missing;
                missing.updatedAt = core::Clock::utcNowIso8601();
                variantRepository_.update(missing);
            }
            markAllVersionsMissing(existing.gameVariantId, result);
        }

        job.status = ScanJobStatus::Completed;
        job.completedAt = core::Clock::utcNowIso8601();
        job.gamesAdded = result.gamesAdded;
        job.variantsAdded = result.variantsAdded;
        job.versionsAdded = result.versionsAdded;
        job.versionsUpdated = result.versionsUpdated;
        job.versionsMissing = result.versionsMissing;
        job.versionsUnchanged = result.versionsUnchanged;
        scanJobRepository_.update(job);

        result.success = true;
    } catch (const std::exception& ex) {
        job.status = ScanJobStatus::Failed;
        job.completedAt = core::Clock::utcNowIso8601();
        job.errorMessage = ex.what();
        scanJobRepository_.update(job);

        result.success = false;
        result.errorMessage = ex.what();
    }

    return result;
}

SetVersionResult GameIndexerService::setVersion(const std::string& gameVariantId, const std::string& rawVersion) {
    SetVersionResult result;

    std::string version;
    if (!trimmed(rawVersion).empty()) {
        const auto normalized = normalizeVersionText(rawVersion);
        if (!normalized) {
            result.errorMessage = "A version is digits separated by dots, like 01.300 or 01.300.000.";
            return result;
        }
        version = *normalized;
    }

    const auto variant = variantRepository_.getById(gameVariantId);
    if (!variant) {
        result.errorMessage = "That game isn't in the library any more - refresh the list.";
        return result;
    }
    const auto storage = storageRepository_.getById(variant->storageId);
    if (!storage) {
        result.errorMessage = "The storage this game was found on has been removed.";
        return result;
    }
    auto target = currentActiveVersion(versionRepository_, gameVariantId);
    if (!target) {
        result.errorMessage = "This game's files weren't found by the last scan, so there's nothing to attach a version to.";
        return result;
    }

    std::string error;
    if (!scanner_.pinReleaseVersion(storage->rootPath, target->relativePath, version, error)) {
        result.errorMessage = error;
        return result;
    }

    target->version = version;
    target->updatedAt = core::Clock::utcNowIso8601();
    versionRepository_.update(*target);
    result.success = true;
    return result;
}

ImportVersionsResult GameIndexerService::importVersions(const std::string& text) {
    ImportVersionsResult result;

    // Last line wins when a title ID appears twice; order kept for reporting.
    std::vector<std::string> order;
    std::unordered_map<std::string, std::string> wanted;
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        const std::string line = text.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        if (trimmed(line.substr(0, line.find('#'))).empty()) continue;  // blank or comment-only
        const auto entry = parseVersionListLine(line);
        if (!entry) {
            ++result.linesIgnored;
            continue;
        }
        if (!wanted.contains(entry->titleId)) order.push_back(entry->titleId);
        wanted[entry->titleId] = entry->version;
    }

    std::unordered_set<std::string> matched;
    std::unordered_map<std::string, std::optional<core::Storage>> storages;
    for (const auto& variant : variantRepository_.getAll()) {
        if (variant.status != core::GameVariantStatus::Active) continue;
        auto target = currentActiveVersion(versionRepository_, variant.gameVariantId);
        if (!target) continue;

        const std::string titleId = titleIdFromVersionPath(target->relativePath);
        const auto wantedIt = wanted.find(titleId);
        if (wantedIt == wanted.end()) continue;
        matched.insert(titleId);

        auto storageIt = storages.find(variant.storageId);
        if (storageIt == storages.end()) {
            storageIt = storages.emplace(variant.storageId, storageRepository_.getById(variant.storageId)).first;
        }
        std::string error = "The storage this game was found on has been removed.";
        if (storageIt->second &&
            scanner_.pinReleaseVersion(storageIt->second->rootPath, target->relativePath, wantedIt->second, error)) {
            target->version = wantedIt->second;
            target->updatedAt = core::Clock::utcNowIso8601();
            versionRepository_.update(*target);
            ++result.applied;
        } else {
            ++result.failed;
            if (result.firstError.empty()) result.firstError = titleId + ": " + error;
        }
    }

    for (const auto& titleId : order) {
        if (!matched.contains(titleId)) result.unmatchedTitleIds.push_back(titleId);
    }
    return result;
}

}  // namespace gth::application
