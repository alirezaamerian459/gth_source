# Database

GTH uses SQLite. The migration source of truth is src/database/src/Migrator.cpp. Migrations are ordered by version and tracked with SQLite's PRAGMA user_version. The migration list currently contains versions 1 through 16; inspect the code before changing schema or relying on a particular version in user data.

The migration runner enables WAL mode and applies each migration newer than the database's current user_version. Add a new migration with the next version. Never edit or renumber a migration that may already have shipped.

## Tables by area

- Storage and indexing: Storages, Games, GameVariants, GameVersions, GameFiles, ScanJobs.
- Updates: UpdateInfos, UpdateJobs, UpdateHistories.
- Transfers and consoles: TransferJobs, TransferItems, Consoles.
- Remote health: RemoteNodes, NasDevices, Disks, HealthSnapshots, TemperatureSnapshots.
- Connections and settings: FtpConnections, AppSettings.
- Network lookup and URL capture: CdnMirrors, CdnUrls.

Column definitions, indexes, foreign keys, and defaults are defined in the migration SQL, not duplicated here.

## Sensitive data

FtpConnections stores a CredentialTarget reference, not the credential itself. Keep secrets in the configured credential store and out of SQLite, logs, documentation, and test fixtures. Treat database backups and archive metadata as user data.

Migration 16 adds SteamGridDbApiKey to AppSettings. The migration comment says the application no longer reads or writes it. Shipped migrations are immutable, so cleanup must not remove that historical column by rewriting the migration.

## Database location

The current default path is managed by ServiceContainer and is documented as %LOCALAPPDATA%/GameTransferHub/gth.db. Inspect the implementation when changing path selection or supporting a user-supplied database.
