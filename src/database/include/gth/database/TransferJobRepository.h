#pragma once

#include "gth/core/transfer/ITransferJobRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class TransferJobRepository : public core::ITransferJobRepository {
public:
    explicit TransferJobRepository(Database& db);

    std::vector<core::TransferJob> getAll() override;
    std::optional<core::TransferJob> getById(const std::string& transferJobId) override;

    void add(const core::TransferJob& job) override;
    void update(const core::TransferJob& job) override;

private:
    Database& db_;
};

}  // namespace gth::database
