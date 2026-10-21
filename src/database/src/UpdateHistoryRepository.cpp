#include "gth/database/UpdateHistoryRepository.h"

namespace gth::database {

using core::UpdateHistory;

namespace {
constexpr const char* kSelectColumns =
    "UpdateHistoryId, GameVariantId, PreviousVersion, NewVersion, StartedAt, CompletedAt, DownloadSizeBytes, "
    "VerificationStatus, Result, ErrorMessage";

UpdateHistory mapRow(const Statement& stmt) {
    UpdateHistory history;
    history.updateHistoryId = stmt.columnText(0);
    history.gameVariantId = stmt.columnText(1);
    history.previousVersion = stmt.columnText(2);
    history.newVersion = stmt.columnText(3);
    history.startedAt = stmt.columnText(4);
    history.completedAt = stmt.columnTextOrNull(5);
    history.downloadSizeBytes = stmt.columnInt64(6);
    history.verificationStatus = stmt.columnText(7);
    history.result = stmt.columnText(8);
    history.errorMessage = stmt.columnTextOrNull(9);
    return history;
}
}  // namespace

UpdateHistoryRepository::UpdateHistoryRepository(Database& db) : db_(db) {}

std::vector<UpdateHistory> UpdateHistoryRepository::getByVariantId(const std::string& gameVariantId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns +
                             " FROM UpdateHistories WHERE GameVariantId = ? ORDER BY StartedAt DESC;");
    stmt.bind(1, gameVariantId);
    std::vector<UpdateHistory> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

void UpdateHistoryRepository::add(const UpdateHistory& history) {
    auto stmt = db_.prepare(
        "INSERT INTO UpdateHistories (UpdateHistoryId, GameVariantId, PreviousVersion, NewVersion, StartedAt, "
        "CompletedAt, DownloadSizeBytes, VerificationStatus, Result, ErrorMessage) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, history.updateHistoryId);
    stmt.bind(2, history.gameVariantId);
    stmt.bind(3, history.previousVersion);
    stmt.bind(4, history.newVersion);
    stmt.bind(5, history.startedAt);
    if (history.completedAt) stmt.bind(6, *history.completedAt); else stmt.bindNull(6);
    stmt.bind(7, history.downloadSizeBytes);
    stmt.bind(8, history.verificationStatus);
    stmt.bind(9, history.result);
    if (history.errorMessage) stmt.bind(10, *history.errorMessage); else stmt.bindNull(10);
    stmt.step();
}

}  // namespace gth::database
