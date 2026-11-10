#include "gth/database/TemperatureSnapshotRepository.h"

namespace gth::database {

using core::health::TemperatureSnapshot;

namespace {
constexpr const char* kSelectColumns = "TemperatureSnapshotId, DiskId, TemperatureCelsius, CapturedAt";

TemperatureSnapshot mapRow(const Statement& stmt) {
    TemperatureSnapshot snapshot;
    snapshot.temperatureSnapshotId = stmt.columnText(0);
    snapshot.diskId = stmt.columnText(1);
    if (auto v = stmt.columnInt64OrNull(2); v.has_value()) snapshot.temperatureCelsius = static_cast<int>(*v);
    snapshot.capturedAt = stmt.columnText(3);
    return snapshot;
}
}  // namespace

TemperatureSnapshotRepository::TemperatureSnapshotRepository(Database& db) : db_(db) {}

void TemperatureSnapshotRepository::add(const TemperatureSnapshot& snapshot) {
    auto stmt = db_.prepare(
        "INSERT INTO TemperatureSnapshots (TemperatureSnapshotId, DiskId, TemperatureCelsius, CapturedAt) "
        "VALUES (?, ?, ?, ?);");
    stmt.bind(1, snapshot.temperatureSnapshotId);
    stmt.bind(2, snapshot.diskId);
    if (snapshot.temperatureCelsius) stmt.bind(3, static_cast<long long>(*snapshot.temperatureCelsius));
    else stmt.bindNull(3);
    stmt.bind(4, snapshot.capturedAt);
    stmt.step();
}

std::vector<TemperatureSnapshot> TemperatureSnapshotRepository::getByDiskId(const std::string& diskId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns +
                             " FROM TemperatureSnapshots WHERE DiskId = ? ORDER BY CapturedAt;");
    stmt.bind(1, diskId);
    std::vector<TemperatureSnapshot> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

}  // namespace gth::database
