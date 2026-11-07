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

    std::vector<core::StorageCategory> getCategories() override;
    std::optional<core::StorageCategory> getCategoryById(const std::string& categoryId) override;
    void addCategory(const core::StorageCategory& category) override;
    void updateCategory(const core::StorageCategory& category) override;
    void removeCategory(const std::string& categoryId) override;
    std::vector<std::string> getStorageIdsForCategory(const std::string& categoryId) override;
    void addStorageToCategory(const std::string& categoryId, const std::string& storageId) override;
    void removeStorageFromCategory(const std::string& categoryId, const std::string& storageId) override;

private:
    Database& db_;
};

}  // namespace gth::database
