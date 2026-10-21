#include "gth/database/UpdateInfoRepository.h"

namespace gth::database {

using core::UpdateInfo;

namespace {
constexpr const char* kSelectColumns =
    "UpdateInfoId, GameVariantId, CurrentVersion, LatestVersion, UpdateSizeBytes, Source, DetectedAt, "
    "CheckedAt, Status, Metadata";

UpdateInfo mapRow(const Statement& stmt) {
    UpdateInfo info;
    info.updateInfoId = stmt.columnText(0);
    info.gameVariantId = stmt.columnText(1);
    info.currentVersion = stmt.columnText(2);
    info.latestVersion = stmt.columnText(3);
    info.updateSizeBytes = stmt.columnInt64(4);
    info.source = stmt.columnText(5);
    info.detectedAt = stmt.columnText(6);
    info.checkedAt = stmt.columnText(7);
    info.status = core::updateStatusFromString(stmt.columnText(8));
    info.metadata = stmt.columnTextOrNull(9);
    return info;
}
}  // namespace

UpdateInfoRepository::UpdateInfoRepository(Database& db) : db_(db) {}

std::vector<UpdateInfo> UpdateInfoRepository::getAll() {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM UpdateInfos;");
    std::vector<UpdateInfo> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<UpdateInfo> UpdateInfoRepository::getByVariantId(const std::string& gameVariantId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM UpdateInfos WHERE GameVariantId = ?;");
    stmt.bind(1, gameVariantId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

void UpdateInfoRepository::upsert(const UpdateInfo& info) {
    auto stmt = db_.prepare(
        "INSERT INTO UpdateInfos (UpdateInfoId, GameVariantId, CurrentVersion, LatestVersion, UpdateSizeBytes, "
        "Source, DetectedAt, CheckedAt, Status, Metadata) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(GameVariantId) DO UPDATE SET "
        "CurrentVersion = excluded.CurrentVersion, LatestVersion = excluded.LatestVersion, "
        "UpdateSizeBytes = excluded.UpdateSizeBytes, Source = excluded.Source, "
        "CheckedAt = excluded.CheckedAt, Status = excluded.Status, Metadata = excluded.Metadata;");
    stmt.bind(1, info.updateInfoId);
    stmt.bind(2, info.gameVariantId);
    stmt.bind(3, info.currentVersion);
    stmt.bind(4, info.latestVersion);
    stmt.bind(5, info.updateSizeBytes);
    stmt.bind(6, info.source);
    stmt.bind(7, info.detectedAt);
    stmt.bind(8, info.checkedAt);
    stmt.bind(9, core::toString(info.status));
    if (info.metadata) stmt.bind(10, *info.metadata); else stmt.bindNull(10);
    stmt.step();
}

}  // namespace gth::database
