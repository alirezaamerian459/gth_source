#pragma once

#include "gth/application/indexing/IGamePosterService.h"
#include "gth/core/index/IGameFileRepository.h"
#include "gth/core/index/IGameVariantRepository.h"
#include "gth/core/index/IGameVersionRepository.h"
#include "gth/core/storage/IStorageRepository.h"

namespace gth::application {

class GamePosterService : public IGamePosterService {
public:
    GamePosterService(core::IGameVariantRepository& variantRepository, core::IGameVersionRepository& versionRepository,
                       core::IGameFileRepository& fileRepository, core::IStorageRepository& storageRepository);

    std::optional<std::string> extractPosterPng(const std::string& gameVariantId) override;

private:
    core::IGameVariantRepository& variantRepository_;
    core::IGameVersionRepository& versionRepository_;
    core::IGameFileRepository& fileRepository_;
    core::IStorageRepository& storageRepository_;
};

}  // namespace gth::application
