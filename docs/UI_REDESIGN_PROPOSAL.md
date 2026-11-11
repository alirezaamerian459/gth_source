# Game Transfer Hub — Complete UI/UX Redesign Proposal

**Status:** Approved for implementation by the owner on 2026-11-10. The supplied `stitch_storage_manager_ui_redesign (3).zip` is the visual reference. UI changes must remain behavior-preserving.

**Prepared:** 2026-11-10

## 1. Product definition

Game Transfer Hub (GTH) is a Windows desktop operations console for managing a local game archive across multiple storage locations. It combines:

- storage discovery, capacity, health, temperature, and physical-disk visibility;
- game indexing and poster-based library browsing;
- Sony version checks, metadata/package synchronization, and guided PS5 link collection;
- download history and cancellation;
- console registration and reachability checks;
- local file transfers;
- FTP client, FTP server, LAN console proxy, CDN URL capture, and mirror acceleration;
- diagnostic logs, archive reports, enterprise server/agent reporting;
- Telegram-backed backup and restore;
- application settings, role-based access, firewall access, and sidebar customization.

The redesign must make this breadth understandable without removing capability or silently changing service behavior.

## 2. Non-negotiable constraints

The implementation remains a Windows-only C++20/CMake/WTL application with the existing modular-monolith boundaries:

```text
Win32/WTL views -> application services -> core contracts
                         ^
        database, storage detection, network, transfer, FTP,
        verification, credentials, logging, and update adapters
```

The redesign must:

1. keep the existing route indices and service contracts;
2. keep archive safety, checksum verification, cancellation, and path validation unchanged;
3. keep networking, FTP, proxy, Sony, Telegram, and enterprise behavior unchanged unless separately approved;
4. keep asynchronous jobs on their existing worker threads and show their current progress/cancel states;
5. preserve persisted data, credentials, category membership, update history, and settings;
6. keep every action available, even when it moves behind a disclosure panel, context menu, command palette, or selected-item toolbar;
7. add tests for changed behavior and run the existing Debug/Release build and CTest checks;
8. avoid third-party UI dependencies and avoid replacing native WTL controls with a web view.

## 3. Current navigation and access model

The current sidebar contains 15 routes:

| Route | Current label | Current group | Signed-out availability |
|---|---|---|---|
| 0 | Dashboard | Workspace | Always available |
| 1 | Games | Workspace | Available |
| 2 | JB | Workspace | Available |
| 3 | Updates | Workspace | Available |
| 4 | Downloads | Workspace | Available |
| 5 | Storage | Library data | Available |
| 6 | Consoles | Devices | Available |
| 7 | Transfers | Workspace | Admin/sign-in required |
| 8 | FTP | Devices | Available |
| 9 | Network | Devices | Admin/sign-in required |
| 10 | Logs | Administration | Admin/sign-in required |
| 11 | Reports | Administration | Admin/sign-in required |
| 12 | Enterprise | Administration | Admin/sign-in required |
| 13 | Backups | Library data | Available |
| 14 | Settings | Settings | Always available |

Dashboard and Settings are pinned and cannot be hidden. The other routes can be hidden in Settings. The current shell also has:

- a hamburger navigation toggle that collapses or restores the sidebar;
- a page title and explanatory hint;
- a shell status label;
- a global Queue shortcut to Transfers;
- a global Updates notification shortcut that opens Updates and clears the visible count.

## 4. Tab-by-tab functional inventory and redesign plan

### 4.1 Dashboard

#### Purpose

The landing page gives a quick health snapshot of registered storage and the local connection information used by consoles or other PCs.

#### Current controls and data

- **Scan Local Storage** — calls `IStorageService::detectAndSyncLocalStorages()`, adds newly detected local storage, updates known rows, marks missing local storage offline, then refreshes the summary.
- **Connection details** disclosure — expands/collapses local IP, port, copy action, and network status.
- **Copy Connection Info** — obtains local connection information and copies the connection URL to the Windows clipboard.
- Summary cards: total storage count, online/offline count, total capacity, and free space.
- Expanded network details: IP address, port, readiness/error state.

#### What it needs to work

