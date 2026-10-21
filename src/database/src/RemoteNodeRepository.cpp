#include "gth/database/RemoteNodeRepository.h"

namespace gth::database {

using core::RemoteNode;

namespace {
constexpr const char* kSelectColumns = "NodeId, Name, Address, LastSeenAt, LastReportSummary, CreatedAt, UpdatedAt";

RemoteNode mapRow(const Statement& stmt) {
    RemoteNode node;
    node.nodeId = stmt.columnText(0);
    node.name = stmt.columnText(1);
    node.address = stmt.columnText(2);
    node.lastSeenAt = stmt.columnTextOrNull(3);
    node.lastReportSummary = stmt.columnTextOrNull(4);
    node.createdAt = stmt.columnText(5);
    node.updatedAt = stmt.columnText(6);
    return node;
}
}  // namespace

RemoteNodeRepository::RemoteNodeRepository(Database& db) : db_(db) {}

std::vector<RemoteNode> RemoteNodeRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM RemoteNodes ORDER BY UpdatedAt DESC;");
    std::vector<RemoteNode> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<RemoteNode> RemoteNodeRepository::getById(const std::string& nodeId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM RemoteNodes WHERE NodeId = ?;");
    stmt.bind(1, nodeId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void RemoteNodeRepository::upsert(const RemoteNode& node) {
    auto stmt = db_.prepare(
        "INSERT INTO RemoteNodes (NodeId, Name, Address, LastSeenAt, LastReportSummary, CreatedAt, UpdatedAt) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(NodeId) DO UPDATE SET "
        "Name = excluded.Name, Address = excluded.Address, LastSeenAt = excluded.LastSeenAt, "
        "LastReportSummary = excluded.LastReportSummary, UpdatedAt = excluded.UpdatedAt;");
    stmt.bind(1, node.nodeId);
    stmt.bind(2, node.name);
    stmt.bind(3, node.address);
    if (node.lastSeenAt) stmt.bind(4, *node.lastSeenAt); else stmt.bindNull(4);
    if (node.lastReportSummary) stmt.bind(5, *node.lastReportSummary); else stmt.bindNull(5);
    stmt.bind(6, node.createdAt);
    stmt.bind(7, node.updatedAt);
    stmt.step();
}

}  // namespace gth::database
