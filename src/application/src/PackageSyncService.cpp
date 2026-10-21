#include "gth/application/indexing/PackageSyncService.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <thread>

#include "gth/application/indexing/ContentIds.h"
#include "gth/application/indexing/CurrentVersion.h"
#include "gth/application/indexing/PackagePieces.h"
#include "gth/core/enums/GameVariantStatus.h"
#include "gth/core/index/VersionComparer.h"

namespace gth::application {

namespace fs = std::filesystem;

namespace {

// Kept free on the volume on top of what a sync downloads, so a sync can't
// be the thing that fills a drive to the brim.
constexpr long long kSpaceMargin = 256LL * 1024 * 1024;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Only Sony's own servers are ever downloaded from, whatever a manifest says.
bool isSonyUrl(const std::string& url) {
    const auto scheme = url.find("://");
    if (scheme == std::string::npos) return false;
    const std::string protocol = lower(url.substr(0, scheme));
    if (protocol != "http" && protocol != "https") return false;
    const size_t hostStart = scheme + 3;
    const size_t hostEnd = url.find_first_of("/:?", hostStart);
    const std::string host = lower(url.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart));
    static const std::string kSuffix = ".playstation.net";
    return host.size() > kSuffix.size() && host.compare(host.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
}

std::string formatSize(long long bytes) {
    char buffer[32];
    if (bytes >= 1024LL * 1024 * 1024) {
        snprintf(buffer, sizeof(buffer), "%.2f GB", static_cast<double>(bytes) / 1024.0 / 1024.0 / 1024.0);
    } else {
        snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / 1024.0 / 1024.0);
    }
    return buffer;
}

struct LocalPiece {
    int index = -1;
    fs::path path;
    long long size = 0;
};

std::string fileNameLower(const fs::path& path) { return lower(path.filename().string()); }

}  // namespace

struct PackageSyncService::Analysis {
    core::GameVariant variant;
    core::Storage storage;
    core::GameVersion version;
    std::string titleId;
    core::LatestRelease latest;

    fs::path folder;
    std::vector<LocalPiece> numbered;        // the game pieces on disk, in part order
    std::optional<LocalPiece> metadata;      // the "_sc.pkg" piece, if it's there

    std::vector<core::ReleasePiece> pfsPieces;       // Sony's game pieces, in order
    std::optional<core::ReleasePiece> metadataPiece; // Sony's metadata piece
    bool pfsMatches = false;                          // the files on disk are exactly Sony's game pieces
};

PackageSyncService::PackageSyncService(core::IStorageRepository& storageRepository,
                                       core::IGameVariantRepository& variantRepository,
                                       core::IGameVersionRepository& versionRepository,
                                       core::IGameFileRepository& fileRepository,
                                       core::ILatestReleaseService& latestReleases, core::IPieceDownloader& downloader,
                                       core::IUpdateVerifier& verifier, core::IStorageScanner& scanner)
    : storageRepository_(storageRepository),
      variantRepository_(variantRepository),
      versionRepository_(versionRepository),
      fileRepository_(fileRepository),
      latestReleases_(latestReleases),
      downloader_(downloader),
      verifier_(verifier),
      scanner_(scanner) {}

bool MetadataPieceBelongsToPackage(const fs::path& firstPart, const fs::path& metadataPiece) {
    std::ifstream part(firstPart, std::ios::binary);
    std::ifstream piece(metadataPiece, std::ios::binary);
    if (!part || !piece) return false;

    // The FIH wrapper starts a PS5 package; its bytes 0x30..0x50 are a digest
    // of the encrypted image that the metadata block repeats verbatim.
    unsigned char header[0x50]{};
    part.read(reinterpret_cast<char*>(header), sizeof(header));
    if (part.gcount() != static_cast<std::streamsize>(sizeof(header))) return false;
    if (!(header[0] == 0x7f && header[1] == 'F' && header[2] == 'I' && header[3] == 'H')) return false;

    constexpr size_t kDigestAt = 0x30;
    constexpr size_t kDigestLength = 0x20;
    bool allZero = true;
    for (size_t i = 0; i < kDigestLength; ++i) allZero = allZero && header[kDigestAt + i] == 0;
    if (allZero) return false;

    std::string block(0x10000, '\0');
    piece.read(block.data(), static_cast<std::streamsize>(block.size()));
    block.resize(static_cast<size_t>(piece.gcount()));
    const std::string digest(reinterpret_cast<const char*>(header + kDigestAt), kDigestLength);
    return block.find(digest) != std::string::npos;
}