- `IStorageService` and its storage-detection providers;
- access to local drive/volume metadata;
- Windows network-interface information;
- Windows clipboard access;
- no external account or Internet connection for the core summary.

#### Proposed UI

Use a calm “home” dashboard with four priority cards: **Storage**, **Games**, **Updates**, and **Activity**. Keep Scan as the primary action. Put connection details in a compact “Connect this PC” card with a copy button and a clearly named status badge. Do not expose raw technical details until the user opens the card.

### 4.2 Games

#### Purpose

The main game library shows indexed games across registered storages, with poster tiles, platform, title, and installed version.

#### Current controls and data

- **Scan All Storages** — starts the indexed scan over registered storage locations; detects new games, versions, and title names; optionally performs online title lookup for eligible PS4/PS5 IDs; updates the library and poster grid.
- **Test Online Lookup** — runs a diagnostic lookup for one PS5 and one PS4 sample ID and displays the result in a dialog. It verifies reachability and lookup behavior without changing the library.
- Poster grid — title, poster, platform/version label, and the current Sony verdict when one has been checked in Updates.
- Shared job lock — if Updates, scanning, or another shared job is active, the scan button is disabled and the user is directed to Updates for the active job.

#### What it needs to work

- `IStorageService`, `IGameIndexer`, and `IGameLibraryService`;
- the shared archive job state;
- local storage read access and permission to inspect package/archive files;
- optional online `ITitleLookupService` access for PS4/PS5 title naming;
- `PosterCache` and `IGamePosterService` for cached artwork and PSPrices/cover-art fallback.

#### Proposed UI

Use a library-first page: a search/filter bar, platform/status filters, a prominent **Scan library** action, and poster cards with only title, platform, version, and a compact state badge. Move the online lookup diagnostic into a “Developer diagnostics” disclosure. Selecting a card should open a details side panel rather than forcing the user to infer actions from a tile.

### 4.3 JB Games

#### Purpose

The JB page finds jailbreak-style game folders and archives on local drives, separate from the normal indexed library.

#### Current controls and data

- **Scan target** — a visible selector with `All local drives` plus one entry for each available local HDD, SSD, external HDD, or USB storage (including its friendly name and root path).
- **Scan all local drives / Scan selected drive** — scans the selected scope for folders named `hacked` or `p30day`; when one drive is selected, traversal is restricted to that storage only. The scan inspects child directories/files for package, dump, patch, DLC, and related markers; identifies probable title IDs/platforms; and records exFAT and scan counts.
- The selected target is captured when the scan starts, so changing storage selection cannot redirect an active scan.
- Poster grid — title, platform, path-derived details, artifact summary, and poster.
- Status line — drives scanned, JB folders found, exFAT count, and scan errors.
- The page automatically starts its first scan when opened.

#### What it needs to work

- local storage detection and filesystem read permission;
- recursive directory traversal, including extended Windows paths;
- no network dependency for scanning; artwork lookup is optional;
- `PosterCache` and `IGamePosterService` for posters.

#### Proposed UI

Rename the page to **JB Library** for clarity. Show a scan progress banner, a drive filter, and cards with path, artifact badges, and platform. Keep the scan action visible. Put raw folder-pattern rules and exFAT diagnostics behind “Scan details.”

### 4.4 Updates

#### Purpose

Updates is the operational center for comparing installed games with Sony, setting/repairing installed versions, downloading metadata/packages, and managing PS5 version-file links.

#### Current controls and data

Primary controls:

- **Update all games** — asks for confirmation, optionally scans storages for newly added games, learns links from local metadata pieces, checks every game, runs the one-game-at-a-time package sync, verifies every downloaded piece, records history, rescans changed storages, and retries eligible failures after the rest of the line. If PS5 games lack links, the guided Prosperopatches collection is started and the chain continues after links are learned.
- **Check for updates** — calls the read-only `IUpdateChecker` for the entire active library. Results are stored in shared state and shown in cards.
- **Cancel** — stops the current sync, link download, or guided link-collection session while preserving completed files and learned links.

Automation disclosure:

