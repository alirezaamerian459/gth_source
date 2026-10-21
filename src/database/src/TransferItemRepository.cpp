#include "gth/database/TransferItemRepository.h"

namespace gth::database {

using core::TransferItem;

namespace {
constexpr const char* kSelectColumns =
    "TransferItemId, TransferJobId, SourcePath, DestinationPath, SizeBytes, BytesTransferred, Status, "
    "ErrorMessage, CreatedAt, UpdatedAt";

TransferItem mapRow(const Statement& stmt) {
    TransferItem item;
    item.transferItemId = stmt.columnText(0);
    item.transferJobId = stmt.columnText(1);
    item.sourcePath = stmt.columnText(2);
    item.destinationPath = stmt.columnText(3);
    item.sizeBytes = stmt.columnInt64(4);
    item.bytesTransferred = stmt.columnInt64(5);
    item.status = core::transferStatusFromString(stmt.columnText(6));
    item.errorMessage = stmt.columnTextOrNull(7);
    item.createdAt = stmt.columnText(8);
    item.updatedAt = stmt.columnText(9);
    return item;
}
}  // namespace

TransferItemRepository::TransferItemRepository(Database& db) : db_(db) {}

std::vector<TransferItem> TransferItemRepository::getByJobId(const std::string& transferJobId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns +
                             " FROM TransferItems WHERE TransferJobId = ? ORDER BY CreatedAt;");
    stmt.bind(1, transferJobId);
    std::vector<TransferItem> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

std::optional<TransferItem> TransferItemRepository::getById(const std::string& transferItemId) {
    auto stmt = db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM TransferItems WHERE TransferItemId = ?;");
    stmt.bind(1, transferItemId);
    if (!stmt.step()) return std::nullopt;
    return mapRow(stmt);
}

std::vector<TransferItem> TransferItemRepository::getQueued() {
    auto stmt =
        db_.prepare(std::string("SELECT ") + kSelectColumns + " FROM TransferItems WHERE Status = 'Queued' ORDER BY CreatedAt;");
    std::vector<TransferItem> results;
    while (stmt.step()) results.push_back(mapRow(stmt));
    return results;
}

void TransferItemRepository::add(const TransferItem& item) {
    auto stmt = db_.prepare(
        "INSERT INTO TransferItems (TransferItemId, TransferJobId, SourcePath, DestinationPath, SizeBytes, "
        "BytesTransferred, Status, ErrorMessage, CreatedAt, UpdatedAt) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, item.transferItemId);
    stmt.bind(2, item.transferJobId);
    stmt.bind(3, item.sourcePath);
    stmt.bind(4, item.destinationPath);
    stmt.bind(5, item.sizeBytes);
    stmt.bind(6, item.bytesTransferred);
    stmt.bind(7, core::toString(item.status));
    if (item.errorMessage) stmt.bind(8, *item.errorMessage); else stmt.bindNull(8);
    stmt.bind(9, item.createdAt);
    stmt.bind(10, item.updatedAt);
    stmt.step();
}

void TransferItemRepository::update(const TransferItem& item) {
    auto stmt = db_.prepare(
        "UPDATE TransferItems SET BytesTransferred = ?, Status = ?, ErrorMessage = ?, UpdatedAt = ? "
        "WHERE TransferItemId = ?;");
    stmt.bind(1, item.bytesTransferred);
    stmt.bind(2, core::toString(item.status));
    if (item.errorMessage) stmt.bind(3, *item.errorMessage); else stmt.bindNull(3);
    stmt.bind(4, item.updatedAt);
    stmt.bind(5, item.transferItemId);
    stmt.step();
}

}  // namespace gth::database
