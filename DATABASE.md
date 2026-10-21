# Database

SQLite via the official C amalgamation (fetched from sqlite.org, not a
third-party wrapper library). The connection path is owned by
`gth::infrastructure::ServiceContainer::defaultDatabasePath()`, defaulting to
`%LOCALAPPDATA%\GameTransferHub\gth.db`. Migrations are applied automatically
at app startup (`Migrator::applyMigrations`).

## Migration mechanism

There is no ORM/migration framework in a native C++ stack, so schema
versioning is tracked via SQLite's built-in `PRAGMA user_version`: each
migration is a `{version, sql}` entry in `Migrator.cpp`'s `kMigrations` array,
applied in order if `version > current PRAGMA user_version`, then the pragma
is bumped. Safe to call on every startup - already-applied migrations are
skipped.

## Schema (Milestone 1)

### `Storages`

One row per storage location known to the archive (a local drive today;
NAS/SMB/network shares once those detectors exist).

| Column              | Type    | Notes                                    |
|---------------------|---------|--------------------------------------------|
| StorageId (PK)      | TEXT    | GUID string (`UuidCreate`/`UuidToStringA`) |
| Name                | TEXT    |                                            |
| Type                | TEXT    | Enum stored as string (`LocalHdd`, `Nas`, ...) |
| RootPath            | TEXT    | Unique - natural key used to reconcile scans |
| TotalCapacityBytes  | INTEGER |                                            |
| FreeSpaceBytes      | INTEGER |                                            |
| UsedSpaceBytes      | INTEGER |                                            |
| Status              | TEXT    | Enum stored as string (`Online`, `Offline`, `Error`, `Unknown`) |
| FileSystem          | TEXT    | Nullable (e.g. NTFS)                       |
| LastScan            | TEXT    | Nullable ISO-8601 UTC timestamp            |
| IsAvailable         | INTEGER | Boolean (0/1)                              |
| CreatedAt           | TEXT    | ISO-8601 UTC timestamp                     |
| UpdatedAt           | TEXT    | ISO-8601 UTC timestamp                     |

