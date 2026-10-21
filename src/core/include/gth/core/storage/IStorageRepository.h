#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/entities/Storage.h"

namespace gth::core {

// Persistence contract for Storage. Implemented by the Database module; the
// Application layer depends only on this abstraction so it never references
// SQLite directly.
class IStorageRepository {
public:
    virtual ~IStorageRepository() = default;

    virtual std::vector<Storage> getAll() = 0;
    virtual std::optional<Storage> getById(const std::string& storageId) = 0;
    virtual std::optional<Storage> getByRootPath(const std::string& rootPath) = 0;

    virtual void add(const Storage& storage) = 0;
    virtual void update(const Storage& storage) = 0;
    virtual void remove(const std::string& storageId) = 0;
};

}  // namespace gth::core