std::optional<PackageSyncService::Analysis> PackageSyncService::analyze(const std::string& gameVariantId, SyncPlan& plan) {
    const auto variant = variantRepository_.getById(gameVariantId);
    if (!variant) {
        plan.detail = "That game isn't in the library any more.";
        return std::nullopt;
    }
    plan.storageId = variant->storageId;
    if (variant->status != core::GameVariantStatus::Active) {
        plan.detail = "The game's files weren't found by the last scan.";
        return std::nullopt;
    }
    if (variant->platform != "PS5") {
        plan.detail = "Only PS5 packages can be brought up to date from Sony's servers.";
        return std::nullopt;
    }
    const auto storage = storageRepository_.getById(variant->storageId);
    if (!storage) {
        plan.detail = "The storage this game was found on has been removed.";
        return std::nullopt;
    }
    auto version = currentActiveVersion(versionRepository_, gameVariantId);
    if (!version) {
        plan.detail = "The game's files weren't found by the last scan.";
        return std::nullopt;
    }
    const std::string titleId = titleIdFromVersionPath(version->relativePath);
    plan.titleId = titleId;
    if (titleId.empty()) {
        plan.detail = "The game's files have no Content ID to look up.";
        return std::nullopt;
    }

    const auto latest = latestReleases_.latestRelease("PS5", titleId);
    if (latest.status != core::LatestReleaseStatus::Found) {
        plan.detail = latest.status == core::LatestReleaseStatus::NoUpdatePublished ? "Sony lists no package for this game."
                                                                                     : latest.detail;
        return std::nullopt;
    }
    plan.latestVersion = latest.release.version;
    if (latest.release.pieces.empty()) {
        plan.detail = "Sony's answer didn't list the package's files.";
        return std::nullopt;
    }
    for (const auto& piece : latest.release.pieces) {
        if (!isSonyUrl(piece.url) || !isSafePieceFileName(leafOfPath(piece.url)) || piece.sizeBytes <= 0) {
            plan.detail = "Sony's answer contained a file address that isn't safe to use, so nothing was downloaded.";
            return std::nullopt;
        }
    }

    // Sony's files must be pieces of THIS edition of the game (same Content ID as the files on disk). A
    // title ID that maps to a differently-named package - a renamed copy, another edition - would otherwise
    // have its pieces dropped into this game's folder beside files they don't belong with.
    const std::string localContentId = lower(leafOfPath(version->relativePath));
    for (const auto& piece : latest.release.pieces) {
        if (lower(leafOfPath(piece.url)).rfind(localContentId + "_", 0) != 0) {
            plan.detail = "Sony's package for this title is a different edition (its files are named " +
                          leafOfPath(piece.url) + ", yours " + leafOfPath(version->relativePath) +
                          "), so nothing was downloaded.";
            return std::nullopt;
        }
    }

    Analysis a;
    a.variant = *variant;
    a.storage = *storage;
    a.version = *version;
    a.titleId = titleId;
    a.latest = latest.release;

    for (const auto& file : fileRepository_.getByVersionId(version->gameVersionId)) {
        LocalPiece local;
        local.path = fs::path(storage->rootPath) / fs::path(file.relativePath);
        local.size = file.sizeBytes;
        if (isMetadataPieceFileName(file.relativePath)) {
            a.metadata = local;
            continue;
        }
        local.index = partIndexOfFileName(file.relativePath);
        if (local.index < 0) {
            plan.detail = "The game's file names don't look like the pieces of a Sony package (" + leafOfPath(file.relativePath) + ").";
            return std::nullopt;
        }
        a.numbered.push_back(std::move(local));
    }
    std::sort(a.numbered.begin(), a.numbered.end(), [](const LocalPiece& x, const LocalPiece& y) { return x.index < y.index; });
    if (a.numbered.empty()) {
        plan.detail = "The game's files weren't found by the last scan.";
        return std::nullopt;
    }
    for (size_t i = 0; i < a.numbered.size(); ++i) {
        if (a.numbered[i].index != static_cast<int>(i)) {
            plan.detail = "The game is missing one of its pieces, so it can't be matched against Sony's package.";
            return std::nullopt;
        }
    }
    a.folder = a.numbered.front().path.parent_path();

    for (const auto& piece : a.latest.pieces) {
        if (isMetadataPieceFileName(piece.url)) {
            a.metadataPiece = piece;
        } else {
            a.pfsPieces.push_back(piece);
        }
    }

    a.pfsMatches = a.numbered.size() == a.pfsPieces.size();
    for (size_t i = 0; a.pfsMatches && i < a.pfsPieces.size(); ++i) {
        a.pfsMatches = a.numbered[i].size == a.pfsPieces[i].sizeBytes &&
                       fileNameLower(a.numbered[i].path) == lower(leafOfPath(a.pfsPieces[i].url));
    }
    return a;
}

namespace {

// What kind of work an analysed game needs, and how much is certain to be downloaded.
void classify(long long& bytes, SyncKind& kind, std::string& detail, bool pfsMatches,
              const std::optional<core::ReleasePiece>& sonyMetadata, const std::optional<LocalPiece>& localMetadata,
              const std::vector<core::ReleasePiece>& pfsPieces, const std::vector<LocalPiece>& numbered,
              const std::string& latestVersion) {
    bytes = 0;
    if (pfsMatches) {
        if (sonyMetadata && !localMetadata) {
            kind = SyncKind::CompleteMetadata;
            bytes = sonyMetadata->sizeBytes;
            detail = "The game files are Sony's latest package (" + latestVersion + "); its metadata piece (" +
                     formatSize(bytes) + ") is missing, and adding it lets the title and version be read from the files.";
            return;
        }
        // A metadata piece is specific to one build of the game: one of a different size than Sony's latest
        // means the files beside it are a different build too, whatever their sizes look like.
        if (!sonyMetadata || (localMetadata && localMetadata->size == sonyMetadata->sizeBytes)) {
            kind = SyncKind::NothingToDo;
            detail = "Already Sony's latest package (" + latestVersion + ").";
            return;
        }
    }

    kind = SyncKind::Update;
    int missingOrDifferent = 0;
    std::map<std::string, long long> localSizes;
    for (const auto& piece : numbered) localSizes[fileNameLower(piece.path)] = piece.size;
    if (localMetadata) localSizes[fileNameLower(localMetadata->path)] = localMetadata->size;
    std::vector<core::ReleasePiece> all = pfsPieces;
    if (sonyMetadata) all.push_back(*sonyMetadata);
    for (const auto& piece : all) {
        const auto found = localSizes.find(lower(leafOfPath(piece.url)));
        if (found == localSizes.end() || found->second != piece.sizeBytes) {
            ++missingOrDifferent;
            bytes += piece.sizeBytes;
        }
    }
    detail = "The files differ from Sony's latest package (" + latestVersion + "): " + std::to_string(missingOrDifferent) +
             " of " + std::to_string(all.size()) + " pieces certainly need downloading (" + formatSize(bytes) +
             "), and pieces of the right size are checked before being kept.";
}

}  // namespace

SyncPlan PackageSyncService::plan(const std::string& gameVariantId) {
    if (isPs4(gameVariantId)) return planPs4(gameVariantId);
    SyncPlan result;
    const auto analysis = analyze(gameVariantId, result);
    if (!analysis) {
        result.kind = SyncKind::Unavailable;
        return result;
    }
    result.latestVersion = analysis->latest.version;
    classify(result.bytesToDownload, result.kind, result.detail, analysis->pfsMatches,
             analysis->metadataPiece, analysis->metadata, analysis->pfsPieces, analysis->numbered, analysis->latest.version);
    return result;
}

SyncResult PackageSyncService::run(const std::string& gameVariantId, SyncMode mode, const SyncCallbacks& callbacks) {
    return runOnce(gameVariantId, mode, callbacks, /*escalated=*/false);
}

