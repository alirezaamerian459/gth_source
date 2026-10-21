#pragma once

#include "gth/database/Database.h"

namespace gth::database {

// Applies versioned schema migrations, tracked via SQLite's built-in
// `PRAGMA user_version` - the closest native equivalent to an EF Core
// migrations history table without pulling in an ORM. Safe to call on every
// startup: migrations already applied (version <= current) are skipped.
class Migrator {
public:
    static void applyMigrations(Database& db);
};

}  // namespace gth::database
