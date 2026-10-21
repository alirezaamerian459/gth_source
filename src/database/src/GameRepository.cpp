#include "gth/database/GameRepository.h"

namespace gth::database {

using core::Game;

namespace {
constexpr const char* kSelectColumns = "GameId, Title, CreatedAt, UpdatedAt";

Game mapRow(const Statement& stmt) {
    Game game;
    game.gameId = stmt.columnText(0);
    game.title = stmt.columnText(1);
    game.createdAt = stmt.columnText(2);
    game.updatedAt = stmt.columnText(3);
    return game;
}
}  // namespace

GameRepository::GameRepository(Database& db) : db_(db) {}

std::vector<Game> GameRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM Games ORDER BY Title;");
    std::vector<Game> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<Game> GameRepository::getById(const std::string& gameId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM Games WHERE GameId = ?;");
    stmt.bind(1, gameId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

std::optional<Game> GameRepository::getByTitle(const std::string& title) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM Games WHERE Title = ?;");
    stmt.bind(1, title);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void GameRepository::add(const Game& game) {
    auto stmt = db_.prepare("INSERT INTO Games (GameId, Title, CreatedAt, UpdatedAt) VALUES (?, ?, ?, ?);");
    stmt.bind(1, game.gameId);
    stmt.bind(2, game.title);
    stmt.bind(3, game.createdAt);
    stmt.bind(4, game.updatedAt);
    stmt.step();
}

void GameRepository::update(const Game& game) {
    auto stmt = db_.prepare("UPDATE Games SET Title = ?, UpdatedAt = ? WHERE GameId = ?;");
    stmt.bind(1, game.title);
    stmt.bind(2, game.updatedAt);
    stmt.bind(3, game.gameId);
    stmt.step();
}

}  // namespace gth::database
