#include "gth/database/NasDeviceRepository.h"

namespace gth::database {

using core::health::NasDevice;

namespace {
constexpr const char* kSelectColumns = "NasDeviceId, Name, IpAddress, Port, Status, LastSeen, CreatedAt, UpdatedAt";

NasDevice mapRow(const Statement& stmt) {
    NasDevice device;
    device.nasDeviceId = stmt.columnText(0);
    device.name = stmt.columnText(1);
    device.ipAddress = stmt.columnText(2);
    device.port = static_cast<int>(stmt.columnInt64(3));
    device.status = core::nasStatusFromString(stmt.columnText(4));
    device.lastSeen = stmt.columnTextOrNull(5);
    device.createdAt = stmt.columnText(6);
    device.updatedAt = stmt.columnText(7);
    return device;
}
}  // namespace

NasDeviceRepository::NasDeviceRepository(Database& db) : db_(db) {}

std::vector<NasDevice> NasDeviceRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM NasDevices ORDER BY Name;");
    std::vector<NasDevice> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<NasDevice> NasDeviceRepository::getById(const std::string& nasDeviceId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM NasDevices WHERE NasDeviceId = ?;");
    stmt.bind(1, nasDeviceId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void NasDeviceRepository::add(const NasDevice& device) {
    auto stmt = db_.prepare(
        "INSERT INTO NasDevices (NasDeviceId, Name, IpAddress, Port, Status, LastSeen, CreatedAt, UpdatedAt) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, device.nasDeviceId);
    stmt.bind(2, device.name);
    stmt.bind(3, device.ipAddress);
    stmt.bind(4, static_cast<long long>(device.port));
    stmt.bind(5, core::toString(device.status));
    if (device.lastSeen) stmt.bind(6, *device.lastSeen); else stmt.bindNull(6);
    stmt.bind(7, device.createdAt);
    stmt.bind(8, device.updatedAt);
    stmt.step();
}

void NasDeviceRepository::update(const NasDevice& device) {
    auto stmt = db_.prepare(
        "UPDATE NasDevices SET Name = ?, IpAddress = ?, Port = ?, Status = ?, LastSeen = ?, UpdatedAt = ? "
        "WHERE NasDeviceId = ?;");
    stmt.bind(1, device.name);
    stmt.bind(2, device.ipAddress);
    stmt.bind(3, static_cast<long long>(device.port));
    stmt.bind(4, core::toString(device.status));
    if (device.lastSeen) stmt.bind(5, *device.lastSeen); else stmt.bindNull(5);
    stmt.bind(6, device.updatedAt);
    stmt.bind(7, device.nasDeviceId);
    stmt.step();
}

void NasDeviceRepository::remove(const std::string& nasDeviceId) {
    auto stmt = db_.prepare("DELETE FROM NasDevices WHERE NasDeviceId = ?;");
    stmt.bind(1, nasDeviceId);
    stmt.step();
}

}  // namespace gth::database
