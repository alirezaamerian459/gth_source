#pragma once

#include "gth/application/settings/IAppSettingsService.h"
#include "gth/core/settings/IAppSettingsRepository.h"

namespace gth::application {

class AppSettingsService : public IAppSettingsService {
public:
    explicit AppSettingsService(core::IAppSettingsRepository& repository);

    core::AppSettings getSettings() override;
    void updateSettings(const core::AppSettings& settings) override;

private:
    core::IAppSettingsRepository& repository_;
};

}  // namespace gth::application
