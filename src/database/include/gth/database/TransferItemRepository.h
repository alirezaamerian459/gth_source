#pragma once

#include "gth/core/transfer/ITransferItemRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class TransferItemRepository : public core::ITransferItemRepository {
public:
    explicit TransferItemRepository(Database& db);

    std::vector<core::TransferItem> getByJobId(const std::string& transferJobId) override;
    std::optional<core::TransferItem> getById(const std::string& transferItemId) override;
    std::vector<core::TransferItem> getQueued() override;

    void add(const core::TransferItem& item) override;
    void update(const core::TransferItem& item) override;

private:
    Database& db_;
};

}  // namespace gth::database