- **Check for updates** — enable scheduled Sony checks.
- **Add missing metadata** — allow scheduled metadata additions.
- **Download updates (may use many GB)** — allow scheduled package downloads after explicit confirmation.
- **Scan for new games first** — scan reachable storages before scheduled work.

Selected-game disclosure:

- **Installed version** field — edit the stored version associated with the selected game.
- **Set version** — save a user-specified version next to the game files.
- **Reset** — clear the override and return to automatic detection.
- **Sync with Sony** — run a full sync for only the selected game after confirmation.
- **Skip game** — skip the current game during guided PS5 link collection.

Secondary tools (currently available through the Updates grid context menu):

- **Sony update links** submenu:
  - Collect links from prosperopatches.com (guided, human-completed CAPTCHA when required);
  - Add links from the clipboard;
  - Add links from a file;
  - Download the public Sony link list;
  - List games that still need a link and open the list in Notepad;
  - Open the links file;
  - Open the copied-SC-links log.
- **Import a list of installed versions** — import title ID/version lines.
- **Open the selected game's tracker page** — open the external tracker for the selected title.
- **How this tab works** — show the complete guide.

Other UI:

- Filter field for title, title ID, platform, region, or version.
- PSPrices/cover-art poster cards with title, platform, status, ID, version, and current progress state.
- Background update polling every 15 minutes while the shared job slot is idle.
- Shared header notification when the latest check reports newly available updates.

#### What it needs to work

- `IGameLibraryService`, `IGameIndexer`, `IUpdateChecker`, `IPackageSync`, `IStorageService`, and shared archive state;
- local package/archive read/write access;
- checksum verification and safe package replacement;
- Sony network access for update checks and package metadata;
- `sony-version-links.tsv`, `sony-links-needed.tsv`, and `sony-automation.txt` in the app data directory;
- browser/clipboard integration for guided Prosperopatches collection;
- human completion of any real CAPTCHA; no bypass behavior is permitted;
- poster cache and cover-art service for artwork.

#### Proposed UI

Make Updates a clear three-stage workflow:

1. **Discover** — current check result, last check time, update count, filter, and cards;
2. **Plan** — selected-game actions, link coverage, automation policy;
3. **Apply** — Update all, Sync selected, queue/progress, cancel, and per-game status.

Use a fixed primary toolbar with **Check now**, **Update all**, and a visible progress/cancel region. Put link management and version import in a labeled **Tools** menu, not an unexplained “More” control. The bell should announce “N updates available,” not only display a number.

### 4.5 Downloads

#### Purpose

Downloads is the audit and control view for active, queued, and completed package work.

#### Current controls and data

- **Refresh** — reread the download history and live shared state.
- **Open history file** — open `download-history.tsv` in Notepad.
- **Stop** — cancel the selected live download or remove a queued item; completed history rows cannot be stopped.
- Table columns: When, Game, Type, Status, Size, Duration, Speed, Details.
- Live rows come from shared Updates state; queued rows come from the shared queue; completed rows come from the history file.

#### What it needs to work

- `SharedArchiveState` for live/queued rows;
- the local `download-history.tsv` file;
- the Updates/package-sync callbacks that write per-game history;
- no external service for browsing history, although active jobs may use Sony/network services.

#### Proposed UI

Use three filterable segments: **Active**, **Queued**, and **History**. Put **Stop selected** in the selection toolbar and expose the history file as a secondary “Open log” action. Show progress, ETA, bytes, and failure reason in a detail drawer instead of requiring a very wide table.

### 4.6 Storage

#### Purpose

Storage is the source of truth for discovered storage roots, capacity, allocation, health, temperature, physical disk membership, and user-defined categories.

#### Current controls and data

- **Rescan** — runs local storage detection and health synchronization in the background.
- **Category name** field — accepts an owner-defined category name.
- **Add category** — creates a persistent category in SQLite.
- **Show health details / Hide health details** — expands disk-level health explanations and alert reasons.
- Storage overview — total capacity, used bytes, allocation percentage, and category legend.
- Category cards — each saved category is shown beside other categories; each contains its assigned storages.
- **Right-click category card** — add an unassigned storage, remove a member storage, or delete the category. Deleting a category does not delete the storage or game data.
- **Other storages** — automatically contains detected storages that are not assigned to a category.
- Each storage card shows root/name, type/status, capacity bar, aggregate health/temperature, and physical disk cards with disk number, model, size, health, temperature, and optional detail.
- Storage scanning auto-refreshes while the page is visible and includes the explicit Rescan retry.

