#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/application/indexing/IUpdateChecker.h"
#include "gth/core/index/IGameFileRepository.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/index/IGameVersionRepository.h"
#include "gth/core/index/ILatestReleaseService.h"

namespace gth::application {

class UpdateCheckService : public IUpdateChecker {
public:
    UpdateCheckService(core::IGameVariantRepository& variantRepository, core::IGameVersionRepository& versionRepository,
                       core::IGameFileRepository& fileRepository, core::ILatestReleaseService& latestReleases);

    UpdateCheckSummary checkAll() override;

private:
    core::IGameVariantRepository& variantRepository_;
    core::IGameVersionRepository& versionRepository_;
    core::IGameFileRepository& fileRepository_;
    core::ILatestReleaseService& latestReleases_;
};

// The sizes of a release's split files in part order (..._0.pkg, ..._1.pkg,
// ...), or nullopt when they can't be put in a trustworthy order (a part
// missing from the middle, or names without a part number). A release that
// is one file gives that single size.
std::optional<std::vector<long long>> OrderedPartSizes(const std::vector<core::GameFile>& files);

// Whether files of these sizes are the package Sony describes with
// `pieceSizes`: every file the same size as its piece, allowing the package's
// last piece (its metadata block) to be absent, as it is from many dumps.
bool PartSizesMatchPackage(const std::vector<long long>& partSizes, const std::vector<long long>& pieceSizes);

}  // namespace gth::application
