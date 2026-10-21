#pragma once

#include "gth/core/settings/AppSettings.h"

namespace gth::application {

class IAppSettingsService {
public:
    virtual ~IAppSettingsService() = default;

    virtual core::AppSettings getSettings() = 0;
    virtual void updateSettings(const core::AppSettings& settings) = 0;
};

}  // namespace gth::application
