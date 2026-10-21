#pragma once

#include "gth/core/storage/IStorageRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class StorageRepository : public core::IStorageRepository {
public:
    explicit StorageRepository(Database& db);

    std::vector<core::Storage> getAll() override;
    std::optional<core::Storage> getById(const std::string& storageId) override;
    std::optional<core::Storage> getByRootPath(const std::string& rootPath) override;

    void add(const core::Storage& storage) override;
    void update(const core::Storage& storage) override;
    void remove(const std::string& storageId) override;

private:
    Database& db_;
};

}  // namespace gth::database
