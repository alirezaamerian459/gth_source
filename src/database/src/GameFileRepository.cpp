#include "gth/database/GameFileRepository.h"

namespace gth::database {

using core::GameFile;

namespace {
constexpr const char* kSelectColumns =
    "GameFileId, GameVersionId, RelativePath, SizeBytes, LastModified, CreatedAt, UpdatedAt";

GameFile mapRow(const Statement& stmt) {
    GameFile file;
    file.gameFileId = stmt.columnText(0);
    file.gameVersionId = stmt.columnText(1);
    file.relativePath = stmt.columnText(2);
    file.sizeBytes = stmt.columnInt64(3);
    file.lastModified = stmt.columnText(4);
    file.createdAt = stmt.columnText(5);
    file.updatedAt = stmt.columnText(6);
    return file;
}
}  // namespace

GameFileRepository::GameFileRepository(Database& db) : db_(db) {}

std::vector<GameFile> GameFileRepository::getByVersionId(const std::string& gameVersionId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM GameFiles WHERE GameVersionId = ?;");
    stmt.bind(1, gameVersionId);
    std::vector<GameFile> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

void GameFileRepository::add(const GameFile& file) {
    auto stmt = db_.prepare(
        "INSERT INTO GameFiles (GameFileId, GameVersionId, RelativePath, SizeBytes, LastModified, CreatedAt, "
        "UpdatedAt) VALUES (?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, file.gameFileId);
    stmt.bind(2, file.gameVersionId);
    stmt.bind(3, file.relativePath);
    stmt.bind(4, file.sizeBytes);
    stmt.bind(5, file.lastModified);
    stmt.bind(6, file.createdAt);
    stmt.bind(7, file.updatedAt);
    stmt.step();
}

void GameFileRepository::update(const GameFile& file) {
    auto stmt = db_.prepare(
        "UPDATE GameFiles SET SizeBytes = ?, LastModified = ?, UpdatedAt = ? WHERE GameFileId = ?;");
    stmt.bind(1, file.sizeBytes);
    stmt.bind(2, file.lastModified);
    stmt.bind(3, file.updatedAt);
    stmt.bind(4, file.gameFileId);
    stmt.step();
}

void GameFileRepository::remove(const std::string& gameFileId) {
    auto stmt = db_.prepare("DELETE FROM GameFiles WHERE GameFileId = ?;");
    stmt.bind(1, gameFileId);
    stmt.step();
}

}  // namespace gth::database
