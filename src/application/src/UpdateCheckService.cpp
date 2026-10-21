#include "gth/application/indexing/UpdateCheckService.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

#include "gth/application/indexing/ContentIds.h"
#include "gth/application/indexing/CurrentVersion.h"
#include "gth/application/indexing/PackagePieces.h"
#include "gth/core/enums/GameVariantStatus.h"
#include "gth/core/index/VersionComparer.h"

namespace gth::application {

namespace {

std::string formatGigabytes(long long bytes) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.2f GB", static_cast<double>(bytes) / 1024.0 / 1024.0 / 1024.0);
    return buffer;
}

}  // namespace

std::optional<std::vector<long long>> OrderedPartSizes(const std::vector<core::GameFile>& files) {
    if (files.empty()) return std::nullopt;

    // Sony's package ends with a metadata piece, "<content id>_sc.pkg", that
    // isn't numbered; it is the last piece of the package.
    std::optional<long long> metadataPieceSize;
    std::vector<const core::GameFile*> numbered;
    for (const auto& file : files) {
        if (isMetadataPieceFileName(file.relativePath)) {
            if (metadataPieceSize) return std::nullopt;  // two of them: not a package we understand
            metadataPieceSize = file.sizeBytes;
        } else {
            numbered.push_back(&file);
        }
    }
    if (numbered.empty()) return std::nullopt;

    std::vector<long long> sizes;
    if (numbered.size() == 1 && !metadataPieceSize) {
        sizes.push_back(numbered[0]->sizeBytes);  // a package that is one file is just that file
        return sizes;
    }

    std::vector<std::pair<int, long long>> parts;
    for (const auto* file : numbered) {
        const int index = partIndexOfFileName(file->relativePath);
        if (index < 0) return std::nullopt;
        parts.emplace_back(index, file->sizeBytes);
    }
    std::sort(parts.begin(), parts.end());
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].first != static_cast<int>(i)) return std::nullopt;  // a part is missing, or two share a number
    }

    sizes.reserve(parts.size() + 1);
    for (const auto& part : parts) sizes.push_back(part.second);
    if (metadataPieceSize) sizes.push_back(*metadataPieceSize);
    return sizes;
}

bool PartSizesMatchPackage(const std::vector<long long>& partSizes, const std::vector<long long>& pieceSizes) {
    if (partSizes.empty() || pieceSizes.empty()) return false;
    if (partSizes.size() != pieceSizes.size() && partSizes.size() + 1 != pieceSizes.size()) return false;
    return std::equal(partSizes.begin(), partSizes.end(), pieceSizes.begin());
}

UpdateCheckService::UpdateCheckService(core::IGameVariantRepository& variantRepository,
                                       core::IGameVersionRepository& versionRepository,
                                       core::IGameFileRepository& fileRepository,
                                       core::ILatestReleaseService& latestReleases)
    : variantRepository_(variantRepository),
      versionRepository_(versionRepository),
      fileRepository_(fileRepository),
      latestReleases_(latestReleases) {}

UpdateCheckSummary UpdateCheckService::checkAll() {
    UpdateCheckSummary summary;

    // Two regional copies of one game, or the same game on two drives, ask
    // Sony the same question - once is enough.
    std::map<std::string, core::LatestReleaseResult> answers;

    for (const auto& variant : variantRepository_.getAll()) {
        if (variant.status != core::GameVariantStatus::Active) continue;
        if (variant.platform != "PS4" && variant.platform != "PS5") continue;

        auto current = currentActiveVersion(versionRepository_, variant.gameVariantId);
        if (!current) continue;

        const std::string titleId = titleIdFromVersionPath(current->relativePath);
        if (titleId.empty()) continue;

        UpdateCheckEntry entry;
        entry.gameVariantId = variant.gameVariantId;
        entry.installedVersion = current->version;

        const std::string key = variant.platform + "|" + titleId;
        auto answer = answers.find(key);
        if (answer == answers.end()) answer = answers.emplace(key, latestReleases_.latestRelease(variant.platform, titleId)).first;
        const auto& latest = answer->second;

        switch (latest.status) {
            case core::LatestReleaseStatus::NoSonyLink:
                entry.state = UpdateState::NoSonyData;
                entry.detail = latest.detail;
                break;

            case core::LatestReleaseStatus::Failed:
                entry.state = UpdateState::CheckFailed;
                entry.detail = latest.detail;
                break;

            case core::LatestReleaseStatus::NoUpdatePublished:
                entry.state = UpdateState::UpToDate;
                entry.detail = "Sony lists no update for this game";
                break;

            case core::LatestReleaseStatus::Found: {
                entry.latestVersion = latest.release.version;

                if (!current->version.empty()) {
                    if (core::compareVersions(current->version, latest.release.version) >= 0) {
                        entry.state = UpdateState::UpToDate;
                        entry.detail = "version " + current->version + " is Sony's latest";
                    } else {
                        entry.state = UpdateState::UpdateAvailable;
                        entry.detail = "you have " + current->version + ", Sony's latest is " + latest.release.version;
                    }
                    break;
                }

                // No version anywhere in the files: for a PS5 package, the sizes of its
                // split files still say whether it is Sony's latest one.
                const auto partSizes = OrderedPartSizes(fileRepository_.getByVersionId(current->gameVersionId));
                if (!latest.release.pieceSizes.empty() && partSizes) {
                    if (PartSizesMatchPackage(*partSizes, latest.release.pieceSizes)) {
                        entry.state = UpdateState::UpToDate;
                        entry.installedVersion = latest.release.version;
                        entry.installedVersionInferred = true;
                        entry.detail = "same file sizes as Sony's latest package (" + latest.release.version + ")";
                    } else {
                        entry.state = UpdateState::UpdateAvailable;
                        entry.detail = "package differs from Sony's latest (" + latest.release.version + ", " +
                                       formatGigabytes(latest.release.packageSizeBytes) + ") - an older or different build";
                    }
                } else {
                    entry.state = UpdateState::InstalledVersionUnknown;
                    entry.detail = "Sony's latest is " + latest.release.version + "; this copy's version isn't known";
                }
                break;
            }
        }

        switch (entry.state) {
            case UpdateState::UpToDate: ++summary.upToDate; break;
            case UpdateState::UpdateAvailable: ++summary.updatesAvailable; break;
            case UpdateState::InstalledVersionUnknown: ++summary.installedUnknown; break;
            case UpdateState::NoSonyData: ++summary.noSonyData; break;
            case UpdateState::CheckFailed:
                ++summary.failed;
                if (summary.firstFailure.empty()) summary.firstFailure = titleId + ": " + entry.detail;
                break;
        }
        summary.entries.push_back(std::move(entry));
    }
    return summary;
}

}  // namespace gth::application