SyncResult PackageSyncService::runOnce(const std::string& gameVariantId, SyncMode mode, const SyncCallbacks& callbacks,
                                       bool escalated) {
    if (isPs4(gameVariantId)) return runPs4(gameVariantId, mode, callbacks);
    SyncResult result;

    SyncPlan basePlan;
    const auto analysisOpt = analyze(gameVariantId, basePlan);
    result.titleId = basePlan.titleId;
    result.storageId = basePlan.storageId;
    if (!analysisOpt) {
        result.kind = SyncKind::Unavailable;
        result.message = basePlan.detail;
        return result;
    }
    const Analysis& a = *analysisOpt;

    long long certainBytes = 0;
    std::string detail;
    classify(certainBytes, result.kind, detail, a.pfsMatches, a.metadataPiece, a.metadata, a.pfsPieces,
             a.numbered, a.latest.version);
    // The files looked like Sony's package by size, but the metadata piece said otherwise: every piece
    // has to be checked by its checksum instead.
    if (escalated && result.kind == SyncKind::CompleteMetadata) result.kind = SyncKind::Update;

    if (result.kind == SyncKind::NothingToDo) {
        result.success = true;
        result.message = detail;
        return result;
    }
    if (result.kind == SyncKind::Update && mode == SyncMode::MetadataOnly) {
        result.skippedNeedsUpdate = true;
        result.message = "Out of date (" + a.latest.version + " is Sony's latest): updating downloads at least " +
                         formatSize(certainBytes) + " and isn't done automatically.";
        return result;
    }

    const auto cancelled = [&] { return callbacks.shouldCancel && callbacks.shouldCancel(); };
    const auto report = [&](const std::string& step, long long done, long long total) {
        if (callbacks.onProgress) callbacks.onProgress(SyncProgress{step, done, total});
    };
    const auto fail = [&](const std::string& message, bool transient = false) {
        result.success = false;
        result.transient = transient;
        result.message = message;
        return result;
    };
    const auto wasCancelled = [&] {
        result.cancelled = true;
        result.message = "Cancelled - what was downloaded so far is kept and will be continued next time.";
        return result;
    };

    std::error_code ec;

    // ---- decide which of Sony's files have to be fetched ------------------------
    std::vector<core::ReleasePiece> toFetch;
    if (result.kind == SyncKind::CompleteMetadata) {
        toFetch.push_back(*a.metadataPiece);
    } else {
        std::vector<core::ReleasePiece> all = a.pfsPieces;
        if (a.metadataPiece) all.push_back(*a.metadataPiece);
        int position = 0;
        for (const auto& piece : all) {
            ++position;
            if (cancelled()) return wasCancelled();
            const fs::path local = a.folder / leafOfPath(piece.url);
            const bool sameSize = fs::exists(local, ec) && static_cast<long long>(fs::file_size(local, ec)) == piece.sizeBytes;
            if (sameSize && !piece.sha256.empty()) {
                report("Checking the existing piece " + std::to_string(position) + " of " + std::to_string(all.size()),
                       0, piece.sizeBytes);
                if (verifier_.verify(local.string(), piece.sizeBytes, piece.sha256).passed) continue;  // already right
            }
            toFetch.push_back(piece);
        }
    }

    // ---- room for it ---------------------------------------------------------------
    const fs::path staging = a.folder / (".gth-sync-" + a.titleId);
    long long stillToDownload = 0;
    for (const auto& piece : toFetch) {
        const fs::path partial = staging / (leafOfPath(piece.url) + ".part");
        const long long have = fs::exists(partial, ec) ? static_cast<long long>(fs::file_size(partial, ec)) : 0;
        stillToDownload += std::max<long long>(0, piece.sizeBytes - have);
    }
    fs::create_directories(a.folder, ec);
    const auto space = fs::space(a.folder, ec);
    if (!ec && static_cast<long long>(space.available) < stillToDownload + kSpaceMargin) {
        return fail("Not enough free space: " + formatSize(stillToDownload) + " to download and " +
                    formatSize(static_cast<long long>(space.available)) + " free on " + a.folder.root_name().string() + ".");
    }

    // ---- download and verify each piece into a staging folder -----------------
    fs::create_directories(staging, ec);
    int position = 0;
    for (const auto& piece : toFetch) {
        ++position;
        if (cancelled()) return wasCancelled();

        const std::string leaf = leafOfPath(piece.url);
        const fs::path staged = staging / (leaf + ".part");
        const std::string step = "Downloading " + std::to_string(position) + " of " + std::to_string(toFetch.size()) + ": " + leaf;
        report(step, 0, piece.sizeBytes);

        const auto download = downloader_.download(piece.url, staged, piece.sizeBytes, [&](long long soFar) {
            report(step, soFar, piece.sizeBytes);
            return !cancelled();
        });
        if (download.cancelled) return wasCancelled();
        if (!download.success) return fail("Couldn't download " + leaf + ": " + download.error, /*transient=*/true);

        report("Verifying " + leaf, piece.sizeBytes, piece.sizeBytes);
        const auto verified = verifier_.verify(staged.string(), piece.sizeBytes,
                                               piece.sha256.empty() ? std::nullopt : std::optional<std::string>(piece.sha256));
        if (!verified.passed) {
            fs::remove(staged, ec);
            return fail("The download of " + leaf + " failed verification against Sony's checksum, so it was discarded: " +
                        verified.errorMessage, /*transient=*/true);
        }
        ++result.filesDownloaded;
        result.bytesDownloaded += piece.sizeBytes;
    }

    // ---- the metadata has to belong to the game files it's added to -----------
    const auto stagedPathFor = [&](const std::string& leaf) -> std::optional<fs::path> {
        for (const auto& piece : toFetch) {
            if (lower(leafOfPath(piece.url)) == lower(leaf)) return staging / (leafOfPath(piece.url) + ".part");
        }
        return std::nullopt;
    };
    if (a.metadataPiece) {
        const auto stagedMetadata = stagedPathFor(leafOfPath(a.metadataPiece->url));
        const fs::path metadataPath = stagedMetadata ? *stagedMetadata : (a.metadata ? a.metadata->path : fs::path());
        const std::string firstLeaf = a.pfsPieces.empty() ? std::string() : leafOfPath(a.pfsPieces.front().url);
        const auto stagedFirst = firstLeaf.empty() ? std::nullopt : stagedPathFor(firstLeaf);
        const fs::path firstPartPath = stagedFirst ? *stagedFirst : a.numbered.front().path;
        if (!metadataPath.empty() && !MetadataPieceBelongsToPackage(firstPartPath, metadataPath)) {
            if (result.kind == SyncKind::CompleteMetadata) {
                // Same sizes as Sony's latest, yet not the same package: it needs a full update after all.
                // Sony's metadata piece stays in the staging folder, verified, for that update to use.
                if (mode == SyncMode::Full && !escalated) return runOnce(gameVariantId, mode, callbacks, /*escalated=*/true);
                result.kind = SyncKind::Update;
                result.skippedNeedsUpdate = true;
            }
            return fail("Sony's metadata piece doesn't match these game files - they're an older or different build than "
                        "Sony's latest (" + a.latest.version + "), so they need a full update. Nothing in the archive was changed.");
        }
    }

    // ---- put the verified pieces in place, all or nothing --------------------
    struct Move {
        fs::path staged, dest, backup;
        bool movedOld = false;
        bool placedNew = false;
    };
    std::vector<Move> moves;
    for (const auto& piece : toFetch) {
        const std::string leaf = leafOfPath(piece.url);
        moves.push_back({staging / (leaf + ".part"), a.folder / leaf, a.folder / (leaf + ".gth-old"), false, false});
    }

    // Game pieces on disk that Sony's package no longer has (an update can have fewer).
    std::vector<Move> removals;
    if (result.kind == SyncKind::Update) {
        for (const auto& local : a.numbered) {
            bool inSonys = false;
            for (const auto& piece : a.pfsPieces) inSonys = inSonys || lower(leafOfPath(piece.url)) == fileNameLower(local.path);
            if (!inSonys) removals.push_back({fs::path(), local.path, fs::path(local.path.string() + ".gth-old"), false, false});
        }
    }

    const auto rollBack = [&] {
        for (auto it = moves.rbegin(); it != moves.rend(); ++it) {
            if (it->placedNew) fs::remove(it->dest, ec);
            if (it->movedOld) fs::rename(it->backup, it->dest, ec);
        }
        for (auto& removal : removals) {
            if (removal.movedOld) fs::rename(removal.backup, removal.dest, ec);
        }
    };

    report("Putting the verified files in place", 0, 0);
    for (auto& move : moves) {
        if (fs::exists(move.dest, ec)) {
            fs::remove(move.backup, ec);
            fs::rename(move.dest, move.backup, ec);
            if (ec) {
                const std::string why = ec.message();
                rollBack();
                return fail("Couldn't replace " + move.dest.filename().string() + " (" + why + "); nothing was changed.",
                            /*transient=*/true);
            }
            move.movedOld = true;
        }
        fs::rename(move.staged, move.dest, ec);
        if (ec) {
            const std::string why = ec.message();
            rollBack();
            return fail("Couldn't move " + move.dest.filename().string() + " into place (" + why + "); nothing was changed.",
                        /*transient=*/true);
        }
        move.placedNew = true;
    }
    for (auto& removal : removals) {
        fs::remove(removal.backup, ec);
        fs::rename(removal.dest, removal.backup, ec);
        if (ec) {
            const std::string why = ec.message();
            rollBack();
            return fail("Couldn't set aside the old " + removal.dest.filename().string() + " (" + why + "); nothing was changed.",
                        /*transient=*/true);
        }
        removal.movedOld = true;
    }

    // Everything is in place: the old copies can go.
    for (const auto& move : moves) {
        if (move.movedOld) fs::remove(move.backup, ec);
    }
    for (const auto& removal : removals) fs::remove(removal.backup, ec);
    fs::remove_all(staging, ec);

    // A version typed in by hand (or an earlier scan's) describes the files as they were; Sony's package
    // now says what they are, so hand the version back to automatic detection.
    std::string ignored;
    scanner_.pinReleaseVersion(a.storage.rootPath, a.version.relativePath, "", ignored);

    result.success = true;
    result.message = result.kind == SyncKind::CompleteMetadata
                         ? "Added the metadata piece (" + formatSize(result.bytesDownloaded) + "); the title and version can now be read from the files."
                         : "Brought the package up to Sony's latest (" + a.latest.version + "): downloaded " +
                               std::to_string(result.filesDownloaded) + " piece(s), " + formatSize(result.bytesDownloaded) + ".";
    return result;
}

