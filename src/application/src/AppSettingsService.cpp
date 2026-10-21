#include "gth/application/settings/AppSettingsService.h"

namespace gth::application {

AppSettingsService::AppSettingsService(core::IAppSettingsRepository& repository) : repository_(repository) {}

core::AppSettings AppSettingsService::getSettings() { return repository_.load(); }

void AppSettingsService::updateSettings(const core::AppSettings& settings) { repository_.save(settings); }

}  // namespace gth::application
