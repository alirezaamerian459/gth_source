#pragma once

#include <vector>

#include "gth/application/updates/IUpdateDetector.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/index/IGameVersionRepository.h"
#include "gth/core/updates/IUpdateInfoRepository.h"
#include "gth/core/updates/IUpdateSource.h"

namespace gth::application {

class UpdateDetectorService : public IUpdateDetector {
public:
    UpdateDetectorService(core::IGameVariantRepository& variantRepository,
                           core::IGameVersionRepository& versionRepository,
                           core::IUpdateInfoRepository& updateInfoRepository,
                           std::vector<core::IUpdateSource*> sources);

    core::UpdateInfo detectForVariant(const core::GameVariant& variant) override;
    std::vector<core::UpdateInfo> detectForAllVariants() override;

private:
    core::IGameVariantRepository& variantRepository_;
    core::IGameVersionRepository& versionRepository_;
    core::IUpdateInfoRepository& updateInfoRepository_;
    std::vector<core::IUpdateSource*> sources_;

    std::string findCurrentVersion(const std::string& gameVariantId);
};

}  // namespace gth::application