// ---- PS4 updates ---------------------------------------------------------------

namespace {

// A PS4 update is ONE .pkg that Sony's servers hand out as consecutive pieces ("<name>_0.pkg", "<name>_1.pkg",
// ...; a small update may be a single piece). The console and the library scanner want it joined: "<name>.pkg".
struct Ps4UpdateLayout {
    std::string finalName;  // "<name>.pkg"
    long long totalBytes = 0;
};

// "GAME-A0136-V0100_2.pkg" -> stem "GAME-A0136-V0100", index 2; a name with no "_<n>" has index -1.
void splitPieceName(const std::string& leaf, std::string& stem, int& index) {
    stem = leaf.size() > 4 ? leaf.substr(0, leaf.size() - 4) : leaf;  // without ".pkg"
    index = partIndexOfFileName(leaf);
    if (index >= 0) stem.resize(stem.find_last_of('_'));
}

// Checks that `pieces` really are consecutive parts of one file and names the file they make up. Anything
// else is refused: joining pieces that aren't parts of one file would put a broken package in the archive.
std::optional<Ps4UpdateLayout> layoutOfPs4Update(const std::vector<core::ReleasePiece>& pieces, long long packageSizeBytes,
                                                  std::string& why) {
    Ps4UpdateLayout layout;
    std::string stem;
    for (size_t i = 0; i < pieces.size(); ++i) {
        const auto& piece = pieces[i];
        if (piece.offsetBytes >= 0 && piece.offsetBytes != layout.totalBytes) {
            why = "Sony's pieces of this update aren't consecutive parts of one file";
            return std::nullopt;
        }
        layout.totalBytes += piece.sizeBytes;

        std::string pieceStem;
        int index = -1;
        splitPieceName(leafOfPath(piece.url), pieceStem, index);
        const bool numberedRight = pieces.size() == 1 ? (index == -1 || index == 0) : index == static_cast<int>(i);
        if (!numberedRight || (i > 0 && lower(pieceStem) != lower(stem))) {
            why = "Sony's pieces of this update aren't named as parts of one file (" + leafOfPath(piece.url) + ")";
            return std::nullopt;
        }
        if (i == 0) stem = pieceStem;
    }
    if (packageSizeBytes > 0 && packageSizeBytes != layout.totalBytes) {
        why = "Sony's pieces of this update add up to " + std::to_string(layout.totalBytes) + " bytes, but the update is " +
              std::to_string(packageSizeBytes);
        return std::nullopt;
    }
    layout.finalName = stem + ".pkg";
    if (!isSafePieceFileName(layout.finalName)) {
        why = "the update's file name isn't safe to use";
        return std::nullopt;
    }
    return layout;
}

// Writes `sources`, one after the other, into `target`. Returns "" when it worked, otherwise what went wrong;
// `progress` gets the bytes written so far and returns false to stop (then `cancelled` is set and "" returned).
std::string joinFiles(const std::vector<fs::path>& sources, const fs::path& target, long long totalBytes,
                      const std::function<bool(long long)>& progress, bool& cancelled) {
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) return "couldn't create " + target.string();

