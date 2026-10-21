#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "gth/application/indexing/IPackageSync.h"
#include "gth/core/index/IGameFileRepository.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/index/IGameVersionRepository.h"
#include "gth/core/index/ILatestReleaseService.h"
#include "gth/core/index/IPieceDownloader.h"
#include "gth/core/index/IStorageScanner.h"
#include "gth/core/storage/IStorageRepository.h"
#include "gth/core/updates/IUpdateVerifier.h"

namespace gth::application {

class PackageSyncService : public IPackageSync {
public:
    PackageSyncService(core::IStorageRepository& storageRepository, core::IGameVariantRepository& variantRepository,
                       core::IGameVersionRepository& versionRepository, core::IGameFileRepository& fileRepository,
                       core::ILatestReleaseService& latestReleases, core::IPieceDownloader& downloader,
                       core::IUpdateVerifier& verifier, core::IStorageScanner& scanner);

    SyncPlan plan(const std::string& gameVariantId) override;
    SyncResult run(const std::string& gameVariantId, SyncMode mode, const SyncCallbacks& callbacks = {}) override;
    SyncBatchResult runAll(SyncMode mode, const SyncCallbacks& callbacks = {}) override;
    std::vector<std::pair<std::string, std::string>> metadataPieceFiles() override;

private:
    // `escalated`: this is a second pass over a game that looked like Sony's latest package by its file
    // sizes but proved not to be, so every piece is verified by checksum.
    SyncResult runOnce(const std::string& gameVariantId, SyncMode mode, const SyncCallbacks& callbacks, bool escalated);

    // PS4: Sony publishes one cumulative update file per title instead of a re-cut package. It is downloaded
    // into an "Updates" folder beside the game's own files, so the base game is never touched; the next scan
    // sees it as the game's newer version.
    struct Ps4Analysis;
    std::optional<Ps4Analysis> analyzePs4(const std::string& gameVariantId, SyncPlan& plan);
    SyncPlan planPs4(const std::string& gameVariantId);
    SyncResult runPs4(const std::string& gameVariantId, SyncMode mode, const SyncCallbacks& callbacks);
    bool isPs4(const std::string& gameVariantId);

    struct Analysis;
    // Everything plan() and run() both need to know about one game.
    std::optional<Analysis> analyze(const std::string& gameVariantId, SyncPlan& plan);

    core::IStorageRepository& storageRepository_;
    core::IGameVariantRepository& variantRepository_;
    core::IGameVersionRepository& versionRepository_;
    core::IGameFileRepository& fileRepository_;
    core::ILatestReleaseService& latestReleases_;
    core::IPieceDownloader& downloader_;
    core::IUpdateVerifier& verifier_;
    core::IStorageScanner& scanner_;
};

// Whether a package's metadata piece belongs to the game files beside it:
// part 0's FIH header carries digests of the image that also appear inside the
// metadata block, so a piece from a different version of the game doesn't
// contain them. Reads only the first bytes of each file.
bool MetadataPieceBelongsToPackage(const std::filesystem::path& firstPart, const std::filesystem::path& metadataPiece);

}  // namespace gth::application
