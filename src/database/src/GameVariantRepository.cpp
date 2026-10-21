#include "gth/database/GameVariantRepository.h"

namespace gth::database {

using core::GameVariant;

namespace {
constexpr const char* kSelectColumns =
    "GameVariantId, GameId, Platform, Region, StorageId, RelativePath, TitleId, ContentId, "
    "Status, LastScan, CreatedAt, UpdatedAt";

GameVariant mapRow(const Statement& stmt) {
    GameVariant variant;
    variant.gameVariantId = stmt.columnText(0);
    variant.gameId = stmt.columnText(1);
    variant.platform = stmt.columnText(2);
    variant.region = stmt.columnText(3);
    variant.storageId = stmt.columnText(4);
    variant.relativePath = stmt.columnText(5);
    variant.titleId = stmt.columnTextOrNull(6);
    variant.contentId = stmt.columnTextOrNull(7);
    variant.status = core::gameVariantStatusFromString(stmt.columnText(8));
    variant.lastScan = stmt.columnTextOrNull(9);
    variant.createdAt = stmt.columnText(10);
    variant.updatedAt = stmt.columnText(11);
    return variant;
}
}  // namespace

GameVariantRepository::GameVariantRepository(Database& db) : db_(db) {}

std::vector<GameVariant> GameVariantRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM GameVariants;");
    std::vector<GameVariant> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::vector<GameVariant> GameVariantRepository::getByGameId(const std::string& gameId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM GameVariants WHERE GameId = ?;");
    stmt.bind(1, gameId);
    std::vector<GameVariant> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::vector<GameVariant> GameVariantRepository::getByStorageId(const std::string& storageId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM GameVariants WHERE StorageId = ?;");
    stmt.bind(1, storageId);
    std::vector<GameVariant> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<GameVariant> GameVariantRepository::getById(const std::string& gameVariantId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM GameVariants WHERE GameVariantId = ?;");
    stmt.bind(1, gameVariantId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

std::optional<GameVariant> GameVariantRepository::getByStorageAndPath(const std::string& storageId,
                                                                        const std::string& relativePath) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns +
                             " FROM GameVariants WHERE StorageId = ? AND RelativePath = ?;");
    stmt.bind(1, storageId);
    stmt.bind(2, relativePath);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void GameVariantRepository::add(const GameVariant& variant) {
    auto stmt = db_.prepare(
        "INSERT INTO GameVariants (GameVariantId, GameId, Platform, Region, StorageId, RelativePath, "
        "TitleId, ContentId, Status, LastScan, CreatedAt, UpdatedAt) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, variant.gameVariantId);
    stmt.bind(2, variant.gameId);
    stmt.bind(3, variant.platform);
    stmt.bind(4, variant.region);
    stmt.bind(5, variant.storageId);
    stmt.bind(6, variant.relativePath);
    if (variant.titleId) stmt.bind(7, *variant.titleId); else stmt.bindNull(7);
    if (variant.contentId) stmt.bind(8, *variant.contentId); else stmt.bindNull(8);
    stmt.bind(9, core::toString(variant.status));
    if (variant.lastScan) stmt.bind(10, *variant.lastScan); else stmt.bindNull(10);
    stmt.bind(11, variant.createdAt);
    stmt.bind(12, variant.updatedAt);
    stmt.step();
}

void GameVariantRepository::update(const GameVariant& variant) {
    auto stmt = db_.prepare(
        "UPDATE GameVariants SET GameId = ?, Platform = ?, Region = ?, RelativePath = ?, "
        "TitleId = ?, ContentId = ?, Status = ?, LastScan = ?, UpdatedAt = ? WHERE GameVariantId = ?;");
    stmt.bind(1, variant.gameId);
    stmt.bind(2, variant.platform);
    stmt.bind(3, variant.region);
    stmt.bind(4, variant.relativePath);
    if (variant.titleId) stmt.bind(5, *variant.titleId); else stmt.bindNull(5);
    if (variant.contentId) stmt.bind(6, *variant.contentId); else stmt.bindNull(6);
    stmt.bind(7, core::toString(variant.status));
    if (variant.lastScan) stmt.bind(8, *variant.lastScan); else stmt.bindNull(8);
    stmt.bind(9, variant.updatedAt);
    stmt.bind(10, variant.gameVariantId);
    stmt.step();
}

}  // namespace gth::database