#### What it needs to work

- `IStorageService`, SQLite storage/category repositories, and migrations;
- local volume enumeration and availability checks;
- physical-disk/SMART/NVMe health providers where supported;
- permission to query Windows storage and WMI/health APIs;
- no Internet connection for core storage data.

#### Proposed UI

Use a storage command center with:

- a top overview strip with capacity and health alerts;
- a category board with a clear **Create category** action;
- category cards with an explicit **Manage drives** button plus right-click as a secondary path;
- drive cards that emphasize model, capacity, health, temperature, and mount/root;
- a health filter: All, Healthy, Attention, Offline;
- a dedicated “Uncategorized” card rather than treating it as an afterthought.

Category membership must remain many-to-many. The new layout must never imply that deleting a category deletes a drive.

### 4.7 Consoles

#### Purpose

Consoles stores and tests console endpoints used by the rest of the application.

#### Current controls and data

- **Add console** disclosure — Name, Platform, IP Address, Port, then **Add Console**.
- **Check Selected** — calls reachability for the selected console.
- **Remove Selected** — removes the selected console.
- Auto-refresh keeps status and selection current.
- Table columns: Name, Platform, IP Address, Port, Status, Last Connected, Source (manual/auto).

#### What it needs to work

- `IConsoleManager` and its repository;
- local database persistence;
- network reachability to the selected endpoint;
- a valid name, IP/host, and numeric port for new consoles.

#### Proposed UI

Use a “Console inventory” page with a compact table/card hybrid. Keep **Add console** as the primary action. Make row selection reveal Check, Remove, and connection details. Use explicit status text and icon plus last-seen time; never use color alone.

### 4.8 Transfers

#### Purpose

Transfers handles generic local file-copy jobs separate from package updates.

#### Current controls and data

- **New transfer** disclosure — Name, Source, Destination.
- **Enqueue** — creates a transfer job with one source/destination item.
- **Cancel selected** — cancels the selected transfer item.
- **Refresh** — reloads job/item state.
- Table columns: Job, Source, Destination, Status, Progress.

#### What it needs to work

- `ITransferQueueService` and persistent transfer data;
- source read access, destination write access, and enough free space;
- cancellation support in the transfer provider.

#### Proposed UI

Use a queue-first layout with **New transfer** and **Refresh** in the toolbar, cards for active jobs, and an expandable item detail. Keep source/destination paths visible but truncatable with a copy/full-path action.

### 4.9 FTP

#### Purpose

FTP is a multi-purpose network workspace containing an FTP client, a local FTP server, a remote browser, accelerator/mirror management, LAN console proxy controls, dynamic MITM instances, and CDN URL capture.

#### Current controls and data

Connection manager:

- fields: Name, Host, Port, Protocol (FTP/FTPS/SFTP), Username, Password, Remote Base Path;
- **Add Connection**;
- **Connect**, **Disconnect**, **Remove** selected connection;
- connections table: Name, Host, Port, Protocol, Username;
- status dot and connection message.

Host-a-server section:

- Shared Folder field and **Browse...**;
- **Include JB Library (read-only)** — when enabled, the hosted server adds a virtual `/JB Library` tree containing the current JB catalog without changing the selected shared folder or enabling writes to JB entries;
- Port (default 2121);
- **Start**, **Stop**, **Copy Address**;
- status dot and LAN address/port message.

Remote browser:

- Remote Directory and Local Path fields;
- **Upload**, **Download**, **Refresh**;
- remote table: Name, Type, Size;
- upload uses the current remote directory and the local filename; download requires a selected remote file and local destination folder.

Advanced network tools disclosure:

