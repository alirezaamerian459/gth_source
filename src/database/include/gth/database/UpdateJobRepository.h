#pragma once

#include "gth/core/updates/IUpdateJobRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class UpdateJobRepository : public core::IUpdateJobRepository {
public:
    explicit UpdateJobRepository(Database& db);

    std::vector<core::UpdateJob> getAll() override;
    std::optional<core::UpdateJob> getById(const std::string& updateJobId) override;
    std::vector<core::UpdateJob> getByVariantId(const std::string& gameVariantId) override;
    std::optional<core::UpdateJob> getActiveJobForVariant(const std::string& gameVariantId,
                                                            const std::string& newVersion) override;

    void add(const core::UpdateJob& job) override;
    void update(const core::UpdateJob& job) override;

private:
    Database& db_;
};

}  // namespace gth::database
