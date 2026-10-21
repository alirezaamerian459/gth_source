#pragma once

#include <string>

#include "gth/core/entities/UpdateJob.h"

namespace gth::application {

// Queues an update, with duplicate prevention: if a non-terminal job for
// the same variant + target version already exists, that job is returned
// instead of creating a second one (per the spec's "Duplicate Prevention"
// requirement for the Update Queue).
class IUpdateJobService {
public:
    virtual ~IUpdateJobService() = default;

    virtual core::UpdateJob queueUpdate(const std::string& gameVariantId, const std::string& oldVersion,
                                         const std::string& newVersion) = 0;
};

}  // namespace gth::application
