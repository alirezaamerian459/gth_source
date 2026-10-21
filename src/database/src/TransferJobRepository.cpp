#include "gth/database/TransferJobRepository.h"

namespace gth::database {

using core::TransferJob;

namespace {
constexpr const char* kSelectColumns = "TransferJobId, Name, Status, CreatedAt, UpdatedAt, CompletedAt";

TransferJob mapRow(const Statement& stmt) {
    TransferJob job;
    job.transferJobId = stmt.columnText(0);
    job.name = stmt.columnText(1);
    job.status = core::transferStatusFromString(stmt.columnText(2));
    job.createdAt = stmt.columnText(3);
    job.updatedAt = stmt.columnText(4);
    job.completedAt = stmt.columnTextOrNull(5);
    return job;
}
}  // namespace

TransferJobRepository::TransferJobRepository(Database& db) : db_(db) {}

std::vector<TransferJob> TransferJobRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM TransferJobs ORDER BY CreatedAt DESC;");
    std::vector<TransferJob> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<TransferJob> TransferJobRepository::getById(const std::string& transferJobId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM TransferJobs WHERE TransferJobId = ?;");
    stmt.bind(1, transferJobId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void TransferJobRepository::add(const TransferJob& job) {
    auto stmt = db_.prepare(
        "INSERT INTO TransferJobs (TransferJobId, Name, Status, CreatedAt, UpdatedAt, CompletedAt) "
        "VALUES (?, ?, ?, ?, ?, ?);");
    stmt.bind(1, job.transferJobId);
    stmt.bind(2, job.name);
    stmt.bind(3, core::toString(job.status));
    stmt.bind(4, job.createdAt);
    stmt.bind(5, job.updatedAt);
    if (job.completedAt) stmt.bind(6, *job.completedAt); else stmt.bindNull(6);
    stmt.step();
}

void TransferJobRepository::update(const TransferJob& job) {
    auto stmt =
        db_.prepare("UPDATE TransferJobs SET Name = ?, Status = ?, UpdatedAt = ?, CompletedAt = ? WHERE TransferJobId = ?;");
    stmt.bind(1, job.name);
    stmt.bind(2, core::toString(job.status));
    stmt.bind(3, job.updatedAt);
    if (job.completedAt) stmt.bind(4, *job.completedAt); else stmt.bindNull(4);
    stmt.bind(5, job.transferJobId);
    stmt.step();
}

}  // namespace gth::database
