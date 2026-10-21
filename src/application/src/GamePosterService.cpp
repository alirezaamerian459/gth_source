#include "gth/application/indexing/GamePosterService.h"

#include <algorithm>
#include <filesystem>

#include "gth/application/indexing/CurrentVersion.h"
#include "gth/application/indexing/PackagePieces.h"
#include "gth/indexing/PkgFileParser.h"

namespace gth::application {

namespace fs = std::filesystem;

GamePosterService::GamePosterService(core::IGameVariantRepository& variantRepository,
                                      core::IGameVersionRepository& versionRepository,
                                      core::IGameFileRepository& fileRepository,
                                      core::IStorageRepository& storageRepository)
    : variantRepository_(variantRepository),
      versionRepository_(versionRepository),
      fileRepository_(fileRepository),
      storageRepository_(storageRepository) {}

namespace {
bool hasPkgExtension(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".pkg";
}
}  // namespace

std::optional<std::string> GamePosterService::extractPosterPng(const std::string& gameVariantId) {
    const auto variant = variantRepository_.getById(gameVariantId);
    if (!variant) return std::nullopt;

    const auto version = currentActiveVersion(versionRepository_, gameVariantId);
    if (!version) return std::nullopt;

    const auto storage = storageRepository_.getById(variant->storageId);
    if (!storage) return std::nullopt;

    struct Part {
        std::string absolutePath;
        long long sizeBytes;
        int splitIndex;
    };
    std::vector<Part> parts;
    std::optional<Part> metadataPiece;

    for (const auto& file : fileRepository_.getByVersionId(version->gameVersionId)) {
        if (!hasPkgExtension(file.relativePath)) continue;
        const fs::path absolutePath = fs::path(storage->rootPath) / fs::path(file.relativePath);
        std::error_code ec;
        if (!fs::is_regular_file(absolutePath, ec)) continue;

        Part part{absolutePath.string(), file.sizeBytes, partIndexOfFileName(file.relativePath)};
        if (isMetadataPieceFileName(file.relativePath)) {
            metadataPiece = std::move(part);
        } else {
            parts.push_back(std::move(part));
        }
    }

    // Whichever single part actually carries the entry table - every unsplit package, and a split
    // PS4 game's part 0 - reads directly, no ordering needed. A no-header-here result (a later part
    // of a split package) is normal and just moves on to the next candidate.
    for (const auto& part : parts) {
        if (auto poster = gth::indexing::PkgFileParser::extractPoster(part.absolutePath)) return poster;
    }

    // A split PS5 release: the entry table sits past every numbered part, in the "_sc.pkg" metadata
    // piece - read across all of them, in original-file order, as one virtual stream (see
    // PkgFileParser::parseSplitPs5Parts, which this mirrors).
    if (!metadataPiece) return std::nullopt;

    std::sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) {
        return std::max(a.splitIndex, 0) < std::max(b.splitIndex, 0);
    });

    std::vector<std::pair<std::string, long long>> orderedParts;
    orderedParts.reserve(parts.size() + 1);
    for (const auto& part : parts) orderedParts.emplace_back(part.absolutePath, part.sizeBytes);
    orderedParts.emplace_back(metadataPiece->absolutePath, metadataPiece->sizeBytes);

    return gth::indexing::PkgFileParser::extractPosterFromParts(orderedParts);
}

}  // namespace gth::application
