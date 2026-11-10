#include "gth/database/DiskRepository.h"

namespace gth::database {

using core::health::Disk;

namespace {
constexpr const char* kSelectColumns =
    "DiskId, NasDeviceId, SerialNumber, Model, Manufacturer, Firmware, CapacityBytes, InterfaceType, Slot, "
    "Status, HealthPercent, TemperatureCelsius, PowerOnHours, LastSeen, CreatedAt, UpdatedAt";

Disk mapRow(const Statement& stmt) {
    Disk disk;
    disk.diskId = stmt.columnText(0);
    disk.nasDeviceId = stmt.columnText(1);
    disk.serialNumber = stmt.columnTextOrNull(2);
    disk.model = stmt.columnTextOrNull(3);
    disk.manufacturer = stmt.columnTextOrNull(4);
    disk.firmware = stmt.columnTextOrNull(5);
    disk.capacityBytes = stmt.columnInt64OrNull(6);
    disk.interfaceType = stmt.columnTextOrNull(7);
    disk.slot = stmt.columnTextOrNull(8);
    disk.status = core::diskStatusFromString(stmt.columnText(9));
    if (auto v = stmt.columnInt64OrNull(10); v.has_value()) disk.healthPercent = static_cast<int>(*v);
    if (auto v = stmt.columnInt64OrNull(11); v.has_value()) disk.temperatureCelsius = static_cast<int>(*v);
    disk.powerOnHours = stmt.columnInt64OrNull(12);
    disk.lastSeen = stmt.columnTextOrNull(13);
    disk.createdAt = stmt.columnText(14);
    disk.updatedAt = stmt.columnText(15);
    return disk;
}

void bindOptionalText(Statement& stmt, int index, const std::optional<std::string>& value) {
    if (value.has_value()) stmt.bind(index, *value); else stmt.bindNull(index);
}

void bindOptionalInt(Statement& stmt, int index, const std::optional<long long>& value) {
    if (value.has_value()) stmt.bind(index, *value); else stmt.bindNull(index);
}
}  // namespace

DiskRepository::DiskRepository(Database& db) : db_(db) {}

std::vector<Disk> DiskRepository::getByNasDeviceId(const std::string& nasDeviceId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns +
                             " FROM Disks WHERE NasDeviceId = ? ORDER BY Slot, Model;");
    stmt.bind(1, nasDeviceId);
    std::vector<Disk> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::vector<Disk> DiskRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM Disks;");
    std::vector<Disk> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<Disk> DiskRepository::getById(const std::string& diskId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM Disks WHERE DiskId = ?;");
    stmt.bind(1, diskId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

std::optional<Disk> DiskRepository::getByNasDeviceAndSerial(const std::string& nasDeviceId,
                                                              const std::string& serialNumber) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns +
                             " FROM Disks WHERE NasDeviceId = ? AND SerialNumber = ?;");
    stmt.bind(1, nasDeviceId);
    stmt.bind(2, serialNumber);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void DiskRepository::add(const Disk& disk) {
    auto stmt = db_.prepare(
        "INSERT INTO Disks (DiskId, NasDeviceId, SerialNumber, Model, Manufacturer, Firmware, CapacityBytes, "
        "InterfaceType, Slot, Status, HealthPercent, TemperatureCelsius, PowerOnHours, LastSeen, CreatedAt, "
        "UpdatedAt) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, disk.diskId);
    stmt.bind(2, disk.nasDeviceId);
    bindOptionalText(stmt, 3, disk.serialNumber);
    bindOptionalText(stmt, 4, disk.model);
    bindOptionalText(stmt, 5, disk.manufacturer);
    bindOptionalText(stmt, 6, disk.firmware);
    bindOptionalInt(stmt, 7, disk.capacityBytes);
    bindOptionalText(stmt, 8, disk.interfaceType);
    bindOptionalText(stmt, 9, disk.slot);
    stmt.bind(10, core::toString(disk.status));
    bindOptionalInt(stmt, 11, disk.healthPercent ? std::optional<long long>(*disk.healthPercent) : std::nullopt);
    bindOptionalInt(stmt, 12,
                     disk.temperatureCelsius ? std::optional<long long>(*disk.temperatureCelsius) : std::nullopt);
    bindOptionalInt(stmt, 13, disk.powerOnHours);
    bindOptionalText(stmt, 14, disk.lastSeen);
    stmt.bind(15, disk.createdAt);
    stmt.bind(16, disk.updatedAt);
    stmt.step();
}

void DiskRepository::update(const Disk& disk) {
    auto stmt = db_.prepare(
        "UPDATE Disks SET SerialNumber = ?, Model = ?, Manufacturer = ?, Firmware = ?, CapacityBytes = ?, "
        "InterfaceType = ?, Slot = ?, Status = ?, HealthPercent = ?, TemperatureCelsius = ?, PowerOnHours = ?, "
        "LastSeen = ?, UpdatedAt = ? WHERE DiskId = ?;");
    bindOptionalText(stmt, 1, disk.serialNumber);
    bindOptionalText(stmt, 2, disk.model);
    bindOptionalText(stmt, 3, disk.manufacturer);
    bindOptionalText(stmt, 4, disk.firmware);
    bindOptionalInt(stmt, 5, disk.capacityBytes);
    bindOptionalText(stmt, 6, disk.interfaceType);
    bindOptionalText(stmt, 7, disk.slot);
    stmt.bind(8, core::toString(disk.status));
    bindOptionalInt(stmt, 9, disk.healthPercent ? std::optional<long long>(*disk.healthPercent) : std::nullopt);
    bindOptionalInt(stmt, 10,
                     disk.temperatureCelsius ? std::optional<long long>(*disk.temperatureCelsius) : std::nullopt);
    bindOptionalInt(stmt, 11, disk.powerOnHours);
    bindOptionalText(stmt, 12, disk.lastSeen);
    stmt.bind(13, disk.updatedAt);
    stmt.bind(14, disk.diskId);
    stmt.step();
}

}  // namespace gth::database
