#pragma once

#include <optional>
#include <string>

#include <sqlite3.h>

namespace gth::database {

// Thin RAII wrapper around a single prepared sqlite3_stmt. Bind indices are
// 1-based (SQLite convention); column indices are 0-based.
class Statement {
public:
    explicit Statement(sqlite3_stmt* stmt);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    void bind(int index, const std::string& value);
    void bind(int index, long long value);
    void bindNull(int index);

    // Returns true if a row is available (SQLITE_ROW), false at end (SQLITE_DONE).
    bool step();

    std::string columnText(int index) const;
    std::optional<std::string> columnTextOrNull(int index) const;
    long long columnInt64(int index) const;
    std::optional<long long> columnInt64OrNull(int index) const;
    bool columnBool(int index) const;

private:
    sqlite3_stmt* stmt_;
};

// Thin RAII wrapper around a single sqlite3 connection.
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Runs one or more ';'-separated statements with no parameters (schema
    // DDL, PRAGMAs). Use prepare() for anything that needs bound parameters.
    void execute(const std::string& sql);

    Statement prepare(const std::string& sql);

private:
    sqlite3* db_ = nullptr;
};

}  // namespace gth::database
