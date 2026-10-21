#pragma once

#include "gth/application/updates/IUpdateJobService.h"
#include "gth/core/updates/IUpdateJobRepository.h"

namespace gth::application {

class UpdateJobService : public IUpdateJobService {
public:
    explicit UpdateJobService(core::IUpdateJobRepository& jobRepository);

    core::UpdateJob queueUpdate(const std::string& gameVariantId, const std::string& oldVersion,
                                 const std::string& newVersion) override;

private:
    core::IUpdateJobRepository& jobRepository_;
};

}  // namespace gth::application
