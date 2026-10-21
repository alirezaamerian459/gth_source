#include "gth/database/ConsoleRepository.h"

namespace gth::database {

using core::Console;

namespace {
constexpr const char* kSelectColumns =
    "ConsoleId, Name, Platform, IpAddress, Port, Protocol, Status, LastConnected, CreatedAt, UpdatedAt, AutoDetected";

Console mapRow(const Statement& stmt) {
    Console console;
    console.consoleId = stmt.columnText(0);
    console.name = stmt.columnText(1);
    console.platform = stmt.columnText(2);
    console.ipAddress = stmt.columnText(3);
    console.port = static_cast<int>(stmt.columnInt64(4));
    console.protocol = core::consoleProtocolFromString(stmt.columnText(5));
    console.status = core::consoleStatusFromString(stmt.columnText(6));
    console.lastConnected = stmt.columnTextOrNull(7);
    console.createdAt = stmt.columnText(8);
    console.updatedAt = stmt.columnText(9);
    console.autoDetected = stmt.columnInt64(10) != 0;
    return console;
}
}  // namespace

ConsoleRepository::ConsoleRepository(Database& db) : db_(db) {}

std::vector<Console> ConsoleRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM Consoles ORDER BY Name;");
    std::vector<Console> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<Console> ConsoleRepository::getById(const std::string& consoleId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM Consoles WHERE ConsoleId = ?;");
    stmt.bind(1, consoleId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

std::optional<Console> ConsoleRepository::getByIpAddress(const std::string& ipAddress) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM Consoles WHERE IpAddress = ? LIMIT 1;");
    stmt.bind(1, ipAddress);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void ConsoleRepository::add(const Console& console) {
    auto stmt = db_.prepare(
        "INSERT INTO Consoles (ConsoleId, Name, Platform, IpAddress, Port, Protocol, Status, LastConnected, "
        "CreatedAt, UpdatedAt, AutoDetected) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, console.consoleId);
    stmt.bind(2, console.name);
    stmt.bind(3, console.platform);
    stmt.bind(4, console.ipAddress);
    stmt.bind(5, static_cast<long long>(console.port));
    stmt.bind(6, core::toString(console.protocol));
    stmt.bind(7, core::toString(console.status));
    if (console.lastConnected) stmt.bind(8, *console.lastConnected); else stmt.bindNull(8);
    stmt.bind(9, console.createdAt);
    stmt.bind(10, console.updatedAt);
    stmt.bind(11, static_cast<long long>(console.autoDetected ? 1 : 0));
    stmt.step();
}

void ConsoleRepository::update(const Console& console) {
    auto stmt = db_.prepare(
        "UPDATE Consoles SET Name = ?, Platform = ?, IpAddress = ?, Port = ?, Protocol = ?, Status = ?, "
        "LastConnected = ?, UpdatedAt = ?, AutoDetected = ? WHERE ConsoleId = ?;");
    stmt.bind(1, console.name);
    stmt.bind(2, console.platform);
    stmt.bind(3, console.ipAddress);
    stmt.bind(4, static_cast<long long>(console.port));
    stmt.bind(5, core::toString(console.protocol));
    stmt.bind(6, core::toString(console.status));
    if (console.lastConnected) stmt.bind(7, *console.lastConnected); else stmt.bindNull(7);
    stmt.bind(8, console.updatedAt);
    stmt.bind(9, static_cast<long long>(console.autoDetected ? 1 : 0));
    stmt.bind(10, console.consoleId);
    stmt.step();
}

void ConsoleRepository::remove(const std::string& consoleId) {
    auto stmt = db_.prepare("DELETE FROM Consoles WHERE ConsoleId = ?;");
    stmt.bind(1, consoleId);
    stmt.step();
}

}  // namespace gth::database
