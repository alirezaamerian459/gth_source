#include "gth/database/Database.h"

#include <stdexcept>

namespace gth::database {

Statement::Statement(sqlite3_stmt* stmt) : stmt_(stmt) {}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : stmt_(other.stmt_) { other.stmt_ = nullptr; }

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_) sqlite3_finalize(stmt_);
        stmt_ = other.stmt_;
        other.stmt_ = nullptr;
    }
    return *this;
}

void Statement::bind(int index, const std::string& value) {
    sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

void Statement::bind(int index, long long value) { sqlite3_bind_int64(stmt_, index, value); }

void Statement::bindNull(int index) { sqlite3_bind_null(stmt_, index); }

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw std::runtime_error(std::string("sqlite3_step failed: ") + sqlite3_errstr(rc));
}

std::string Statement::columnText(int index) const {
    const auto* text = sqlite3_column_text(stmt_, index);
    return text ? reinterpret_cast<const char*>(text) : std::string();
}

std::optional<std::string> Statement::columnTextOrNull(int index) const {
    if (sqlite3_column_type(stmt_, index) == SQLITE_NULL) return std::nullopt;
    return columnText(index);
}

long long Statement::columnInt64(int index) const { return sqlite3_column_int64(stmt_, index); }

std::optional<long long> Statement::columnInt64OrNull(int index) const {
    if (sqlite3_column_type(stmt_, index) == SQLITE_NULL) return std::nullopt;
    return columnInt64(index);
}

bool Statement::columnBool(int index) const { return sqlite3_column_int(stmt_, index) != 0; }

Database::Database(const std::string& path) {
    const int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        const std::string message = db_ ? sqlite3_errmsg(db_) : "unknown error";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to open database '" + path + "': " + message);
    }
    // Without this, a write from one thread while another holds the write
    // lock (e.g. two proxy connections' captureUrl() calls landing at the
    // same moment -- consoles commonly open several parallel connections
    // per download) fails immediately with SQLITE_BUSY instead of waiting,
    // which Statement::step() turns into a thrown exception. Callers that
    // catch(...) around a capture call (HttpProxyServer::handleClient does,
    // deliberately, so a logging failure can't kill the actual download)
    // would silently lose the URL instead of it ever reaching the database.
    sqlite3_busy_timeout(db_, 5000);
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::execute(const std::string& sql) {
    char* errMsg = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        const std::string message = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw std::runtime_error("SQL execute failed: " + message);
    }
}

Statement Database::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    return Statement(stmt);
}

}  // namespace gth::database