- accelerator mirrors: Host Pattern, Mirror IP, Label, **Add Mirror**, **Measure All**, **Remove**; table includes last RTT;
- LAN Console Proxy: editable local IP, Port (default 8080), **Start**, **Stop**, optional Cache Folder with **Browse...**, **Clear Cache**, status and proxy configuration text;
- the LAN Console Proxy automatically checks the shared JB catalog after the normal indexed-library lookup and serves matching JB package/archive requests locally; unmatched requests continue to the origin unchanged;
- **+ Add Console** creates additional independent proxy/MITM listeners;
- CDN URL Log: manual URL field, **Add URL**, URL table with URL/Mark/Local Path/Source/Captured, editable Mark and Local Path, **Browse...**, **Remove**, **Copy URL**. Double-click also copies the selected URL.

#### What it needs to work

- FTP connection, transfer, and server services;
- credential storage for saved usernames/passwords;
- Windows sockets and network reachability;
- filesystem read/write access for shared folders, uploads, downloads, and cache;
- `IDownloadAcceleratorService` and its mirror/proxy implementation;
- `ICdnUrlService` persistence;
- admin sign-in for controls that are intentionally restricted by the current access model;
- firewall rules and correct LAN addressing for console connections.

#### Proposed UI

This page needs the largest information-architecture change. Split it into internal sub-tabs or a stepper:

1. **Connections** — saved client connections and connect/disconnect;
2. **Browser** — remote files and upload/download;
3. **Host** — local FTP server;
4. **Accelerator** — mirrors, cache, and proxy;
5. **CDN log** — captured URLs and local mapping.

The current “More network tools” panel hides too many unrelated systems behind one label. Each sub-page should have one primary task and its own status region.

### 4.10 Network

#### Purpose

Network lists the interfaces visible to the application and their addressing state.

#### Current controls and data

- **Refresh** — calls `INetworkInterfaceDetector::detect()`.
- Table columns: Name, Type, Status, IPv4, Gateway, MAC.

#### What it needs to work

- Windows network-interface enumeration;
- admin access under the current role policy for the route to be visible;
- no external connection and no data mutation.

#### Proposed UI

Make this a read-only “Network diagnostics” page with an interface summary at the top, a filter for Up/Down, and a row detail panel. Keep Refresh visible, but explain that this page reports the PC’s interfaces rather than testing a remote console.

### 4.11 Logs

#### Purpose

Logs displays today’s application log for troubleshooting.

#### Current controls and data

- **Refresh** — rereads today’s UTF-8 log file.
- Displays the resolved log path and a read-only multiline text area.
- If no file exists, shows “No log file for today yet.”

#### What it needs to work

- the local app log directory under `%LOCALAPPDATA%\GameTransferHub\logs`;
- file read access;
- admin sign-in under the current route policy.

#### Proposed UI

Use a log viewer with severity filters, search, copy selected lines, open folder, and a clear “today’s file” indicator. Keep the raw text accessible, but make common failures discoverable as structured events.

### 4.12 Reports

#### Purpose

Reports provides an archive-health snapshot generated from the application services.

#### Current controls and data

- **Refresh** — calls `IReportService::generate()`.
- Report timestamp.
- Metrics table: total/online/offline storages, total/ missing games/variants/versions, updates available, pending/failed update jobs, total/online/offline consoles, active/completed/failed transfer jobs.

#### What it needs to work

- `IReportService` and the database/service state it aggregates;
- admin sign-in under the current route policy;
- no external service for generation.

#### Proposed UI

Turn the page into an “Archive health” dashboard: high-value KPI cards first, then grouped sections for Storage, Library, Updates, Consoles, and Transfers. Keep Refresh and add export only as a separately approved feature. Make every metric clickable only if a safe route exists to the underlying filtered view.

### 4.13 Enterprise

#### Purpose

Enterprise provides a central server and reporting agent for multi-PC operation.

#### Current controls and data

Central server:

- Port field (default 6900);
- **Start Server / Stop Server**;
- running/not-running status.

Agent reporting disclosure:

- Server Address, Port, Node Name, Interval (seconds);
- **Start Reporting / Stop Reporting**;
- first-report status and local node ID/error information.

Known nodes:

- **Refresh**;
- table columns: Node ID, Name, Address, Last Seen, Summary.