    std::vector<char> buffer(4 << 20);
    long long written = 0;
    for (const auto& source : sources) {
        std::ifstream in(source, std::ios::binary);
        if (!in) return "couldn't read " + source.string();
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize got = in.gcount();
            if (got <= 0) break;
            out.write(buffer.data(), got);
            if (!out) return "couldn't write to " + target.string() + " (disk full?)";
            written += got;
            if (progress && !progress(written)) {
                cancelled = true;
                return {};
            }
        }
    }
    out.flush();
    if (!out) return "couldn't write to " + target.string() + " (disk full?)";
    out.close();
    if (written != totalBytes) {
        return "the joined file has " + std::to_string(written) + " of " + std::to_string(totalBytes) + " bytes";
    }
    return {};
}

}  // namespace

struct PackageSyncService::Ps4Analysis {
    core::GameVariant variant;
    core::Storage storage;
    std::string titleId;
    std::string contentId;
    std::string installedVersion;
    bool noUpdatePublished = false;
    bool upToDate = false;
    core::LatestRelease latest;
    Ps4UpdateLayout layout;  // the file the pieces make up
    fs::path updatesFolder;  // where the update's files go
};

bool PackageSyncService::isPs4(const std::string& gameVariantId) {
    const auto variant = variantRepository_.getById(gameVariantId);
    return variant && variant->platform == "PS4";
}

std::optional<PackageSyncService::Ps4Analysis> PackageSyncService::analyzePs4(const std::string& gameVariantId,
                                                                             SyncPlan& plan) {
    const auto variant = variantRepository_.getById(gameVariantId);
    if (!variant) {
        plan.detail = "That game isn't in the library any more.";
        return std::nullopt;
    }
    plan.storageId = variant->storageId;
    if (variant->status != core::GameVariantStatus::Active) {
        plan.detail = "The game's files weren't found by the last scan.";
        return std::nullopt;
    }
    const auto storage = storageRepository_.getById(variant->storageId);
    if (!storage) {
        plan.detail = "The storage this game was found on has been removed.";
        return std::nullopt;
    }
    const auto version = currentActiveVersion(versionRepository_, gameVariantId);
    if (!version) {
        plan.detail = "The game's files weren't found by the last scan.";
        return std::nullopt;
    }
    const std::string titleId = titleIdFromVersionPath(version->relativePath);
    plan.titleId = titleId;
    if (titleId.empty()) {
        plan.detail = "The game's files have no Content ID to look up.";
        return std::nullopt;
    }
    if (version->version.empty()) {
        plan.detail = "The installed version can't be read from the game's files, so it can't be compared with Sony's update.";
        return std::nullopt;
    }

    Ps4Analysis a;
    a.variant = *variant;
    a.storage = *storage;
    a.titleId = titleId;
    a.contentId = leafOfPath(version->relativePath);
    a.installedVersion = version->version;

    const auto latest = latestReleases_.latestRelease("PS4", titleId);
    if (latest.status == core::LatestReleaseStatus::NoUpdatePublished) {
        a.noUpdatePublished = true;
        return a;
    }
    if (latest.status != core::LatestReleaseStatus::Found) {
        plan.detail = latest.detail;
        return std::nullopt;
    }
    a.latest = latest.release;
    plan.latestVersion = a.latest.version;
    if (core::compareVersions(a.installedVersion, a.latest.version) >= 0) {
        a.upToDate = true;
        return a;
    }

    if (a.latest.pieces.empty()) {
        plan.detail = "Sony's answer didn't list the update's files, so " + a.latest.version + " can't be downloaded from here.";
        return std::nullopt;
    }
    for (const auto& piece : a.latest.pieces) {
        if (!isSonyUrl(piece.url) || !isSafePieceFileName(leafOfPath(piece.url)) || piece.sizeBytes <= 0 ||
            piece.sha256.empty()) {
            plan.detail = "Sony's answer contained a file address or checksum that isn't safe to use, so nothing was downloaded.";
            return std::nullopt;
        }
        if (lower(leafOfPath(piece.url)).rfind(lower(a.contentId), 0) != 0) {
            plan.detail = "Sony's update for this title belongs to a different edition (its file is named " +
                          leafOfPath(piece.url) + ", yours " + a.contentId + "), so nothing was downloaded.";
            return std::nullopt;
        }
    }

    std::string layoutProblem;
    const auto layout = layoutOfPs4Update(a.latest.pieces, a.latest.packageSizeBytes, layoutProblem);
    if (!layout) {
        plan.detail = layoutProblem + ", so nothing was downloaded.";
        return std::nullopt;
    }
    a.layout = *layout;

    const auto files = fileRepository_.getByVersionId(version->gameVersionId);
    if (files.empty()) {
        plan.detail = "The game's files weren't found by the last scan.";
        return std::nullopt;
    }
    const fs::path gameFolder = (fs::path(storage->rootPath) / fs::path(files.front().relativePath)).parent_path();
    a.updatesFolder = lower(gameFolder.filename().string()) == "updates" ? gameFolder : gameFolder / "Updates";
    return a;
}

