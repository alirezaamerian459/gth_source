#include "gth/database/CdnMirrorRepository.h"

namespace gth::database {

using core::CdnMirror;

namespace {
constexpr const char* kSelectColumns = "CdnMirrorId, HostPattern, Ip, Label, LastRttMs, CreatedAt";

CdnMirror mapRow(const Statement& stmt) {
    CdnMirror mirror;
    mirror.mirrorId = stmt.columnText(0);
    mirror.hostPattern = stmt.columnText(1);
    mirror.ip = stmt.columnText(2);
    mirror.label = stmt.columnText(3);
    const auto rtt = stmt.columnInt64OrNull(4);
    mirror.lastRttMs = rtt ? std::optional<int>(static_cast<int>(*rtt)) : std::nullopt;
    mirror.createdAt = stmt.columnText(5);
    return mirror;
}
}  // namespace

CdnMirrorRepository::CdnMirrorRepository(Database& db) : db_(db) {}

std::vector<CdnMirror> CdnMirrorRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM CdnMirrors ORDER BY HostPattern;");
    std::vector<CdnMirror> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<CdnMirror> CdnMirrorRepository::getById(const std::string& mirrorId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM CdnMirrors WHERE CdnMirrorId = ?;");
    stmt.bind(1, mirrorId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void CdnMirrorRepository::add(const CdnMirror& mirror) {
    auto stmt = db_.prepare(
        "INSERT INTO CdnMirrors (CdnMirrorId, HostPattern, Ip, Label, LastRttMs, CreatedAt) "
        "VALUES (?, ?, ?, ?, ?, ?);");
    stmt.bind(1, mirror.mirrorId);
    stmt.bind(2, mirror.hostPattern);
    stmt.bind(3, mirror.ip);
    stmt.bind(4, mirror.label);
    if (mirror.lastRttMs) {
        stmt.bind(5, static_cast<long long>(*mirror.lastRttMs));
    } else {
        stmt.bindNull(5);
    }
    stmt.bind(6, mirror.createdAt);
    stmt.step();
}

void CdnMirrorRepository::updateRtt(const std::string& mirrorId, std::optional<int> rttMs) {
    auto stmt = db_.prepare("UPDATE CdnMirrors SET LastRttMs = ? WHERE CdnMirrorId = ?;");
    if (rttMs.has_value()) {
        stmt.bind(1, static_cast<long long>(*rttMs));
    } else {
        stmt.bindNull(1);
    }
    stmt.bind(2, mirrorId);
    stmt.step();
}

void CdnMirrorRepository::remove(const std::string& mirrorId) {
    auto stmt = db_.prepare("DELETE FROM CdnMirrors WHERE CdnMirrorId = ?;");
    stmt.bind(1, mirrorId);
    stmt.step();
}

}  // namespace gth::database
