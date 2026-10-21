#pragma once

#include "gth/core/settings/AppSettings.h"

namespace gth::core {

// Single-row persistence for AppSettings - no id/list/remove like every
// other repository in this codebase, since there's exactly one settings
// record for the whole install.
class IAppSettingsRepository {
public:
    virtual ~IAppSettingsRepository() = default;

    virtual AppSettings load() = 0;
    virtual void save(const AppSettings& settings) = 0;
};

}  // namespace gth::core