SyncPlan PackageSyncService::planPs4(const std::string& gameVariantId) {
    SyncPlan result;
    const auto a = analyzePs4(gameVariantId, result);
    if (!a) {
        result.kind = SyncKind::Unavailable;
        return result;
    }
    result.latestVersion = a->latest.version;
    if (a->noUpdatePublished) {
        result.kind = SyncKind::NothingToDo;
        result.detail = "Sony lists no update for this game, so the game as released is the latest.";
        return result;
    }
    if (a->upToDate) {
        result.kind = SyncKind::NothingToDo;
        result.detail = "Already Sony's latest update (" + a->latest.version + ").";
        return result;
    }
    result.kind = SyncKind::Update;
    std::error_code ec;
    const fs::path finalPath = a->updatesFolder / a->layout.finalName;
    if (fs::is_regular_file(finalPath, ec) && static_cast<long long>(fs::file_size(finalPath, ec)) == a->layout.totalBytes) {
        result.detail = "Update " + a->latest.version + " is already in the game's Updates folder (" + a->layout.finalName +
                        "); it only needs a rescan to show up.";
        return result;
    }
    for (const auto& piece : a->latest.pieces) {
        const fs::path have = a->updatesFolder / leafOfPath(piece.url);
        if (!fs::exists(have, ec) || static_cast<long long>(fs::file_size(have, ec)) != piece.sizeBytes) {
            result.bytesToDownload += piece.sizeBytes;
        }
    }
    result.detail = "Update " + a->latest.version + " is out (you have " + a->installedVersion + "): " +
                    std::to_string(a->latest.pieces.size()) + " file(s), " + formatSize(result.bytesToDownload) +
                    " to download into the game's Updates folder.";
    return result;
}

