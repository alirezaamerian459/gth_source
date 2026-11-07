#include "gth/database/StorageRepository.h"

namespace gth::database {

using core::Storage;

namespace {

constexpr const char* kSelectColumns =
    "StorageId, Name, Type, RootPath, TotalCapacityBytes, FreeSpaceBytes, UsedSpaceBytes, "
    "Status, FileSystem, LastScan, IsAvailable, CreatedAt, UpdatedAt, HealthPercent, TemperatureCelsius, "
    "HealthAlertReason";

constexpr const char* kSelectCategoryColumns = "CategoryId, Name, CreatedAt, UpdatedAt";

Storage mapRow(const Statement& stmt) {
    Storage storage;
    storage.storageId = stmt.columnText(0);
    storage.name = stmt.columnText(1);
    storage.type = core::storageTypeFromString(stmt.columnText(2));
    storage.rootPath = stmt.columnText(3);
    storage.totalCapacityBytes = stmt.columnInt64(4);
    storage.freeSpaceBytes = stmt.columnInt64(5);
    storage.usedSpaceBytes = stmt.columnInt64(6);
    storage.status = core::storageStatusFromString(stmt.columnText(7));
    storage.fileSystem = stmt.columnTextOrNull(8);
    storage.lastScan = stmt.columnTextOrNull(9);
    storage.isAvailable = stmt.columnBool(10);
    storage.createdAt = stmt.columnText(11);
    storage.updatedAt = stmt.columnText(12);
    if (const auto health = stmt.columnInt64OrNull(13)) storage.healthPercent = static_cast<int>(*health);
    if (const auto temp = stmt.columnInt64OrNull(14)) storage.temperatureCelsius = static_cast<int>(*temp);
    storage.healthAlertReason = stmt.columnTextOrNull(15);
    return storage;
}

core::StorageCategory mapCategoryRow(const Statement& stmt) {
    core::StorageCategory category;
    category.categoryId = stmt.columnText(0);
    category.name = stmt.columnText(1);
    category.createdAt = stmt.columnText(2);
    category.updatedAt = stmt.columnText(3);
    return category;
}

}  // namespace

StorageRepository::StorageRepository(Database& db) : db_(db) {}

std::vector<Storage> StorageRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM Storages ORDER BY Name;");
    std::vector<Storage> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<Storage> StorageRepository::getById(const std::string& storageId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM Storages WHERE StorageId = ?;");
    stmt.bind(1, storageId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

std::optional<Storage> StorageRepository::getByRootPath(const std::string& rootPath) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM Storages WHERE RootPath = ?;");
    stmt.bind(1, rootPath);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void StorageRepository::add(const Storage& storage) {
    auto stmt = db_.prepare(
        "INSERT INTO Storages (StorageId, Name, Type, RootPath, TotalCapacityBytes, FreeSpaceBytes, "
        "UsedSpaceBytes, Status, FileSystem, LastScan, IsAvailable, CreatedAt, UpdatedAt, HealthPercent, "
        "TemperatureCelsius, HealthAlertReason) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, storage.storageId);
    stmt.bind(2, storage.name);
    stmt.bind(3, core::toString(storage.type));
    stmt.bind(4, storage.rootPath);
    stmt.bind(5, storage.totalCapacityBytes);
    stmt.bind(6, storage.freeSpaceBytes);
    stmt.bind(7, storage.usedSpaceBytes);
    stmt.bind(8, core::toString(storage.status));
    if (storage.fileSystem) stmt.bind(9, *storage.fileSystem); else stmt.bindNull(9);
    if (storage.lastScan) stmt.bind(10, *storage.lastScan); else stmt.bindNull(10);
    stmt.bind(11, static_cast<long long>(storage.isAvailable ? 1 : 0));
    stmt.bind(12, storage.createdAt);
    stmt.bind(13, storage.updatedAt);
    if (storage.healthPercent) stmt.bind(14, static_cast<long long>(*storage.healthPercent)); else stmt.bindNull(14);
    if (storage.temperatureCelsius) stmt.bind(15, static_cast<long long>(*storage.temperatureCelsius));
    else stmt.bindNull(15);
    if (storage.healthAlertReason) stmt.bind(16, *storage.healthAlertReason); else stmt.bindNull(16);
    stmt.step();
}

void StorageRepository::update(const Storage& storage) {
    auto stmt = db_.prepare(
        "UPDATE Storages SET Name = ?, Type = ?, RootPath = ?, TotalCapacityBytes = ?, FreeSpaceBytes = ?, "
        "UsedSpaceBytes = ?, Status = ?, FileSystem = ?, LastScan = ?, IsAvailable = ?, UpdatedAt = ?, "
        "HealthPercent = ?, TemperatureCelsius = ?, HealthAlertReason = ? "
        "WHERE StorageId = ?;");
    stmt.bind(1, storage.name);
    stmt.bind(2, core::toString(storage.type));
    stmt.bind(3, storage.rootPath);
    stmt.bind(4, storage.totalCapacityBytes);
    stmt.bind(5, storage.freeSpaceBytes);
    stmt.bind(6, storage.usedSpaceBytes);
    stmt.bind(7, core::toString(storage.status));
    if (storage.fileSystem) stmt.bind(8, *storage.fileSystem); else stmt.bindNull(8);
    if (storage.lastScan) stmt.bind(9, *storage.lastScan); else stmt.bindNull(9);
    stmt.bind(10, static_cast<long long>(storage.isAvailable ? 1 : 0));
    stmt.bind(11, storage.updatedAt);
    if (storage.healthPercent) stmt.bind(12, static_cast<long long>(*storage.healthPercent)); else stmt.bindNull(12);
    if (storage.temperatureCelsius) stmt.bind(13, static_cast<long long>(*storage.temperatureCelsius));
    else stmt.bindNull(13);
    if (storage.healthAlertReason) stmt.bind(14, *storage.healthAlertReason); else stmt.bindNull(14);
    stmt.bind(15, storage.storageId);
    stmt.step();
}

void StorageRepository::remove(const std::string& storageId) {
    // No ON DELETE CASCADE on the GameVariants/GameVersions/GameFiles ->
    // Storages foreign keys (SQLite doesn't enforce FKs unless PRAGMA
    // foreign_keys is on, and this schema doesn't turn it on), so a bare
    // DELETE here would leave every game the removed storage ever indexed
    // as permanently orphaned - still shown in the library, with no way to
    // remove it since nothing points back to a storage that no longer
    // exists. Cascade it by hand instead.
    auto deleteCategoryMemberships = db_.prepare("DELETE FROM StorageCategoryMemberships WHERE StorageId = ?;");
    deleteCategoryMemberships.bind(1, storageId);
    deleteCategoryMemberships.step();

    auto deleteFiles = db_.prepare(
        "DELETE FROM GameFiles WHERE GameVersionId IN ("
        "SELECT GameVersionId FROM GameVersions WHERE GameVariantId IN ("
        "SELECT GameVariantId FROM GameVariants WHERE StorageId = ?));");
    deleteFiles.bind(1, storageId);
    deleteFiles.step();

    auto deleteVersions = db_.prepare(
        "DELETE FROM GameVersions WHERE GameVariantId IN ("
        "SELECT GameVariantId FROM GameVariants WHERE StorageId = ?);");
    deleteVersions.bind(1, storageId);
    deleteVersions.step();

    auto deleteVariants = db_.prepare("DELETE FROM GameVariants WHERE StorageId = ?;");
    deleteVariants.bind(1, storageId);
    deleteVariants.step();

    // A Game with no variants left (possibly from another storage too) is
    // dead weight - prune it rather than leaving a title with nothing under it.
    db_.execute("DELETE FROM Games WHERE GameId NOT IN (SELECT DISTINCT GameId FROM GameVariants);");

    auto deleteScanJobs = db_.prepare("DELETE FROM ScanJobs WHERE StorageId = ?;");
    deleteScanJobs.bind(1, storageId);
    deleteScanJobs.step();

    auto stmt = db_.prepare("DELETE FROM Storages WHERE StorageId = ?;");
    stmt.bind(1, storageId);
    stmt.step();
}

std::vector<core::StorageCategory> StorageRepository::getCategories() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectCategoryColumns +
                            " FROM StorageCategories ORDER BY Name COLLATE NOCASE, CategoryId;");
    std::vector<core::StorageCategory> results;
    while (stmt.step()) results.push_back(mapCategoryRow(stmt));
    return results;
}

std::optional<core::StorageCategory> StorageRepository::getCategoryById(const std::string& categoryId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectCategoryColumns +
                            " FROM StorageCategories WHERE CategoryId = ?;");
    stmt.bind(1, categoryId);
    if (!stmt.step()) return std::nullopt;
    return mapCategoryRow(stmt);
}

void StorageRepository::addCategory(const core::StorageCategory& category) {
    auto stmt = db_.prepare("INSERT INTO StorageCategories (CategoryId, Name, CreatedAt, UpdatedAt) "
                            "VALUES (?, ?, ?, ?);");
    stmt.bind(1, category.categoryId);
    stmt.bind(2, category.name);
    stmt.bind(3, category.createdAt);
    stmt.bind(4, category.updatedAt);
    stmt.step();
}

void StorageRepository::updateCategory(const core::StorageCategory& category) {
    auto stmt = db_.prepare("UPDATE StorageCategories SET Name = ?, UpdatedAt = ? WHERE CategoryId = ?;");
    stmt.bind(1, category.name);
    stmt.bind(2, category.updatedAt);
    stmt.bind(3, category.categoryId);
    stmt.step();
}

void StorageRepository::removeCategory(const std::string& categoryId) {
    auto deleteMemberships = db_.prepare("DELETE FROM StorageCategoryMemberships WHERE CategoryId = ?;");
    deleteMemberships.bind(1, categoryId);
    deleteMemberships.step();

    auto stmt = db_.prepare("DELETE FROM StorageCategories WHERE CategoryId = ?;");
    stmt.bind(1, categoryId);
    stmt.step();
}

std::vector<std::string> StorageRepository::getStorageIdsForCategory(const std::string& categoryId) {
    auto stmt = db_.prepare(
        "SELECT membership.StorageId FROM StorageCategoryMemberships AS membership "
        "JOIN Storages AS storage ON storage.StorageId = membership.StorageId "
        "WHERE membership.CategoryId = ? ORDER BY storage.Name COLLATE NOCASE, storage.StorageId;");
    stmt.bind(1, categoryId);
    std::vector<std::string> results;
    while (stmt.step()) results.push_back(stmt.columnText(0));
    return results;
}

void StorageRepository::addStorageToCategory(const std::string& categoryId, const std::string& storageId) {
    auto stmt = db_.prepare("INSERT OR IGNORE INTO StorageCategoryMemberships (CategoryId, StorageId) VALUES (?, ?);");
    stmt.bind(1, categoryId);
    stmt.bind(2, storageId);
    stmt.step();
}

void StorageRepository::removeStorageFromCategory(const std::string& categoryId, const std::string& storageId) {
    auto stmt = db_.prepare("DELETE FROM StorageCategoryMemberships WHERE CategoryId = ? AND StorageId = ?;");
    stmt.bind(1, categoryId);
    stmt.bind(2, storageId);
    stmt.step();
}

}  // namespace gth::database
