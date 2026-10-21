#pragma once

#include "gth/core/settings/IAppSettingsRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class AppSettingsRepository : public core::IAppSettingsRepository {
public:
    explicit AppSettingsRepository(Database& db);

    core::AppSettings load() override;
    void save(const core::AppSettings& settings) override;

private:
    Database& db_;
};

}  // namespace gth::database