Indexes: unique on `RootPath`; non-unique on `Type` and `Status` (matches the
spec's list of fields expected to be queried/filtered often).

## Adding a migration

Add a new `{version, sql}` entry to `kMigrations` in
`src/database/src/Migrator.cpp` with the next sequential version number.
Never edit an already-shipped migration's SQL in place - add a new one, the
same way EF Core or any other migration tool would.

## Schema (Milestone 2: Game Indexer)

### `Games` / `GameVariants` / `GameVersions` / `GameFiles`

Four-level hierarchy, deliberately **not** flattened:

```
Game            "Spider-Man 2"
  GameVariant   PS5 / Europe          <- stable across version updates
    GameVersion   1.04 (Missing)      <- one row per version folder on disk
    GameVersion   1.06 (Active, IsCurrent)
      GameFile      data.bin
      GameFile      dlc.bin
```

- **Games**: `GameId` (PK), `Title` (unique), `CreatedAt`, `UpdatedAt`.
- **GameVariants**: one per `{Platform}/{Title}/{Region}` container folder.
  `RelativePath` (unique per `StorageId`) is the natural key a rescan
  reconciles against - it does **not** include the version segment.
  `Status` is `Active`/`Missing` (the container folder disappeared).
- **GameVersions**: one per version folder found under a variant. Natural key
  is `(GameVariantId, RelativePath)`. Multiple versions can be `Active`
  simultaneously (an in-progress atomic update, or N retained old versions);
  exactly one is flagged `IsCurrent` (whichever has the highest version
  string per `VersionComparer`'s numeric-dotted comparison - a heuristic, not
  an authoritative decision from the Update Manager, which owns that
  question once it exists in Milestone 4).
- **GameFiles**: flat file listing per `GameVersion`, keyed by
  `(GameVersionId, RelativePath)`. `SizeBytes` + `LastModified` are the
  incremental-scan change-detection fingerprint - no content hash is
  computed here (that's Update Manager verification work, later).

### `ScanJobs`

One row per indexing pass over a `Storage`: `Status` (`Running`/`Completed`/
`Failed`), timing, and per-pass counts (`GamesAdded`, `VariantsAdded`,
`VersionsAdded`, `VersionsUpdated`, `VersionsMissing`, `VersionsUnchanged`).

### A modeling mistake worth knowing about

The first cut of this schema baked `Version` into `GameVariant`'s identity
(and therefore into the path used to match a scan back to an existing row).
Under the `{Platform}/{Title}/{Region}/{Version}` folder convention, a
version bump always changes that path - so every version update looked like
a brand-new variant appearing alongside an old one silently going missing,
never an update to the same variant. A test written specifically to check
"does a version bump register as an update" (`VersionBumpKeepsSameVariant...`
in `test_game_indexer_service.cpp`) caught this before anything was built on
top of it. The fix was introducing `GameVersion` as its own entity, exactly
as the original specification's Section 29 already called for -
`GameVariant` now identifies the stable container, `GameVersion` identifies
one version folder within it, and a version bump is correctly recognized as
"new version of the same variant" instead of "new variant."

## Schema (Milestone 4: Update Manager foundation)

### `UpdateInfos`

One row per `GameVariantId` (unique index - a re-check overwrites the
previous snapshot rather than accumulating rows): `CurrentVersion`,
`LatestVersion`, `UpdateSizeBytes`, `Source` (the `IUpdateSource::name()`
that produced it), `Status` (`UpToDate`/`UpdateAvailable`/`NoSafeMatch`/
`Unknown`), `DetectedAt`, `CheckedAt`, optional `Metadata`.

### `UpdateJobs`

One row per queued/in-progress/completed update operation, `Status` moving
through the specification's exact flow: `Detected` -> `Queued` ->
`Preparing` -> `Downloading` -> `Downloaded` -> `Verifying` -> `Verified` ->
`Replacing` -> `Completed`, with `Failed`/`Cancelled`/`RolledBack` as exits.
Duplicate prevention queries `WHERE GameVariantId = ? AND NewVersion = ? AND
Status NOT IN (terminal states)`.

### `UpdateHistories`

Permanent, append-only record of every completed update attempt (successful
or not) - `PreviousVersion`, `NewVersion`, timing, `VerificationStatus`,
`Result`. Never edited after being written; this is the audit trail behind
the "Update History" view (1.00 -> 1.02 -> 1.04 -> 1.06).

## Schema (Milestone 10: Console Manager)

### `Consoles`

One row per manually-registered console/device: `Name`, `Platform`,
`IpAddress`, `Port`, `Protocol` (`Ftp`/`Smb`/`Custom`), `Status`
(`Online`/`Offline`), nullable `LastConnected`. There is no auto-discovery -
rows are only ever added, checked, or removed by explicit user action (see
`IConsoleManager`/`IReachabilityChecker`), never populated by scanning the
network.

## Schema (Milestone 11: Transfer Engine)

### `TransferJobs` / `TransferItems`

```
TransferJob   "Copy Spider-Man 2 to NAS02"   (Status = aggregate of its items)
  TransferItem   data.bin   (SourcePath -> DestinationPath, BytesTransferred/SizeBytes)
  TransferItem   dlc.bin
```

- **TransferJobs**: `Status` mirrors the aggregate of its items (`Queued` until
  a worker picks up the first item, `Completed` only once every item is
  `Completed`, `Failed`/`Cancelled` propagate up from any item). Nullable
  `CompletedAt`.
- **TransferItems**: one row per file copy, `Status` moving through
  `Queued -> Preparing -> Transferring -> Completed`, with `Paused`,
  `Verifying`, `Failed`, and `Cancelled` as the other states in the spec's
  transfer model. `BytesTransferred` is updated as `ITransferProvider`'s
  progress callback fires, so a killed/crashed process leaves some items
  visibly mid-`Transferring` - `TransferQueueService::recoverIncompleteItemsFromPreviousRun()`
  resets any such item back to `Queued` at the next startup rather than
  leaving it stuck. Indexed on `TransferJobId` and `Status`.

## Milestone 12: Monitoring - no schema change

`ArchiveHealthReport` (the Reports page's data) and the Logs page's content
are both computed/read on demand rather than persisted - the former is a
cheap aggregate over tables that already exist, the latter reads spdlog's
already-durable daily log file directly. No new migration was needed.

## Schema (Milestone 14: Enterprise - Central Server / Agent)

### `RemoteNodes`

One row per peer Game Transfer Hub installation that has ever reported in to
this machine's Central Server. `NodeId` is generated once by the reporting
Agent and persisted locally there (a text file, not this database - see
ARCHITECTURE.md), so repeated reports upsert the same row rather than
accumulating one per app restart. `Address` is the peer's IP as seen by the
server (`getpeername`), not self-reported, so it can't be spoofed by a
malicious payload. `LastReportSummary` is an opaque, human-readable string
(not further-normalized columns) - see ARCHITECTURE.md's Enterprise section
for why the wire protocol stays this simple.

## Planned entities (not yet implemented)

Later milestones may add `Platform`/`Region` as normalized lookup tables
(kept as plain string columns for now - no lookup-table benefit exists yet).
`LogEntry`, `Report`, and `SchedulerJob` from the specification were
deliberately *not* added as tables in Milestone 12 - see the note above. Full
archive-data replication (as opposed to Milestone 14's health-summary
reporting) remains unimplemented - see ARCHITECTURE.md. Adding any future
entity is additive: a new table/index via a new migration entry, plus a new
`*Repository` class implementing whatever Core interface it needs.
