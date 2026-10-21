#include "gth/database/HealthSnapshotRepository.h"

namespace gth::database {

using core::health::HealthSnapshot;

namespace {
constexpr const char* kSelectColumns = "HealthSnapshotId, DiskId, HealthPercent, Source, Reason, CapturedAt";

HealthSnapshot mapRow(const Statement& stmt) {
    HealthSnapshot snapshot;
    snapshot.healthSnapshotId = stmt.columnText(0);
    snapshot.diskId = stmt.columnText(1);
    if (auto v = stmt.columnInt64OrNull(2)) snapshot.healthPercent = static_cast<int>(*v);
    snapshot.source = stmt.columnText(3);
    snapshot.reason = stmt.columnTextOrNull(4);
    snapshot.capturedAt = stmt.columnText(5);
    return snapshot;
}
}  // namespace

HealthSnapshotRepository::HealthSnapshotRepository(Database& db) : db_(db) {}

void HealthSnapshotRepository::add(const HealthSnapshot& snapshot) {
    auto stmt = db_.prepare(
        "INSERT INTO HealthSnapshots (HealthSnapshotId, DiskId, HealthPercent, Source, Reason, CapturedAt) "
        "VALUES (?, ?, ?, ?, ?, ?);");
    stmt.bind(1, snapshot.healthSnapshotId);
    stmt.bind(2, snapshot.diskId);
    if (snapshot.healthPercent) stmt.bind(3, static_cast<long long>(*snapshot.healthPercent)); else stmt.bindNull(3);
    stmt.bind(4, snapshot.source);
    if (snapshot.reason) stmt.bind(5, *snapshot.reason); else stmt.bindNull(5);
    stmt.bind(6, snapshot.capturedAt);
    stmt.step();
}

std::vector<HealthSnapshot> HealthSnapshotRepository::getByDiskId(const std::string& diskId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns +
                             " FROM HealthSnapshots WHERE DiskId = ? ORDER BY CapturedAt;");
    stmt.bind(1, diskId);
    std::vector<HealthSnapshot> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

}  // namespace gth::database
