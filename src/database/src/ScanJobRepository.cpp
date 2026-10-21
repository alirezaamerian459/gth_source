#include "gth/database/ScanJobRepository.h"

namespace gth::database {

using core::ScanJob;

namespace {
constexpr const char* kSelectColumns =
    "ScanJobId, StorageId, Status, StartedAt, CompletedAt, GamesAdded, VariantsAdded, VersionsAdded, "
    "VersionsUpdated, VersionsMissing, VersionsUnchanged, ErrorMessage";

ScanJob mapRow(const Statement& stmt) {
    ScanJob job;
    job.scanJobId = stmt.columnText(0);
    job.storageId = stmt.columnText(1);
    job.status = core::scanJobStatusFromString(stmt.columnText(2));
    job.startedAt = stmt.columnText(3);
    job.completedAt = stmt.columnTextOrNull(4);
    job.gamesAdded = static_cast<int>(stmt.columnInt64(5));
    job.variantsAdded = static_cast<int>(stmt.columnInt64(6));
    job.versionsAdded = static_cast<int>(stmt.columnInt64(7));
    job.versionsUpdated = static_cast<int>(stmt.columnInt64(8));
    job.versionsMissing = static_cast<int>(stmt.columnInt64(9));
    job.versionsUnchanged = static_cast<int>(stmt.columnInt64(10));
    job.errorMessage = stmt.columnTextOrNull(11);
    return job;
}
}  // namespace

ScanJobRepository::ScanJobRepository(Database& db) : db_(db) {}

std::vector<ScanJob> ScanJobRepository::getByStorageId(const std::string& storageId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns +
                             " FROM ScanJobs WHERE StorageId = ? ORDER BY StartedAt DESC;");
    stmt.bind(1, storageId);
    std::vector<ScanJob> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<ScanJob> ScanJobRepository::getById(const std::string& scanJobId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM ScanJobs WHERE ScanJobId = ?;");
    stmt.bind(1, scanJobId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void ScanJobRepository::add(const ScanJob& job) {
    auto stmt = db_.prepare(
        "INSERT INTO ScanJobs (ScanJobId, StorageId, Status, StartedAt, CompletedAt, GamesAdded, VariantsAdded, "
        "VersionsAdded, VersionsUpdated, VersionsMissing, VersionsUnchanged, ErrorMessage) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, job.scanJobId);
    stmt.bind(2, job.storageId);
    stmt.bind(3, core::toString(job.status));
    stmt.bind(4, job.startedAt);
    if (job.completedAt) stmt.bind(5, *job.completedAt); else stmt.bindNull(5);
    stmt.bind(6, static_cast<long long>(job.gamesAdded));
    stmt.bind(7, static_cast<long long>(job.variantsAdded));
    stmt.bind(8, static_cast<long long>(job.versionsAdded));
    stmt.bind(9, static_cast<long long>(job.versionsUpdated));
    stmt.bind(10, static_cast<long long>(job.versionsMissing));
    stmt.bind(11, static_cast<long long>(job.versionsUnchanged));
    if (job.errorMessage) stmt.bind(12, *job.errorMessage); else stmt.bindNull(12);
    stmt.step();
}

void ScanJobRepository::update(const ScanJob& job) {
    auto stmt = db_.prepare(
        "UPDATE ScanJobs SET Status = ?, CompletedAt = ?, GamesAdded = ?, VariantsAdded = ?, VersionsAdded = ?, "
        "VersionsUpdated = ?, VersionsMissing = ?, VersionsUnchanged = ?, ErrorMessage = ? WHERE ScanJobId = ?;");
    stmt.bind(1, core::toString(job.status));
    if (job.completedAt) stmt.bind(2, *job.completedAt); else stmt.bindNull(2);
    stmt.bind(3, static_cast<long long>(job.gamesAdded));
    stmt.bind(4, static_cast<long long>(job.variantsAdded));
    stmt.bind(5, static_cast<long long>(job.versionsAdded));
    stmt.bind(6, static_cast<long long>(job.versionsUpdated));
    stmt.bind(7, static_cast<long long>(job.versionsMissing));
    stmt.bind(8, static_cast<long long>(job.versionsUnchanged));
    if (job.errorMessage) stmt.bind(9, *job.errorMessage); else stmt.bindNull(9);
    stmt.bind(10, job.scanJobId);
    stmt.step();
}

}  // namespace gth::database