#### What it needs to work

- `ICentralServerService`, `IAgentService`, and persistent local node ID;
- Windows sockets and firewall access;
- valid numeric ports and reporting interval;
- a reachable central server for agent reporting;
- admin sign-in under the current route policy.

#### Proposed UI

Use an “Enterprise topology” page with two clearly separated cards: **This PC as server** and **This PC as agent**. Show connection tests, last successful report, and endpoint status. Put known nodes into a table below. Avoid presenting server and agent as if they were the same switch.

### 4.14 Backups

#### Purpose

Backups protects the GTH library by sending verified backup manifests/chunks to Telegram and restoring them into a new local folder.

#### Current controls and data

Telegram setup disclosure:

- Telegram bot token (secret field);
- private channel username or chat ID;
- **Save Telegram settings** — saves the token in Windows Credential Manager and the chat ID in the app data directory;
- **Test connection** — verifies that the bot can access the channel.

Backup actions:

- games table: Game, Platform, Version, Status;
- **Back up selected** — backs up the selected active game;
- **Back up all available games** — backs up all active library games;
- **Cancel** — requests cancellation after the current network operation;
- **Refresh** — reloads games and available backup rows.

Restore disclosure:

- restore destination folder and **Browse...**;
- Telegram manifest file ID, or select a backup row;
- **Restore backup** — downloads and verifies the backup into a new folder under the chosen destination;
- **Copy selected restore ID** — fills and copies the selected manifest ID.

#### What it needs to work

- `BackupService`, `IGameLibraryService`, and `TelegramBackupTransport`;
- Windows Credential Manager for the token;
- Telegram network access and a bot with channel permissions;
- a writable backup working directory and restore destination;
- verification/checksum logic and safe manifest handling;
- no secrets committed to the repository or shown in logs.

#### Proposed UI

Make this a two-step workflow: **Configure destination** and **Backup/Restore**. Show a persistent connection-health badge and last successful backup. Keep the token field hidden by default and show exactly where the restore ID is stored/copied. Separate backup and restore visually so a destructive-looking restore cannot be confused with backup.

### 4.15 Settings

#### Purpose

Settings controls appearance, sidebar visibility, default paths, logging, firewall rules, admin access, console limits, and advanced preferences.

#### Current controls and data

Always-visible basics:

- **Light Mode**, **Dark Mode**;
- Sidebar Tabs visibility checkboxes for hideable routes;
- **More settings / Hide other settings** disclosure.

Expanded settings:

- Default Local Folder and **Browse...** / **Save** — seeds FTP’s local path;
- Logging verbosity: Error, Warning, Info, Debug;
- Windows Firewall status and explanation;
- **Allow through Firewall** / **Remove rule**;
- Admin Sign In: Username, Password, **Sign In**;
- signed-in state: **Sign Out**;
- Change Username/Password: current password, optional new username/password, **Update**;
- Console Limits: maximum consoles and **Save**;
- **Reset to Defaults**;
- status messages.

#### What it needs to work

- `IAppSettingsService` and persisted settings;
- `IAdminAuthService` and Windows Credential Manager;
- `IFirewallService` and Windows firewall APIs;
- administrator rights where Windows requires them;
- a callback to reapply theme/sidebar visibility after settings change.

#### Proposed UI

Replace one long page with a settings index and focused sections:

1. Appearance;
2. Navigation;
3. Paths and storage defaults;
4. Logging and diagnostics;
5. Firewall and connectivity;
6. Administrator account;
7. Console limits;
8. Reset and recovery.

Keep the current live-save behavior where it is safe, but show an explicit saved state and never clear password fields after an error without explaining why.

### 4.16 Coming Soon placeholder

`ComingSoonView` is an internal fallback used when a route has no implemented view. It shows a title and “This module is planned for a later milestone.” It should not be presented as a normal operational tab in the redesigned navigation; it should be a consistent empty-state component for future routes.

## 5. Proposed application-wide UI language

The UI should move from implementation-oriented labels to user-task language:

| Current style | Proposed style |
|---|---|
| Scan All Storages | Scan library |
| Test Online Lookup | Test title lookup |
| Update all games | Update library |
| Check for updates | Check now |
| More network tools | Network tools |
| More settings | Additional settings |
| Rescan | Scan again |
| Check Selected | Check connection |
| Remove Selected | Remove console |
| Open history file | Open download history |
| Show health details | Show disk health |

Labels should describe the result or task, not the internal service name. Technical details remain visible in a detail panel, help text, or diagnostics view.

### Localization proposal

The current application hardcodes English strings throughout the views. A safe language redesign should happen before visual implementation:

1. Introduce stable message keys and one localization service at the app layer.
2. Move visible labels, status messages, confirmations, table headings, and errors into locale resources.
3. Keep paths, title IDs, protocol names, versions, URLs, and log content unlocalized.
4. Keep English as the fallback locale.
5. Add Persian as the first additional locale only after the key inventory is complete; the owner can choose another language before implementation.
6. Support right-to-left layout as a deliberate phase: mirrored spacing, navigation order, table alignment, icon direction, and mixed Latin technical strings must all be tested.
7. Use locale-aware numbers, dates, byte sizes, plurals, and status messages.
8. Never concatenate translated fragments in a way that makes grammar impossible to translate; use complete message templates.
9. Add a Settings language selector and apply changes after a controlled view refresh/restart boundary.

The first implementation should not translate technical error text emitted by external services unless a reliable message map exists; show the original detail beneath a localized summary.

## 6. New shell and navigation proposal

### Header

Keep a single predictable header on every page:

- hamburger button with an accessible name and visible focus state;
- application identity and current page title;
- optional breadcrumb for deep sub-pages;
- compact activity status;
- Updates notification bell with a meaningful label such as “3 updates available”;
- Queue shortcut;
- user/admin status shortcut.

Avoid putting a permanent button for every advanced tool in the header.

### Navigation groups

Use task-based groups rather than implementation ownership:

- **Library:** Dashboard, Games, JB Library, Updates, Downloads;
- **Storage & devices:** Storage, Consoles, FTP, Network;
- **Operations:** Transfers, Backups;
- **Administration:** Reports, Logs, Enterprise;
- **Preferences:** Settings.

Each group is collapsible. The active route is always visually distinct. The navigation must remain keyboard-operable, and the hamburger must not trap focus when the sidebar is collapsed.

### Shared page structure

Every page should follow the same visual order:

1. Page title and one-sentence purpose;
2. one primary action;
3. current status/health strip;
4. main content surface;
5. secondary controls in a labeled disclosure or context menu;
6. empty, loading, error, and success states.

Use one spacing scale, one heading hierarchy, one button hierarchy, and one status vocabulary across all routes.

## 7. Design system direction

The UI/UX Pro Max search selected a balanced, minimal Swiss-style operations dashboard direction for GTH:

- dark navy/charcoal surfaces with high-contrast text;
- one primary blue/indigo action color and one semantic green success color;
- red/orange only for destructive or health-warning states;
- restrained borders and shadows; no decorative gradients that compete with data;
- consistent type scale and monospace only for IDs, paths, ports, versions, and diagnostics;
- concise one-line badges that do not wrap;
- native keyboard focus rings and visible hover/pressed/disabled states;
- no emoji used as icons; use a consistent vector/icon-font resource set with accessible text labels;
- subtle motion only for disclosure and progress transitions, with a reduced-motion-safe path.

The current app uses native Windows fonts and WTL controls. The visual direction should be implemented with the existing theme tokens rather than importing web fonts or a web UI framework unless the owner separately approves that dependency change.

## 8. Cross-tab behavior and state model

The redesigned UI should expose the current state model consistently:

- **Idle** — action available;
- **Working** — progress and cancel visible;
- **Needs attention** — actionable error or missing prerequisite;
- **Ready** — completed result available;
- **Offline/unavailable** — distinguish from an application failure;
- **Restricted** — explain that sign-in/admin permission is required.

The shared job lock remains the safety boundary. A page must never allow a second conflicting job merely because the user navigated away. The header status and Queue/Updates notifications should reflect the same shared state rather than each page inventing a separate counter.

