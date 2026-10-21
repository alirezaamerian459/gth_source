#include "gth/database/GameVersionRepository.h"

namespace gth::database {

using core::GameVersion;

namespace {
constexpr const char* kSelectColumns =
    "GameVersionId, GameVariantId, Version, RelativePath, SizeBytes, Status, IsCurrent, LastScan, CreatedAt, "
    "UpdatedAt";

GameVersion mapRow(const Statement& stmt) {
    GameVersion version;
    version.gameVersionId = stmt.columnText(0);
    version.gameVariantId = stmt.columnText(1);
    version.version = stmt.columnText(2);
    version.relativePath = stmt.columnText(3);
    version.sizeBytes = stmt.columnInt64(4);
    version.status = core::gameVariantStatusFromString(stmt.columnText(5));
    version.isCurrent = stmt.columnBool(6);
    version.lastScan = stmt.columnTextOrNull(7);
    version.createdAt = stmt.columnText(8);
    version.updatedAt = stmt.columnText(9);
    return version;
}
}  // namespace

GameVersionRepository::GameVersionRepository(Database& db) : db_(db) {}

std::vector<GameVersion> GameVersionRepository::getByVariantId(const std::string& gameVariantId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM GameVersions WHERE GameVariantId = ?;");
    stmt.bind(1, gameVariantId);
    std::vector<GameVersion> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<GameVersion> GameVersionRepository::getById(const std::string& gameVersionId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM GameVersions WHERE GameVersionId = ?;");
    stmt.bind(1, gameVersionId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

std::optional<GameVersion> GameVersionRepository::getByVariantAndPath(const std::string& gameVariantId,
                                                                        const std::string& relativePath) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns +
                             " FROM GameVersions WHERE GameVariantId = ? AND RelativePath = ?;");
    stmt.bind(1, gameVariantId);
    stmt.bind(2, relativePath);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void GameVersionRepository::add(const GameVersion& version) {
    auto stmt = db_.prepare(
        "INSERT INTO GameVersions (GameVersionId, GameVariantId, Version, RelativePath, SizeBytes, Status, "
        "IsCurrent, LastScan, CreatedAt, UpdatedAt) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, version.gameVersionId);
    stmt.bind(2, version.gameVariantId);
    stmt.bind(3, version.version);
    stmt.bind(4, version.relativePath);
    stmt.bind(5, version.sizeBytes);
    stmt.bind(6, core::toString(version.status));
    stmt.bind(7, static_cast<long long>(version.isCurrent ? 1 : 0));
    if (version.lastScan) stmt.bind(8, *version.lastScan); else stmt.bindNull(8);
    stmt.bind(9, version.createdAt);
    stmt.bind(10, version.updatedAt);
    stmt.step();
}

void GameVersionRepository::update(const GameVersion& version) {
    auto stmt = db_.prepare(
        "UPDATE GameVersions SET Version = ?, SizeBytes = ?, Status = ?, IsCurrent = ?, LastScan = ?, UpdatedAt = ? "
        "WHERE GameVersionId = ?;");
    stmt.bind(1, version.version);
    stmt.bind(2, version.sizeBytes);
    stmt.bind(3, core::toString(version.status));
    stmt.bind(4, static_cast<long long>(version.isCurrent ? 1 : 0));
    if (version.lastScan) stmt.bind(5, *version.lastScan); else stmt.bindNull(5);
    stmt.bind(6, version.updatedAt);
    stmt.bind(7, version.gameVersionId);
    stmt.step();
}

}  // namespace gth::database
