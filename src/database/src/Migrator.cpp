#include "gth/database/Migrator.h"

#include <array>
#include <format>
#include <string>

namespace gth::database {

namespace {

struct Migration {
    int version;
    const char* sql;
};

// Mirrors the Milestone 1 schema: one Storages row per known storage
// location, RootPath unique so a rescan reconciles by path instead of
// creating duplicates, Type/Status indexed since those are the fields
// filtered/grouped on most often (dashboard summary, storage list).
constexpr std::array<Migration, 17> kMigrations{{
    {1, R"SQL(
        CREATE TABLE Storages (
            StorageId TEXT NOT NULL PRIMARY KEY,
            Name TEXT NOT NULL,
            Type TEXT NOT NULL,
            RootPath TEXT NOT NULL,
            TotalCapacityBytes INTEGER NOT NULL,
            FreeSpaceBytes INTEGER NOT NULL,
            UsedSpaceBytes INTEGER NOT NULL,
            Status TEXT NOT NULL,
            FileSystem TEXT NULL,
            LastScan TEXT NULL,
            IsAvailable INTEGER NOT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL
        );
        CREATE UNIQUE INDEX IX_Storages_RootPath ON Storages(RootPath);
        CREATE INDEX IX_Storages_Type ON Storages(Type);
        CREATE INDEX IX_Storages_Status ON Storages(Status);
    )SQL"},
    // Milestone 2: Game Indexer. Games group GameVariants (one per
    // platform/title/region container folder, stable across version
    // updates); each GameVariant owns its GameVersions (one per version
    // folder - multiple can coexist on disk, e.g. mid-update); each
    // GameVersion owns a flat list of GameFiles used for incremental-scan
    // change detection. ScanJobs record one row per indexing pass over a
    // Storage.
    {2, R"SQL(
        CREATE TABLE Games (
            GameId TEXT NOT NULL PRIMARY KEY,
            Title TEXT NOT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL
        );
        CREATE UNIQUE INDEX IX_Games_Title ON Games(Title);

        CREATE TABLE GameVariants (
            GameVariantId TEXT NOT NULL PRIMARY KEY,
            GameId TEXT NOT NULL REFERENCES Games(GameId),
            Platform TEXT NOT NULL,
            Region TEXT NOT NULL,
            StorageId TEXT NOT NULL REFERENCES Storages(StorageId),
            RelativePath TEXT NOT NULL,
            TitleId TEXT NULL,
            ContentId TEXT NULL,
            Status TEXT NOT NULL,
            LastScan TEXT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL
        );
        CREATE UNIQUE INDEX IX_GameVariants_Storage_Path ON GameVariants(StorageId, RelativePath);
        CREATE INDEX IX_GameVariants_GameId ON GameVariants(GameId);
        CREATE INDEX IX_GameVariants_Platform_Region ON GameVariants(Platform, Region);
        CREATE INDEX IX_GameVariants_Status ON GameVariants(Status);

        CREATE TABLE GameVersions (
            GameVersionId TEXT NOT NULL PRIMARY KEY,
            GameVariantId TEXT NOT NULL REFERENCES GameVariants(GameVariantId),
            Version TEXT NOT NULL,
            RelativePath TEXT NOT NULL,
            SizeBytes INTEGER NOT NULL,
            Status TEXT NOT NULL,
            IsCurrent INTEGER NOT NULL,
            LastScan TEXT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL
        );
        CREATE UNIQUE INDEX IX_GameVersions_Variant_Path ON GameVersions(GameVariantId, RelativePath);
        CREATE INDEX IX_GameVersions_VariantId ON GameVersions(GameVariantId);
        CREATE INDEX IX_GameVersions_Status ON GameVersions(Status);

        CREATE TABLE GameFiles (
            GameFileId TEXT NOT NULL PRIMARY KEY,
            GameVersionId TEXT NOT NULL REFERENCES GameVersions(GameVersionId),
            RelativePath TEXT NOT NULL,
            SizeBytes INTEGER NOT NULL,
            LastModified TEXT NOT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL
        );
        CREATE UNIQUE INDEX IX_GameFiles_Version_Path ON GameFiles(GameVersionId, RelativePath);

        CREATE TABLE ScanJobs (
            ScanJobId TEXT NOT NULL PRIMARY KEY,
            StorageId TEXT NOT NULL REFERENCES Storages(StorageId),
            Status TEXT NOT NULL,
            StartedAt TEXT NOT NULL,
            CompletedAt TEXT NULL,
            GamesAdded INTEGER NOT NULL,
            VariantsAdded INTEGER NOT NULL,
            VersionsAdded INTEGER NOT NULL,
            VersionsUpdated INTEGER NOT NULL,
            VersionsMissing INTEGER NOT NULL,
            VersionsUnchanged INTEGER NOT NULL,
            ErrorMessage TEXT NULL
        );
        CREATE INDEX IX_ScanJobs_StorageId ON ScanJobs(StorageId);
    )SQL"},
    // Milestone 4: Update Manager foundation. UpdateInfo is a snapshot (one
    // row per variant, overwritten on re-check); UpdateJob tracks an
    // in-progress/completed update operation through the spec's status flow;
    // UpdateHistory is the permanent, append-only record of what actually
    // happened.
    {3, R"SQL(
        CREATE TABLE UpdateInfos (
            UpdateInfoId TEXT NOT NULL PRIMARY KEY,
            GameVariantId TEXT NOT NULL REFERENCES GameVariants(GameVariantId),
            CurrentVersion TEXT NOT NULL,
            LatestVersion TEXT NOT NULL,
            UpdateSizeBytes INTEGER NOT NULL,
            Source TEXT NOT NULL,
            DetectedAt TEXT NOT NULL,
            CheckedAt TEXT NOT NULL,
            Status TEXT NOT NULL,
            Metadata TEXT NULL
        );
        CREATE UNIQUE INDEX IX_UpdateInfos_VariantId ON UpdateInfos(GameVariantId);
        CREATE INDEX IX_UpdateInfos_Status ON UpdateInfos(Status);

        CREATE TABLE UpdateJobs (
            UpdateJobId TEXT NOT NULL PRIMARY KEY,
            GameVariantId TEXT NOT NULL REFERENCES GameVariants(GameVariantId),
            OldVersion TEXT NOT NULL,
            NewVersion TEXT NOT NULL,
            Status TEXT NOT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL,
            ErrorMessage TEXT NULL
        );
        CREATE INDEX IX_UpdateJobs_VariantId ON UpdateJobs(GameVariantId);
        CREATE INDEX IX_UpdateJobs_Status ON UpdateJobs(Status);

        CREATE TABLE UpdateHistories (
            UpdateHistoryId TEXT NOT NULL PRIMARY KEY,
            GameVariantId TEXT NOT NULL REFERENCES GameVariants(GameVariantId),
            PreviousVersion TEXT NOT NULL,
            NewVersion TEXT NOT NULL,
            StartedAt TEXT NOT NULL,
            CompletedAt TEXT NULL,
            DownloadSizeBytes INTEGER NOT NULL,
            VerificationStatus TEXT NOT NULL,
            Result TEXT NOT NULL,
            ErrorMessage TEXT NULL
        );
        CREATE INDEX IX_UpdateHistories_VariantId ON UpdateHistories(GameVariantId);
    )SQL"},
    // Milestone 10: Console Manager. Manually-registered transfer
    // sources/destinations - never auto-discovered or connected to except
    // when the user explicitly asks (see IReachabilityChecker).
    {4, R"SQL(
        CREATE TABLE Consoles (
            ConsoleId TEXT NOT NULL PRIMARY KEY,
            Name TEXT NOT NULL,
            Platform TEXT NOT NULL,
            IpAddress TEXT NOT NULL,
            Port INTEGER NOT NULL,
            Protocol TEXT NOT NULL,
            Status TEXT NOT NULL,
            LastConnected TEXT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL
        );
    )SQL"},
    // Milestone 11: Transfer Engine. TransferJobs group TransferItems (one
    // per file copy); item Status mirrors the spec's transfer state model
    // exactly (Queued/Preparing/Transferring/Paused/Verifying/Completed/
    // Failed/Cancelled).
    {5, R"SQL(
        CREATE TABLE TransferJobs (
            TransferJobId TEXT NOT NULL PRIMARY KEY,
            Name TEXT NOT NULL,
            Status TEXT NOT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL,
            CompletedAt TEXT NULL
        );

        CREATE TABLE TransferItems (
            TransferItemId TEXT NOT NULL PRIMARY KEY,
            TransferJobId TEXT NOT NULL REFERENCES TransferJobs(TransferJobId),
            SourcePath TEXT NOT NULL,
            DestinationPath TEXT NOT NULL,
            SizeBytes INTEGER NOT NULL,
            BytesTransferred INTEGER NOT NULL,
            Status TEXT NOT NULL,
            ErrorMessage TEXT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL
        );
        CREATE INDEX IX_TransferItems_JobId ON TransferItems(TransferJobId);
        CREATE INDEX IX_TransferItems_Status ON TransferItems(Status);
    )SQL"},
    // Milestone 14: Enterprise (Central Server / Agent). One row per peer
    // Game Transfer Hub installation that has reported in - NodeId is
    // generated once by the reporting agent and persisted locally there, so
    // repeated reports upsert the same row (see RemoteNodeRepository).
    {6, R"SQL(
        CREATE TABLE RemoteNodes (
            NodeId TEXT NOT NULL PRIMARY KEY,
            Name TEXT NOT NULL,
            Address TEXT NOT NULL,
            LastSeenAt TEXT NULL,
            LastReportSummary TEXT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL
        );
    )SQL"},
    // NAS & HDD Health Monitor, Milestone 1 (Foundation): domain entities
    // and persistence only - no real SMART/SSH/SNMP provider yet, see
    // INasProvider and SimulationNasProvider. Health/Temperature columns
    // are nullable throughout because a provider that couldn't determine a
    // value must leave it unknown, never a defaulted 0 (spec Section 71).
    {7, R"SQL(
        CREATE TABLE NasDevices (
            NasDeviceId TEXT NOT NULL PRIMARY KEY,
            Name TEXT NOT NULL,
            IpAddress TEXT NOT NULL,
            Port INTEGER NOT NULL,
            Status TEXT NOT NULL,
            LastSeen TEXT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL
        );

        CREATE TABLE Disks (
            DiskId TEXT NOT NULL PRIMARY KEY,
            NasDeviceId TEXT NOT NULL REFERENCES NasDevices(NasDeviceId),
            SerialNumber TEXT NULL,
            Model TEXT NULL,
            Manufacturer TEXT NULL,
            Firmware TEXT NULL,
            CapacityBytes INTEGER NULL,
            InterfaceType TEXT NULL,
            Slot TEXT NULL,
            Status TEXT NOT NULL,
            HealthPercent INTEGER NULL,
            TemperatureCelsius INTEGER NULL,
            PowerOnHours INTEGER NULL,
            LastSeen TEXT NULL,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL
        );
        CREATE INDEX IX_Disks_NasDeviceId ON Disks(NasDeviceId);
        CREATE UNIQUE INDEX IX_Disks_NasDevice_Serial ON Disks(NasDeviceId, SerialNumber);

        CREATE TABLE HealthSnapshots (
            HealthSnapshotId TEXT NOT NULL PRIMARY KEY,
            DiskId TEXT NOT NULL REFERENCES Disks(DiskId),
            HealthPercent INTEGER NULL,
            Source TEXT NOT NULL,
            Reason TEXT NULL,
            CapturedAt TEXT NOT NULL
        );
        CREATE INDEX IX_HealthSnapshots_DiskId ON HealthSnapshots(DiskId);

        CREATE TABLE TemperatureSnapshots (
            TemperatureSnapshotId TEXT NOT NULL PRIMARY KEY,
            DiskId TEXT NOT NULL REFERENCES Disks(DiskId),
            TemperatureCelsius INTEGER NULL,
            CapturedAt TEXT NOT NULL
        );
        CREATE INDEX IX_TemperatureSnapshots_DiskId ON TemperatureSnapshots(DiskId);
    )SQL"},
    // FTP/FTPS/SFTP tab: saved connection profiles only. No password/secret
    // column - CredentialTarget is an opaque key into Windows Credential
    // Manager (see ICredentialStore); the actual secret never touches this
    // database.
    {8, R"SQL(
        CREATE TABLE FtpConnections (
            FtpConnectionId TEXT NOT NULL PRIMARY KEY,
            Name TEXT NOT NULL,
            Host TEXT NOT NULL,
            Port INTEGER NOT NULL,
            Protocol TEXT NOT NULL,
            Username TEXT NOT NULL,
            CredentialTarget TEXT NOT NULL,
            RemoteBasePath TEXT NOT NULL,
            CreatedAt TEXT NOT NULL
        );
    )SQL"},
    // Storage tab health display: HealthPercent/TemperatureCelsius on the
    // Storage a user already sees (local drives), as opposed to the NAS
    // Health Monitor's Disk table which models disks *inside* a separately-
    // registered remote NasDevice. Nullable for the same "Unknown must never
    // become 0" reason as migration 7 - populated only when a
    // IStorageHealthProvider is configured and the drive exposes the data.
    {9, R"SQL(
        ALTER TABLE Storages ADD COLUMN HealthPercent INTEGER NULL;
        ALTER TABLE Storages ADD COLUMN TemperatureCelsius INTEGER NULL;
    )SQL"},
    // Flags a storage whose health percent moved since the previous scan or
    // whose temperature crossed the overheat threshold - see
    // StorageService::applyDetection. Nullable/text rather than a bool
    // column so the reason travels with the flag instead of needing to be
    // recomputed.
    {10, R"SQL(
        ALTER TABLE Storages ADD COLUMN HealthAlertReason TEXT NULL;
    )SQL"},
    // Download Accelerator: saved CDN mirror candidates only. LastRttMs is
    // nullable and only ever written by an explicit "measure" pass (see
    // IHostPinger) - never populated inline on the request path.
    {11, R"SQL(
        CREATE TABLE CdnMirrors (
            CdnMirrorId TEXT NOT NULL PRIMARY KEY,
            HostPattern TEXT NOT NULL,
            Ip TEXT NOT NULL,
            Label TEXT NOT NULL,
            LastRttMs INTEGER NULL,
            CreatedAt TEXT NOT NULL
        );
        CREATE INDEX IX_CdnMirrors_HostPattern ON CdnMirrors(HostPattern);
    )SQL"},
    // Settings tab: one row of whole-app preferences (AppSettingsId is
    // always 1 - see AppSettingsRepository). HiddenTabs is a pipe-joined
    // list of sidebar labels rather than its own table since it's always a
    // short, app-defined checklist, never user-authored rows.
    {12, R"SQL(
        CREATE TABLE AppSettings (
            AppSettingsId INTEGER NOT NULL PRIMARY KEY,
            Theme TEXT NOT NULL,
            HiddenTabs TEXT NOT NULL,
            DefaultLocalFolder TEXT NOT NULL,
            LogLevel TEXT NOT NULL
        );
    )SQL"},
    // CDN URL Log: captures PSN/CDN URLs like PSX Download Helper, stores
    // mappings to local paths, user comments, and offline URLs. URLs are
    // logged as captured with timestamps, deduped by URL (update on recapture).
    {13, R"SQL(
        CREATE TABLE CdnUrls (
            cdnUrlId TEXT PRIMARY KEY,
            url TEXT NOT NULL UNIQUE,
            localPath TEXT,
            markText TEXT,
            lixianUrl TEXT,
            isLixian INTEGER DEFAULT 0,
            isCdn INTEGER DEFAULT 1,
            host TEXT,
            capturedAt TEXT NOT NULL,
            updatedAt TEXT NOT NULL
        );
        CREATE INDEX idx_CdnUrls_capturedAt ON CdnUrls(capturedAt DESC);
    )SQL"},
    // Caps how many "Add Console" HTTP MITM Proxy instances can be created
    // at once - admin-set, since a household only has so many consoles to
    // point at this PC. Existing rows get the same default AppSettings::
    // maxConsoles ships with, so upgrading doesn't silently zero out the
    // limit for anyone already running the app.
    {14, R"SQL(
        ALTER TABLE AppSettings ADD COLUMN MaxConsoles INTEGER NOT NULL DEFAULT 4;
    )SQL"},
    // Lets the Consoles tab double as a connection archive: rows created by
    // IConsoleManager::recordConnection() (traffic seen through the HTTP
    // MITM Proxy) are flagged so the UI can tell them apart from ones the
    // user typed in via "Add Console". Existing rows all predate this
    // feature, so they default to manually-added (0).
    {15, R"SQL(
        ALTER TABLE Consoles ADD COLUMN AutoDetected INTEGER NOT NULL DEFAULT 0;
    )SQL"},
    // The Games/Updates poster grid's web fallback originally read a SteamGridDB API key from this
    // column - since replaced by a keyless lookup (see PosterCache/PsPricesClient), so nothing
    // reads or writes it any more. Left in place rather than dropped: a shipped migration is never
    // removed or renumbered.
    {16, R"SQL(
        ALTER TABLE AppSettings ADD COLUMN SteamGridDbApiKey TEXT NOT NULL DEFAULT '';
    )SQL"},
    // User-defined Storage-tab groups. Membership is many-to-many so a
    // storage can appear in every category the owner wants without copying
    // or moving the underlying storage record.
    {17, R"SQL(
        CREATE TABLE StorageCategories (
            CategoryId TEXT NOT NULL PRIMARY KEY,
            Name TEXT NOT NULL COLLATE NOCASE UNIQUE,
            CreatedAt TEXT NOT NULL,
            UpdatedAt TEXT NOT NULL
        );
        CREATE TABLE StorageCategoryMemberships (
            CategoryId TEXT NOT NULL REFERENCES StorageCategories(CategoryId) ON DELETE CASCADE,
            StorageId TEXT NOT NULL REFERENCES Storages(StorageId) ON DELETE CASCADE,
            PRIMARY KEY (CategoryId, StorageId)
        );
        CREATE INDEX IX_StorageCategoryMemberships_StorageId
            ON StorageCategoryMemberships(StorageId);
    )SQL"},
}};

}  // namespace

void Migrator::applyMigrations(Database& db) {
    db.execute("PRAGMA journal_mode = WAL;");

    int currentVersion = 0;
    {
        auto stmt = db.prepare("PRAGMA user_version;");
        if (stmt.step()) currentVersion = static_cast<int>(stmt.columnInt64(0));
    }

    for (const auto& migration : kMigrations) {
        if (migration.version <= currentVersion) continue;
        db.execute(migration.sql);
        db.execute(std::format("PRAGMA user_version = {};", migration.version));
    }
}

}  // namespace gth::database