## 9. Data, integration, and prerequisite map

### Local persisted state

The default data directory is `%LOCALAPPDATA%\GameTransferHub` and includes:

- `gth.db` — SQLite application/library state and migrations;
- `logs\` — daily application logs;
- `sony-version-links.tsv` — saved Sony version-file links;
- `sony-links-needed.tsv` — generated missing-link work list;
- `sony-automation.txt` — update automation settings;
- `download-history.tsv` — update/download history;
- `telegram-backup-chat-id.txt` — saved Telegram destination identifier;
- `node-id.txt` — persistent enterprise-agent identity;
- `.backups\` and `.updates\` — working/temp areas;
- `restored-games\` — default restore root.

The UI must show whether a feature depends on database state, a local file, a credential, a Windows permission, or network access.

### External services

- Sony update/version/package endpoints for Updates;
- Prosperopatches/orbispatches or related title/tracker pages for guided link collection and title lookup;
- PSPrices/cover-art service through the existing poster service/cache;
- Telegram Bot API for Backups;
- remote FTP/FTPS/SFTP servers and optional local FTP/proxy listeners;
- central GTH server/agent endpoints for Enterprise.

All external-service failures should present: what was attempted, which prerequisite is missing, whether local data was changed, and the safe next action.

## 10. Proposed implementation phases

### Phase A — Language and component foundation

- approve the language set and vocabulary;
- inventory and key all visible strings;
- define shared page header, toolbar, status strip, card, table, disclosure, empty-state, and error components;
- define icon and accessibility conventions;
- add tests for route labels and localization fallback.

### Phase B — Shell and core library workflows

- redesign header/sidebar;
- redesign Dashboard, Games, JB Library, Storage, Updates, and Downloads;
- preserve existing service calls and job state;
- perform keyboard-only and resize tests.

### Phase C — Device and operations workflows

- redesign Consoles, Transfers, FTP, Network, and Backups;
- split FTP into task-focused sub-pages without changing its service behavior;
- preserve credential, firewall, checksum, and cancellation guarantees.

### Phase D — Administration and settings

- redesign Reports, Logs, Enterprise, and Settings;
- make role restrictions and firewall/credential prerequisites explicit;
- add language selector and RTL validation if approved.

### Phase E — Verification and owner review

- Debug and Release builds;
- full CTest suite;
- targeted UI interaction checks for every route and primary action;
- keyboard navigation and visible-focus review;
- dark/light and minimum supported window-size review;
- verify Desktop Release executable only after the owner accepts the visual result.

## 11. Acceptance criteria for the redesign

The redesign is complete only when:

- every current route and action listed in this proposal remains reachable;
- no existing storage, category, game, update, transfer, backup, credential, or network data is lost;
- every async job has visible progress, a safe cancel path where supported, and a final result;
- the first screen a new user sees explains what GTH manages and what action to take next;
- each page has one obvious primary action and no unexplained “More” or overloaded catch-all panel;
- the same information appears in the same visual location across pages;
- status is communicated by text plus color/icon, never color alone;
- all buttons, disclosures, menus, tables, and cards are keyboard accessible with visible focus;
- labels remain readable at supported window sizes and do not wrap unpredictably;
- the language system can add a second locale without editing view logic;
- CTest and the relevant UI tests pass, and the owner has reviewed the updated Desktop executable.

## 12. Decisions required from the owner before implementation

1. Approve the task-based navigation groups and page names, especially **JB Library**, **Operations**, and **Administration**.
2. Choose the first supported UI languages: English only initially, or English plus Persian from the first redesign pass.
3. Confirm whether the application should default to dark mode or remember the current theme exactly as it does now.
4. Confirm whether FTP should be split into internal sub-pages or remain one scrollable page with stronger sections.
5. Confirm whether the current signed-out restrictions remain unchanged.
6. Confirm whether the Updates page should use **Update library** or keep the more technical **Update all games** label.
7. Confirm the smallest window size that must remain fully usable.
8. Confirm whether export actions (reports, logs, download history) are required in the redesign or remain secondary future work.

No product code should be changed until these decisions and the acceptance criteria are approved.
