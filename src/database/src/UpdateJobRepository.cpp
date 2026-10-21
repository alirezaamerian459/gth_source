#include "gth/database/UpdateJobRepository.h"

namespace gth::database {

using core::UpdateJob;

namespace {
constexpr const char* kSelectColumns =
    "UpdateJobId, GameVariantId, OldVersion, NewVersion, Status, CreatedAt, UpdatedAt, ErrorMessage";

UpdateJob mapRow(const Statement& stmt) {
    UpdateJob job;
    job.updateJobId = stmt.columnText(0);
    job.gameVariantId = stmt.columnText(1);
    job.oldVersion = stmt.columnText(2);
    job.newVersion = stmt.columnText(3);
    job.status = core::updateJobStatusFromString(stmt.columnText(4));
    job.createdAt = stmt.columnText(5);
    job.updatedAt = stmt.columnText(6);
    job.errorMessage = stmt.columnTextOrNull(7);
    return job;
}
}  // namespace

UpdateJobRepository::UpdateJobRepository(Database& db) : db_(db) {}

std::vector<UpdateJob> UpdateJobRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM UpdateJobs ORDER BY CreatedAt DESC;");
    std::vector<UpdateJob> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<UpdateJob> UpdateJobRepository::getById(const std::string& updateJobId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM UpdateJobs WHERE UpdateJobId = ?;");
    stmt.bind(1, updateJobId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

std::vector<UpdateJob> UpdateJobRepository::getByVariantId(const std::string& gameVariantId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns +
                             " FROM UpdateJobs WHERE GameVariantId = ? ORDER BY CreatedAt DESC;");
    stmt.bind(1, gameVariantId);
    std::vector<UpdateJob> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<UpdateJob> UpdateJobRepository::getActiveJobForVariant(const std::string& gameVariantId,
                                                                       const std::string& newVersion) {
    auto stmt = db_.prepare(
        std::string("SELECT ") + kSelectColumns +
        " FROM UpdateJobs WHERE GameVariantId = ? AND NewVersion = ? "
        "AND Status NOT IN ('Completed', 'Failed', 'Cancelled', 'RolledBack') LIMIT 1;");
    stmt.bind(1, gameVariantId);
    stmt.bind(2, newVersion);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void UpdateJobRepository::add(const UpdateJob& job) {
    auto stmt = db_.prepare(
        "INSERT INTO UpdateJobs (UpdateJobId, GameVariantId, OldVersion, NewVersion, Status, CreatedAt, "
        "UpdatedAt, ErrorMessage) VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, job.updateJobId);
    stmt.bind(2, job.gameVariantId);
    stmt.bind(3, job.oldVersion);
    stmt.bind(4, job.newVersion);
    stmt.bind(5, core::toString(job.status));
    stmt.bind(6, job.createdAt);
    stmt.bind(7, job.updatedAt);
    if (job.errorMessage) stmt.bind(8, *job.errorMessage); else stmt.bindNull(8);
    stmt.step();
}

void UpdateJobRepository::update(const UpdateJob& job) {
    auto stmt = db_.prepare("UPDATE UpdateJobs SET Status = ?, UpdatedAt = ?, ErrorMessage = ? WHERE UpdateJobId = ?;");
    stmt.bind(1, core::toString(job.status));
    stmt.bind(2, job.updatedAt);
    if (job.errorMessage) stmt.bind(3, *job.errorMessage); else stmt.bindNull(3);
    stmt.bind(4, job.updateJobId);
    stmt.step();
}

}  // namespace gth::database
