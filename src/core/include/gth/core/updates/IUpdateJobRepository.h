#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/entities/UpdateJob.h"

namespace gth::core {

class IUpdateJobRepository {
public:
    virtual ~IUpdateJobRepository() = default;

    virtual std::vector<UpdateJob> getAll() = 0;
    virtual std::optional<UpdateJob> getById(const std::string& updateJobId) = 0;
    virtual std::vector<UpdateJob> getByVariantId(const std::string& gameVariantId) = 0;

    // For duplicate-prevention: an existing job for this variant/target
    // version that hasn't reached a terminal state yet.
    virtual std::optional<UpdateJob> getActiveJobForVariant(const std::string& gameVariantId,
                                                              const std::string& newVersion) = 0;

    virtual void add(const UpdateJob& job) = 0;
    virtual void update(const UpdateJob& job) = 0;
};

}  // namespace gth::core