SyncResult PackageSyncService::runPs4(const std::string& gameVariantId, SyncMode mode, const SyncCallbacks& callbacks) {
    SyncResult result;
    SyncPlan basePlan;
    const auto a = analyzePs4(gameVariantId, basePlan);
    result.titleId = basePlan.titleId;
    result.storageId = basePlan.storageId;
    if (!a) {
        result.kind = SyncKind::Unavailable;
        result.message = basePlan.detail;
        return result;
    }
    if (a->noUpdatePublished || a->upToDate) {
        result.kind = SyncKind::NothingToDo;
        result.success = true;
        result.message = a->noUpdatePublished ? "Sony lists no update for this game, so the game as released is the latest."
                                              : "Already Sony's latest update (" + a->latest.version + ").";
        return result;
    }

    result.kind = SyncKind::Update;
    const auto& pieces = a->latest.pieces;
    const long long totalBytes = a->layout.totalBytes;
    std::error_code ec;
    if (mode == SyncMode::MetadataOnly) {
        result.skippedNeedsUpdate = true;
        result.message = "Update " + a->latest.version + " is out (you have " + a->installedVersion + "): downloading it takes " +
                         formatSize(totalBytes) + " and isn't done automatically.";
        return result;
    }

    const auto cancelled = [&] { return callbacks.shouldCancel && callbacks.shouldCancel(); };
    const auto report = [&](const std::string& step, long long done, long long total) {
        if (callbacks.onProgress) callbacks.onProgress(SyncProgress{step, done, total});
    };
    const auto fail = [&](const std::string& message, bool transient = false) {
        result.success = false;
        result.transient = transient;
        result.message = message;
        return result;
    };
    const auto wasCancelled = [&] {
        result.cancelled = true;
        result.message = "Cancelled - what was downloaded so far is kept and will be continued next time.";
        return result;
    };

    // The whole update may already be there (an earlier run put it in place but the library hasn't been rescanned).
    const fs::path finalPath = a->updatesFolder / a->layout.finalName;
    if (fs::is_regular_file(finalPath, ec) && static_cast<long long>(fs::file_size(finalPath, ec)) == totalBytes) {
        result.success = true;
        result.message = "Update " + a->latest.version + " is already in " + a->updatesFolder.string() + " as " +
                         a->layout.finalName + "; a rescan shows it.";
        return result;
    }

    // Pieces already saved loose in the Updates folder (by an earlier run, or by an older version of this app)
    // that are Sony's, byte for byte, aren't fetched again.
    const fs::path staging = a->updatesFolder / (".gth-sync-" + a->titleId);
    std::vector<fs::path> pieceOnDisk(pieces.size());  // where each piece is before joining
    std::vector<fs::path> looseReused;
    std::vector<core::ReleasePiece> toFetch;
    for (size_t i = 0; i < pieces.size(); ++i) {
        if (cancelled()) return wasCancelled();
        const auto& piece = pieces[i];
        const fs::path loose = a->updatesFolder / leafOfPath(piece.url);
        if (fs::exists(loose, ec) && static_cast<long long>(fs::file_size(loose, ec)) == piece.sizeBytes) {
            report("Checking the existing file " + std::to_string(i + 1) + " of " + std::to_string(pieces.size()), 0, piece.sizeBytes);
            if (verifier_.verify(loose.string(), piece.sizeBytes, piece.sha256).passed) {
                pieceOnDisk[i] = loose;
                looseReused.push_back(loose);
                continue;
            }
        }
        pieceOnDisk[i] = staging / (leafOfPath(piece.url) + ".part");
        toFetch.push_back(piece);
    }

    // Room for it: what is still to be fetched, plus - when several pieces are joined - the joined file.
    long long stillToDownload = 0;
    for (const auto& piece : toFetch) {
        const fs::path partial = staging / (leafOfPath(piece.url) + ".part");
        const long long have = fs::exists(partial, ec) ? static_cast<long long>(fs::file_size(partial, ec)) : 0;
        stillToDownload += std::max<long long>(0, piece.sizeBytes - have);
    }
    const long long joinedBytes = pieces.size() > 1 ? totalBytes : 0;
    fs::create_directories(a->updatesFolder, ec);
    const auto space = fs::space(a->updatesFolder, ec);
    if (!ec && static_cast<long long>(space.available) < stillToDownload + joinedBytes + kSpaceMargin) {
        return fail("Not enough free space: " + formatSize(stillToDownload + joinedBytes) + " needed (download" +
                    (joinedBytes > 0 ? " plus the joined file" : "") + ") and " +
                    formatSize(static_cast<long long>(space.available)) + " free on " + a->updatesFolder.root_name().string() + ".");
    }

    fs::create_directories(staging, ec);
    int position = 0;
    for (const auto& piece : toFetch) {
        ++position;
        if (cancelled()) return wasCancelled();
        const std::string leaf = leafOfPath(piece.url);
        const fs::path staged = staging / (leaf + ".part");
        const std::string step = "Downloading " + std::to_string(position) + " of " + std::to_string(toFetch.size()) + ": " + leaf;
        report(step, 0, piece.sizeBytes);

        const auto download = downloader_.download(piece.url, staged, piece.sizeBytes, [&](long long soFar) {
            report(step, soFar, piece.sizeBytes);
            return !cancelled();
        });
        if (download.cancelled) return wasCancelled();
        if (!download.success) return fail("Couldn't download " + leaf + ": " + download.error, /*transient=*/true);

        report("Verifying " + leaf, piece.sizeBytes, piece.sizeBytes);
        const auto verified = verifier_.verify(staged.string(), piece.sizeBytes, piece.sha256);
        if (!verified.passed) {
            fs::remove(staged, ec);
            return fail("The download of " + leaf + " failed verification against Sony's checksum, so it was discarded: " +
                        verified.errorMessage, /*transient=*/true);
        }
        ++result.filesDownloaded;
        result.bytesDownloaded += piece.sizeBytes;
    }

    // Every piece is on disk and matches Sony's checksum. Several pieces are joined into the one .pkg they are
    // parts of - the console and the scanner only understand that file.
    fs::path assembled;
    if (pieces.size() == 1) {
        assembled = pieceOnDisk[0];
    } else {
        assembled = staging / (a->layout.finalName + ".joined");
        const std::string step = "Joining the pieces into " + a->layout.finalName;
        report(step, 0, totalBytes);
        bool joinCancelled = false;
        const std::string problem = joinFiles(pieceOnDisk, assembled, totalBytes, [&](long long done) {
            report(step, done, totalBytes);
            return !cancelled();
        }, joinCancelled);
        if (joinCancelled) {
            fs::remove(assembled, ec);
            return wasCancelled();
        }
        if (!problem.empty()) {
            fs::remove(assembled, ec);
            return fail("Couldn't join the update's pieces: " + problem);
        }
    }

    // Put it in place, all or nothing. Nothing that was already in the game's folders is deleted.
    report("Putting the verified update in place", 0, 0);
    const fs::path backup = fs::path(finalPath.string() + ".gth-old");
    bool movedOld = false;
    if (fs::exists(finalPath, ec)) {
        fs::remove(backup, ec);
        fs::rename(finalPath, backup, ec);
        if (ec) {
            const std::string why = ec.message();
            return fail("Couldn't replace " + finalPath.filename().string() + " (" + why + "); nothing was changed.",
                        /*transient=*/true);
        }
        movedOld = true;
    }
    fs::rename(assembled, finalPath, ec);
    if (ec) {
        const std::string why = ec.message();
        if (movedOld) fs::rename(backup, finalPath, ec);
        return fail("Couldn't move " + finalPath.filename().string() + " into place (" + why + "); nothing was changed.",
                    /*transient=*/true);
    }
    if (movedOld) fs::remove(backup, ec);
    // The loose pieces that went into the joined file (each proved to be Sony's) are now part of it.
    if (pieces.size() > 1) {
        for (const auto& loose : looseReused) fs::remove(loose, ec);
    }
    fs::remove_all(staging, ec);

    // The new update is verified and in place - anything else in this game's Updates folder can only be a
    // superseded version (every version gets its own file name, "<content id>-A####-V####.pkg" - see
    // layoutOfPs4Update), so it's just dead weight now. Content IDs are long and specific enough that this
    // prefix match can't accidentally catch another game's file. Swept only now, after the new file has
    // already replaced it in every way that matters - never before, so a failed download never leaves the
    // game with neither version.
    long long freedBytes = 0;
    int filesRemoved = 0;
    for (const auto& entry : fs::directory_iterator(a->updatesFolder, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        const std::string name = entry.path().filename().string();
        if (entry.path() == finalPath || lower(name).rfind(lower(a->contentId), 0) != 0) continue;
        const long long size = static_cast<long long>(entry.file_size(ec));
        if (fs::remove(entry.path(), ec)) {
            freedBytes += size;
            ++filesRemoved;
        }
    }

    result.success = true;
    result.message = "Downloaded update " + a->latest.version + " (you had " + a->installedVersion + "): " +
                     std::to_string(result.filesDownloaded) + " file(s), " + formatSize(result.bytesDownloaded) + ", saved as " +
                     a->layout.finalName + " in " + a->updatesFolder.string() + ".";
    if (filesRemoved > 0) {
        result.message += " Removed " + std::to_string(filesRemoved) + " outdated update file(s) (" + formatSize(freedBytes) + ").";
    }
    return result;
}

SyncBatchResult PackageSyncService::runAll(SyncMode mode, const SyncCallbacks& callbacks) {
    SyncBatchResult batch;
    const auto cancelled = [&] { return callbacks.shouldCancel && callbacks.shouldCancel(); };
    const auto record = [&](const SyncGameOutcome& outcome) {
        batch.games.push_back(outcome);
        if (callbacks.onGameDone) callbacks.onGameDone(outcome);
    };

    // First find out which games have anything to do, so the line - and how long it is - is known before the
    // first download starts. Games that are already current, or that Sony can't be asked about, never enter it.
    struct Waiting {
        std::string variantId;
        SyncPlan plan;
    };
    std::vector<Waiting> line;
    for (const auto& variant : variantRepository_.getAll()) {
        if (variant.status != core::GameVariantStatus::Active || (variant.platform != "PS5" && variant.platform != "PS4")) continue;
        if (cancelled()) {
            batch.cancelled = true;
            return batch;
        }

        SyncPlan plan = this->plan(variant.gameVariantId);
        if (plan.kind == SyncKind::Unavailable) {
            ++batch.unavailable;
        } else if (plan.kind == SyncKind::NothingToDo) {
            ++batch.alreadyFine;
        } else if (plan.kind == SyncKind::Update && mode == SyncMode::MetadataOnly) {
            ++batch.needUpdate;
            record(SyncGameOutcome{plan.titleId, plan.kind, false, true, plan.detail, 0});
        } else {
            line.push_back({variant.gameVariantId, std::move(plan)});
        }
    }

    if (callbacks.onQueue) {
        std::vector<SyncQueueItem> items;
        for (const auto& waiting : line) items.push_back({waiting.plan.titleId, waiting.plan.kind, waiting.plan.bytesToDownload});
        callbacks.onQueue(items);
    }

    // One game's turn, told where it stands in the line.
    const auto runOne = [&](size_t at) {
        SyncCallbacks forThisGame = callbacks;
        forThisGame.onProgress = [&, at](const SyncProgress& progress) {
            if (!callbacks.onProgress) return;
            SyncProgress tagged = progress;
            tagged.titleId = line[at].plan.titleId;
            tagged.gameIndex = static_cast<int>(at) + 1;
            tagged.gameCount = static_cast<int>(line.size());
            callbacks.onProgress(tagged);
        };
        return run(line[at].variantId, mode, forThisGame);
    };
    const auto outcomeOf = [](const SyncResult& r) {
        return SyncGameOutcome{r.titleId, r.kind, r.success, r.skippedNeedsUpdate, r.message, r.bytesDownloaded};
    };

    std::vector<size_t> retryLater;         // indexes into `line` of the games that failed in a way that can pass
    std::map<size_t, size_t> failureSlot;   // line index -> where that game's failure sits in batch.games
    for (size_t at = 0; at < line.size(); ++at) {
        if (cancelled()) {
            batch.cancelled = true;
            break;
        }

        const auto result = runOne(at);
        if (result.cancelled) {
            batch.cancelled = true;
            break;
        }

        const SyncGameOutcome outcome = outcomeOf(result);
        if (result.skippedNeedsUpdate) {
            ++batch.needUpdate;
            record(outcome);
        } else if (result.kind == SyncKind::Unavailable) {
            ++batch.unavailable;
        } else if (result.kind == SyncKind::NothingToDo) {
            ++batch.alreadyFine;
        } else if (result.success) {
            if (result.kind == SyncKind::CompleteMetadata) ++batch.completed; else ++batch.updated;
            batch.bytesDownloaded += result.bytesDownloaded;
            batch.storageIds.insert(result.storageId);
            record(outcome);
        } else {
            ++batch.failed;
            failureSlot[at] = batch.games.size();
            record(outcome);
            if (result.transient) retryLater.push_back(at);
        }
    }

    // A dropped connection or a download that didn't match its checksum often works the second time. Rather than
    // leaving those games for the person to press the button again, the line goes back to them - after a pause,
    // since a network problem needs a moment to clear - a few times before giving up on them.
    for (int round = 1; round <= callbacks.retryRounds && !retryLater.empty() && !batch.cancelled; ++round) {
        if (callbacks.onProgress) {
            SyncProgress waiting;
            waiting.step = "Waiting before trying " + std::to_string(retryLater.size()) + " failed game(s) again (round " +
                           std::to_string(round) + " of " + std::to_string(callbacks.retryRounds) + ")";
            callbacks.onProgress(waiting);
        }
        const auto pause = callbacks.retryPause * round;
        for (std::chrono::seconds waited{0}; waited < pause && !cancelled(); waited += std::chrono::seconds(1)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::vector<size_t> stillFailing;
        for (const size_t at : retryLater) {
            if (cancelled()) {
                batch.cancelled = true;
                break;
            }
            const auto result = runOne(at);
            if (result.cancelled) {
                batch.cancelled = true;
                break;
            }

            const SyncGameOutcome outcome = outcomeOf(result);
            batch.games[failureSlot[at]] = outcome;  // the newest word on this game replaces its earlier failure
            if (callbacks.onGameDone) callbacks.onGameDone(outcome);

            const bool worked = result.success && !result.skippedNeedsUpdate && result.kind != SyncKind::Unavailable;
            if (worked) {
                --batch.failed;
                if (result.kind == SyncKind::NothingToDo) {
                    ++batch.alreadyFine;  // put right some other way in the meantime
                } else {
                    ++batch.recoveredOnRetry;
                    if (result.kind == SyncKind::CompleteMetadata) ++batch.completed; else ++batch.updated;
                    batch.bytesDownloaded += result.bytesDownloaded;
                    batch.storageIds.insert(result.storageId);
                }
            } else if (result.transient) {
                stillFailing.push_back(at);
            }
        }
        retryLater = std::move(stillFailing);
    }

    for (const auto& game : batch.games) {
        if (!game.success && !game.skippedNeedsUpdate) {
            batch.firstFailure = game.titleId + ": " + game.message;
            break;
        }
    }
    return batch;
}

std::vector<std::pair<std::string, std::string>> PackageSyncService::metadataPieceFiles() {
    std::vector<std::pair<std::string, std::string>> found;
    std::set<std::string> seen;
    std::map<std::string, std::optional<core::Storage>> storages;

    for (const auto& variant : variantRepository_.getAll()) {
        if (variant.status != core::GameVariantStatus::Active || variant.platform != "PS5") continue;
        const auto version = currentActiveVersion(versionRepository_, variant.gameVariantId);
        if (!version) continue;
        const std::string titleId = titleIdFromVersionPath(version->relativePath);
        if (titleId.empty() || seen.contains(titleId)) continue;

        for (const auto& file : fileRepository_.getByVersionId(version->gameVersionId)) {
            if (!isMetadataPieceFileName(file.relativePath)) continue;
            auto storage = storages.find(variant.storageId);
            if (storage == storages.end()) storage = storages.emplace(variant.storageId, storageRepository_.getById(variant.storageId)).first;
            if (!storage->second) break;

            const fs::path path = fs::path(storage->second->rootPath) / fs::path(file.relativePath);
            std::error_code ec;
            if (!fs::is_regular_file(path, ec)) continue;
            found.emplace_back(titleId, path.string());
            seen.insert(titleId);
            break;
        }
    }
    return found;
}

}  // namespace gth::application
