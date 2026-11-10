# Report 04 — SonarQube findings remediation

## Scope and baseline

This pass reviewed the captured Sonar issue snapshot and corrected actionable findings in the local working tree while preserving the app's existing workflows and protocol compatibility. A focused local checkpoint was created as commit `8b3c6e6`; no remote push was performed. Additional accept-loop cleanups and verification are recorded in [the IDE run log](../planning/SONAR_RUN_LOG_2026-11-07.txt).

The snapshot belongs to analysis `2680bb86-4582-43e6-9717-94f85666d053`, run on 2026-11-03 against baseline HEAD `32a32b9bab987862e7fc1089354dc0d2d1bf6eea`. It contained 153 open issues: 112 security, 38 reliability, and 22 maintainability in the dashboard's overlapping categories. The issue types in the export were 93 vulnerabilities, 19 bugs, and 41 code smells. These are baseline counts, not the count after this local change.

## Changes made

- Replaced unbounded Windows string-length checks with bounded checks and validated the Windows-directory API's returned length.
- Replaced legacy `gethostbyname` use with `getaddrinfo` while retaining the existing address-selection behavior.
- Made UI message payload ownership explicit across failed posts, worker shutdown, and queued messages. CAPTCHA click behavior itself was not changed.
- Corrected unsafe or analyzer-flagged implementation patterns: bounded socket-send lengths, constant response sizes, distinct filesystem error checks, explicit side-effect sequencing, bit-preserving storage conversion, and exception-preserving parallel storage-health work.
- Applied the configured TLS version to all FTPS operations when TLS is selected. Plain FTP remains available for compatibility.
- Removed the hardcoded first-use admin password. A first-time user now chooses a non-empty credential, and the service confirms the saved value before unlocking; existing credentials stored in Windows Credential Manager continue to work. This is the one intentional sign-in flow change required to remove the hardcoded credential. Previously stored copies of the old default are not automatically migrated, so an owner who used that default should replace it in Settings.
- Consolidated unit-test temporary paths under one unique per-run directory in the user's OS temp folder, added assertions to the relevant browser integration case, corrected browser test lint findings, and removed a shadowed duplicate browser getter.
- No additional production `.cpp` unit was deleted: the CMake inventory already found all 129 production `.cpp` files wired into module builds. Function-level dead-code detection remains for a configured C++ analyzer.

Test-only analyzer exceptions are narrowly documented in source for fixed sample IP addresses, mock URLs, and the shared OS temp-path helper. They do not disable checks across production files.

## Findings intentionally left open

- Some production network paths still support HTTP: the local proxy handles HTTP requests, Sony's legacy update/catalog service uses HTTP URLs, and imported legacy links may contain HTTP before they are upgraded. The upgrade happens before a request in the affected import path. Changing the remaining endpoint behavior without confirming service and certificate compatibility could break existing workflows.
- Optional plain FTP remains available and can transmit credentials/data without encryption. FTPS is selected when supported by the configured server.
- These protocol risks are recorded in [the security policy](../docs/SECURITY_POLICY.md). They need endpoint or product-owner confirmation before changing compatibility behavior. A fresh scan is needed to establish the exact remaining issue count.

## Verification

On Windows 10.0.19045.6466 with Visual Studio Build Tools 2022 17.14.39, MSVC 19.44.35228.0, Windows SDK 10.0.26100.0, CMake 4.4.3, and Node.js 24.21.0:

- `cmake --build build --config Debug --parallel` — passed.
- `ctest --test-dir build -C Debug --output-on-failure` — 499 passed, 6 skipped, 0 failed out of 505 registered tests.
- `cmake --build build --config Release --parallel` — passed.
- `ctest --test-dir build -C Release --output-on-failure` — 499 passed, 6 skipped, 0 failed out of 505 registered tests.
- `npm test` in `tests/browser-automation` — 12 passed.
- `npm run test:integration` in `tests/browser-automation` — 7 passed.
- `git diff --check` — passed.

The six skipped CTest cases are three Credential Manager checks unsupported by the current logon and three existing optional live drive/network checks. Debug linking still reports `LNK4098`; Release still reports the documented C4244 conversion in `src/captcha/SonyLinkCopyWatcher.cpp:441`.

## Scan refresh status

The local SonarQube Server responds as version 2026.5.3.133540 with status `UP`. Its API rejects anonymous requests with HTTP 401. `sonar-scanner` is not installed on PATH, and no `SONAR_TOKEN` is configured in this environment. Therefore, this modified working tree has **not** been rescanned, and the baseline counts above must not be treated as a post-fix result. The next step is to configure an authenticated scanner locally, run CFamily analysis against this tree, and review any newly reported issues. Keep the token out of chat and Git.

## 2026-11-07 SonarQube for IDE pass and follow-up

The corrected SonarQube for IDE pass completed in VS Code against the open Desktop workspace. The IDE reached 1,761 of 1,761 Sonar findings across 177 files; VS Code showed 1,763 Problems including two C/C++ IntelliSense configuration errors. The run log in planning/SONAR_RUN_LOG_2026-11-07.txt records progress snapshots and the captured findings.

The configured compilation database is stored in this checkout, but its source-file entries point to the Desktop copy of the project. The Desktop workspace and versioned checkout are not byte-identical. The initial checkpoint addressed low-risk findings in WinsockCachingProxyServer.cpp, and a follow-up addressed its accept-loop nesting, fixed-size buffer, worker type, cacheDir parameter passing, and redundant type. The const_cast finding was reviewed against the BCryptHashData input-only contract and retained. The best-effort connection logging catches remain in place so logging failures cannot interrupt proxy handling; comments now explain why their bodies are intentionally empty.

Verification in the versioned checkout:
- Debug build of gth_acceleratorserver passed.
- Debug build of gth_unit_tests passed.
- 35 proxy and local-game-server tests passed using the focused GoogleTest filter.
- The full GoogleTest suite then passed: 512 tests across 88 suites, 506 passed and 6 skipped.
- Focused git diff --check passed.
- A separate wrapper test build failed before the changed target because its MSVC 14.51 invocation could not locate the standard header string. The normal Visual Studio 2022 build succeeded and the relevant tests were run directly.
- The IDE scan has not been rerun. The 1,761 count remains the completed Desktop scan baseline, not a post-fix result. The scan exposed only part of its issue list, so additional findings still need to be enumerated and triaged.
- The follow-up accept-loop changes passed Debug builds for `gth_acceleratorserver` and `gth_unit_tests`, the 35 focused proxy tests, and all 512 unit tests (506 passed, 6 skipped). A full Debug build compiled the app sources but its final link failed with LNK1168 because the running `GameTransferHub.exe` was locked; the app was left running. No refreshed Sonar analysis was run.

The next visible IDE group contained 17 findings; eight rows were exposed and matched to `src/app/src/StorageView.cpp` by their messages and source locations. Those eight were addressed in the local checkout with equivalent optional checks, rating branches, formatting, and fixed-size `std::array` buffers. The other nine rows in that group and the rest of the project findings are not yet enumerated. The run log records the individual visible rows and current status. Across the documented proxy and StorageView work, 23 distinct visible findings have a code disposition; the requested 80% goal is resolving at least 1,409 of the 1,761 baseline findings, leaving no more than 352 open. That goal has not been reached. The Debug app build compiled the changed StorageView source but could not replace the running executable (LNK1168). No Sonar re-analysis was run.

The IDE's Explorer initially showed eight problems in `BackupsView.cpp`, but their diagnostic text was unavailable. Two same-pattern fixed destination buffers were converted to `std::array` based on the confirmed `cpp:S5945` pattern from StorageView. After the file rechecked, its visible count fell from 8 to 6 and the observed IDE total fell from 1,761 to 1,759. This confirms two fewer live findings, though the individual BackupsView rows were not captured. Later source edits are tracked separately until their issue counts refresh.

The same IDE-visible rule families also motivated a behavior-preserving source pass: 22 optional-presence checks now use `has_value()`, and nested conditional expressions in package selection, transfer status, health scoring, and native control color selection were expressed as ordered branches or an equivalent boolean condition. Since their exact rows were not captured, these are tracked as candidate remediations rather than verified Sonar findings. Debug unit-test build passed and CTest completed with 506 passed, 6 skipped, and 0 failed (512 total). The app sources compiled, but final linking remained blocked by the already-running app executable (LNK1168). No Sonar re-analysis was run.

The next behavior-preserving candidate batch changed 13 message-formatting expressions in `PackageSyncService.cpp` to `std::format`, matching the `cpp:S6185` family directly observed in StorageView. The exact IDE rows for these locations were unavailable, so they remain candidate remediations and are excluded from the count of 23 confirmed IDE rows. The application module and unit test executable built successfully; all 25 `PackageSyncServiceTest` cases passed, and focused `git diff --check` passed. The original run logs confirm the finished 1,761-finding snapshot but do not expose individual issue details. The IDE cache remains locked, and the IDE window could not be activated for paging; the 80% goal (resolve at least 1,409 findings, leave at most 352) remains open. No Sonar re-analysis was run.

A second candidate batch replaced 13 fixed byte buffers in `PkgFileParser.cpp` with same-size `std::array` instances and `.data()`/`.size()` at the parsing boundaries. All 15 `PkgFileParserTest` cases passed after rebuilding `gth_unit_tests`; focused diff checking passed. This follows the C-style buffer rule shown in the IDE, but the actual parser issue rows were not available, so the 13 locations remain candidates and are excluded from the 23 confirmed IDE rows.

The three fixed text buffers in `Win32NetworkInterfaceDetector.cpp` were converted to same-size `std::array` storage, preserving IP/MAC formatting and Win32 output capacities. The network module rebuilt successfully and all 5 focused network-interface tests passed. This is a candidate match to the visible C-style buffer rule and remains excluded from the 23 confirmed IDE-row count.

The Sony automation formatter now uses same-size `std::array` storage for its time and size buffers, and its duration strings use equivalent `std::format` expressions. The application library and test executable rebuilt successfully; all 8 Sony automation tests passed. These are candidate rule-family matches only and remain outside the confirmed IDE-row count.

Two more candidate buffer cleanups replaced fixed arrays in `SonyLinkCopyWatcher.cpp` and `LocalFileTransferProvider.cpp` with same-size `std::array` storage. The captcha library built (with its existing C4244 warning on the unrelated ASCII-only ID conversion), the unit test target built, and all 4 local file transfer tests passed. Individual IDE rows were not captured, so these remain candidates and do not change the confirmed issue count.

The SFTP directory/transfer buffers and remote report acknowledgement buffer were changed to same-size `std::array` storage. `gth_ftp` and the unit test executable built successfully. The two `CentralServerListenerTest` cases passed, exercising the `TcpRemoteReportSender` acknowledgement path. The exact IDE issue rows were unavailable, so these four candidate locations are excluded from the confirmed issue count.

Nine storage health reason strings now use equivalent `std::format` calls. The unit test target rebuilt, and all 14 focused SMART/NVMe health and reason-summary tests passed. Because the individual IDE rows were not captured, these are candidate `cpp:S6185` matches and are not added to the 23 confirmed issue rows.

Four Sony piece downloader diagnostics/messages now use equivalent `std::format` expressions. The unit test executable rebuilt, both `SonyPieceDownloaderTest` cases passed, and focused diff checking passed. These are candidate matches only; the confirmed IDE-row count remains unchanged.

Four Sony update service text expressions now use equivalent `std::format` calls. The unit test target rebuilt and all 14 `SonyUpdateServiceTest` cases passed. These remain candidate matches, not confirmed IDE rows.

Backup temporary chunk names, object IDs, and chunk status text now use `std::format` with unchanged decimal values and wording. The application module and unit test target rebuilt; all 4 `BackupServiceTest` cases passed. These are candidate matches only and do not alter the confirmed IDE-row count.

The size mismatch text in `FileVerifier.cpp` now uses an equivalent `std::format` expression. The unit test target rebuilt, all 11 `FileVerifierTest` cases passed, and focused diff checking passed. This is a candidate only; the confirmed IDE count is unchanged.

Five numeric expressions in archive updates, download failures, title lookup diagnostics, and migration version updates now use `std::format`. The unit test target rebuilt; 24 relevant archive, download, and lookup tests passed, and the archive tests exercised database migration setup. These are candidate matches only and do not alter the confirmed IDE-row count.

The shared Sony HTTP helper now uses `std::format` for two diagnostics and a same-size `std::array` for its 32-byte certificate digest output. The network module and unit tests rebuilt; all 16 Sony update/download tests passed. These are candidate matches only and do not change the confirmed issue count.

### 2026-11-08 follow-up — fixed-buffer cleanup and live count

The user asked whether the problem total should decrease during remediation. Yes: a resolved finding should disappear after SonarQube for IDE rechecks that file. The only verified live decrease at this checkpoint is the observed 1,761-to-1,759 drop after the two BackupsView buffer changes. The latest VS Code title still reports 80 problems for the currently open, unsaved `WinsockCachingProxyServer.cpp`; that file was left untouched. No project-wide Sonar analysis was restarted.

Applied the same-capacity `std::array` conversion to 12 fixed buffers across `DownloadsView.cpp`, `LogsView.cpp`, `MainFrame.cpp`, `SplashWindow.h`, `Theme.h`, `PackageSyncService.cpp`, `UpdateCheckService.cpp`, `ServiceContainer.cpp`, `LocalGameServer.cpp`, and `SonyUpdateService.cpp`. Buffer lengths and Win32/Crypto API capacities remain unchanged. These are candidate `cpp:S5945` remediations until the IDE refreshes their individual issue counts; do not add them to the verified reduction yet.

Verification for the local checkout:
- Debug application target built to a separate output directory, leaving the running app executable untouched.
- Focused package, Sony update, and local-server tests: 80 passed.
- Full unit CTest run: 506 passed, 6 skipped, 0 failed (512 total). One existing long-running scan test reported 57,546.58 seconds, so this full suite should not be repeated without need.
- `git diff --check` — passed after the final log update; Git reported only line-ending normalization notices for the two text reports. No new Sonar run was performed.

### 2026-11-08 continued IDE remediation

Work resumed against the already-running Desktop SonarQube for IDE workspace. Behavior-preserving formatting and conditional cleanups were applied to health reporting, Sony/update messages, migration version SQL, firewall guidance, Storage page text, and update/transfer outcome mapping. A `std::array` call-site error found by compiling the Desktop source was corrected by passing `.data()` to `DrawTextW`. The relevant code was also updated in the versioned checkout where it matched. The changes preserve message wording/values, health thresholds, result priority, and rendered button colors.

The exact Desktop source configured and built successfully with Visual Studio 2022 and cached dependencies. The versioned checkout's Debug app and unit-test targets also built successfully to a separate output directory. Focused filters passed: 136 tests across 17 suites before the final status refactors, 58 package-sync/transfer-queue tests after them, 17 storage-health threshold tests, 20 storage/health service tests, and 12 archive/firewall tests. These filters overlap. Focused diff checking passed. Existing `LNK4098` warnings remain.

No Sonar analysis was restarted or refreshed, so these are candidate fixes and the confirmed issue total remains unverified. The owner first reported 1,714 and later 1,516 problems; an earlier agent capture was 1,658. The latest user-reported count is 1,516, but it is not independently verified. The 80% goal remains open: resolve at least 1,409 of the 1,761 baseline findings, leaving at most 352. Keep the existing unsaved proxy source unchanged and use the detailed [IDE run log](../planning/SONAR_RUN_LOG_2026-11-07.txt) for ongoing file-by-file triage.

### 2026-11-08 continued source cleanup

The owner reported a current IDE total of 1,516 problems. This is 198 fewer than the earlier owner-reported 1,714 and 245 fewer than the 1,761 initial baseline. The current count after the latest source edits has not been read. The requested 80% target means resolving at least 1,409 baseline findings and leaving no more than 352 open.

Continued equivalent `std::format` replacements in Sony update and Updates-page status text, bounded Windows Credential Manager username length before conversion, and corrected stale Desktop sidebar test indices without changing app behavior. The full Desktop Debug app and unit-test targets built. CTest completed 503/503 with 6 expected environment/live-service skips; all other tests passed. No new Sonar analysis was run. Details are in the [IDE run log](../planning/SONAR_RUN_LOG_2026-11-07.txt).

The 2026-11-08 follow-up applied additional verified optional-presence cleanups in package parsing, storage indexing, update services, credential and FTP paths, backup UI state, and Sony/Telegram parsing. Telegram's nested hex-digit conditional was rewritten as ordered branches with the same mapping. The app and unit-test targets built successfully in a separate output directory; focused test runs passed 141/141 and 113/113 cases respectively (overlapping suites). `git diff --check -- src` passed with only existing line-ending normalization notices. The IDE finding list was not refreshed, so these remain candidate fixes rather than confirmed cleared findings, and the 1,516 owner-reported count remains the last available total. See the [detailed run log](../planning/SONAR_RUN_LOG_2026-11-07.txt).
To ensure the active IDE workspace received the fixes, 69 exact optional-presence replacements were applied to 24 matching files in the Desktop source copy, plus three safe optional checks in its older `AdminAuthService.cpp`. The known unsaved tabs were left untouched. The Desktop Debug app and unit-test targets built; 212 focused tests across 22 suites passed, and the 8 AdminAuth tests passed again after its final edits. A local Debug app build passed after a separate page-label conditional cleanup. The IDE session was not restarted, and its live issue total has not been captured; these edits are not counted as resolved until the IDE refreshes. The 80% target remains open. See the [run log](../planning/SONAR_RUN_LOG_2026-11-07.txt).
A final pattern sweep found and fixed one more optional-presence check in `CurrentVersion.h`; its dependent GameIndexer, package sync, and update service tests passed 77/77. A separate local Enterprise page label conditional was simplified while preserving its labels and short-circuit order; the local app target built. The remaining matching fixed arrays and numeric concatenations in the Desktop tree are inside the unsaved proxy source, which remains untouched. The IDE count is still unverified, and no new full scan was started.
### 2026-11-08 — additional behavior-preserving cleanup

Refactored the Windows storage-health WMI path into flatter helpers while retaining query order, COM lifetimes, fallback behavior, and output. The IDE's visible finding count for `WindowsStorageHealthProvider.cpp` dropped from 36 to 30 after reanalysis. Converted 23 thread declarations/constructions whose lifecycles already explicitly join to `std::jthread` in both source copies; the detached storage probes still detach immediately. No unsaved proxy/BackupsView files or CAPTCHA code were changed.

The IDE subsequently analyzed six modified files and logged 120 findings across them; this is not the current project-wide total. The latest owner-reported total before these edits remains 1,512. No new full scan was started. Versioned and Desktop Debug app/test builds completed; the focused thread-service and FTP checks passed 15/15 in each checkout. Storage-health tests passed 28 with two expected environment skips in each checkout, and `git diff --check` passed. The separate-output versioned test run had failures; the normal-output rebuild and isolated rerun passed, as recorded in the [run log](../planning/SONAR_RUN_LOG_2026-11-07.txt).
### 2026-11-08 — refreshed panel count

After the six-file incremental analysis, a read-only VS Code snapshot showed 1,513 Problems total. The Sonar filter showed 1,512 rows; the status bar showed 2 errors and 1,511 warnings. The IDE log's 120 findings were the current per-file results across six analyzed files, not an increase of 120 in the project total. This live total remains above the 80% target (no more than 352 open findings). No full scan was restarted.
### 2026-11-08 — owner-reported VS Code count

The owner reports 1,492 total VS Code Problems, 20 fewer than the prior owner-reported 1,512. The last direct accessibility snapshot showed 1,513 total Problems, including 1,512 Sonar-filtered rows; the current filtered Sonar subtotal has not been read separately. This indicates progress, but does not identify which edits cleared which rows. No full analysis was restarted. The 80% target remains at no more than 352 open findings against the 1,761 baseline.

### 2026-11-08 — Winsock initialization and fixed replies

Applied the observed Sonar recommendation to keep each temporary WSADATA local inside its if initializer in CentralServerListener, TcpReachabilityChecker, TcpRemoteReportSender, and WinsockFtpServer. In CentralServerListener, also replaced strlen on two fixed protocol replies with compile-time array lengths and removed the unused <cstring> include. The exact response bytes and WSAStartup result handling are unchanged. Applied the same relevant edits to the IDE-scanned Desktop copy; its NetworkInfoHelper header received only the matching scope change.

IDE log evidence after the Desktop edits:
- CentralServerListener.cpp reported 13 findings after its recheck, down from 14 after the preceding reply-length cleanup (and 16 in its earlier file decoration).
- TcpReachabilityChecker.cpp reported 6 findings; no pre-change per-file count is available.
- The two-file recheck for WinsockFtpServer.cpp and TcpRemoteReportSender.cpp reported 32 findings combined; this is not a project total.

Verification:
- Versioned Debug gth_unit_tests build passed; focused CentralServerListener, WinsockFtpServer, TcpReachabilityChecker, and AgentService tests passed 10/10.
- Desktop-source Debug app and unit-test targets built through the separate desktop-sonar-verification-offline build; the same focused tests passed 10/10.
- git diff --check passed on the changed versioned source files.
- No full project analysis was restarted. The owner reports 1,492 total VS Code Problems; the read-only accessibility snapshot still shows the older 1,513 total / 1,512 Sonar-filtered count, so the current project-wide Sonar subtotal remains unverified.

### 2026-11-08 — ICMP and storage byte-buffer cleanup

In IcmpHostPinger, replaced the static payload with a mutable std::array created from the same string literal, preserving its terminating null byte and request length. Changed the reply buffer to std::byte storage and copied the ICMP_ECHO_REPLY into a local struct with memcpy instead of reading it through a reinterpret_cast. The IDE recheck reports 4 findings in this file; its earlier Explorer decoration showed 5.

Changed the Windows volume-extents buffer from unsigned-char storage to std::byte storage in both source copies. The IDE recheck for WindowsStorageHealthProvider.cpp remains at 30 findings, the same as the prior recheck; this candidate change is not counted as a resolved row.

Verification:
- Versioned Debug unit-test target built successfully; Debug app target linked successfully to the separate verification-2026-11-08-fast-batch output after the normal output was blocked by the already-running app.
- Desktop-source Debug app and test targets built successfully through desktop-sonar-verification-offline.
- Focused storage-health tests passed 22/24, with 2 expected environment skips, in both builds.
- Earlier focused network tests passed 10/10 in both builds after the Winsock and listener changes. There is no direct IcmpHostPinger test in the suite.
- No full Sonar analysis was restarted. The owner-reported total remains 1,492; the open accessibility snapshot still shows its earlier 1,513 total / 1,512 Sonar-filtered rows.

### 2026-11-08 — network-interface conversion buffer

The network-interface UTF-8 conversion now allocates the NUL-inclusive length required by `WideCharToMultiByte`, then removes the terminator before returning the same string value. Desktop app/test targets and the versioned test target built; the versioned app's normal link output was locked by the running app. Focused network-interface tests passed 5/5 in each build. Sonar's per-file refresh remained at 11 findings, so no Sonar reduction is attributed to this bounds correction. See the [run log](../planning/SONAR_RUN_LOG_2026-11-07.txt).

At the owner's request, SonarQube for IDE automatic analysis was turned off in the VS Code user settings. The visible proxy problem text was saved in the run log and the versioned proxy source received matching safe cleanups for its worker type, fixed-size buffers, Winsock initialization, address copy, and redundant type. The open Desktop proxy and backup tabs contain unsaved edits and were not overwritten. The accelerator-server and unit-test targets built, the app linked to a separate Debug output path without replacing the running copy, and 35 focused proxy/local-server tests passed. No post-change Sonar total is available or claimed.

### StorageView verification — 2026-11-08

The eight captured `StorageView.cpp` Sonar rows are present as behavior-preserving source changes. Full view review found no additional definite behavior defect. The app and unit-test targets built to a separate Debug output directory, all 19 focused storage/category tests passed, and `git diff --check` passed. Nine other rows in the IDE's 17-finding group were not captured with file/message details and remain unattributed; Sonar was not rerun.

### Saved IDE rows — StorageView and SettingsView (2026-11-08)

Read the cached VS Code diagnostics without restarting analysis. StorageView's one remaining row was replaced by C++20 `std::bit_cast`; its earlier eight captured rows remain addressed. SettingsView's nine saved rows were checked against the current source: exact `ShlObj.h` casing, C++20 bit-casts and `auto` at the Win32 message boundary, `using enum`, `contains`, fixed-size `std::array`, and specific `std::stoi` exception handling. Both versioned and Desktop source copies built. Focused tests passed 28/28 in the versioned build and 25/25 available tests in the Desktop-source build. No firewall handler or networking module changed. The IDE's saved 1,485 total (1,484 filtered) is pre-fix; automatic analysis remains disabled and no updated count is claimed.

### ReportsView saved rows — 2026-11-08

The three saved ReportsView rows were handled with `auto` and C++20 `std::bit_cast` at the WM_CTLCOLORSTATIC boundary in both source copies. Rendering and report data remain unchanged. The versioned and Desktop-source builds passed; `ReportServiceTest.EmptyDatabaseReportsAllZeros` passed in each. Sonar was not rerun.

### ComingSoonView saved rows — 2026-11-08

The three saved findings were addressed in both source copies with `auto` and C++20 `std::bit_cast` at the Win32 message boundary. Versioned and Desktop-source app targets built successfully; the view has no direct unit test. No Sonar analysis was restarted.

### WinMain saved row — 2026-11-08

Corrected the SDK include to match `CommCtrl.h` in both source copies. Startup behavior is unchanged. Both Debug app targets built successfully; no direct unit test applies. Sonar was not rerun.

### Logging and DiskStatus saved rows — 2026-11-08

`Logging.cpp` now keeps its shared logger pointer in function-local static storage while preserving the logger's type, configuration, lifetime, and default-logger registration. `DiskStatus.cpp` uses function-local `using enum` declarations to remove redundant enum qualification; conversions and fallback strings are unchanged. The matching DiskStatus edit was made in the versioned and Desktop copies.

Versioned and Desktop-source Debug app and unit-test targets built successfully. No direct unit tests cover these two conversion functions or logger access; the test executables were rebuilt but not run. `git diff --check` passed for touched versioned files, and the two DiskStatus sources match after normalizing line endings. Automatic analysis remains disabled; the existing 1,485 / 1,484 IDE display is stale and no updated total is claimed.

### AdminAuthService saved row — 2026-11-08

The Desktop row flagged a hard-coded default password. The versioned implementation already complies with the security policy: users choose the first non-empty credential and it is stored and read back from Windows Credential Manager before sign-in succeeds. The Desktop source/header and regression tests are now aligned to that implementation; credential verification and change flows remain covered. No network code changed.

Both versioned and clean Desktop Debug app/unit-test builds succeeded. The focused `AdminAuthServiceTest.*` suite passed 10/10 in each build. The Desktop clean build reports an existing LNK4098 warning. An earlier custom-output build linked a stale library and failed 7/10, so it was discarded in favor of the clean native-output rebuild. Automatic analysis was not rerun and the old IDE total is not a post-fix count.

### AgentService saved rows — 2026-11-08

Explicitly qualified the destructor's call to `AgentService::stop` and changed the `std::unique_lock<std::mutex>` declaration to CTAD in both source copies. This affects only local dispatch clarity and lock type spelling; reporting behavior and all network details are unchanged. Both Debug app/test builds passed, and `AgentServiceTest.*` passed 2/2 in each. The tests communicate only over their local loopback endpoint. No Sonar rerun or project count update was made.

### GamesView owned-message cleanup — 2026-11-08

The lookup-result message in `GamesView` now uses the existing unique-ownership helper on both posting and handling. Shutdown drains any queued result after joining the worker. The successful request/result flow is unchanged; the change only closes payload leaks on post failure or window shutdown.

Both Debug app and unit-test targets built in the versioned checkout and Desktop IDE workspace. Versioned CTest recorded 506 passes, 0 failures, and 6 skips. Desktop CTest recorded 504 passes, 6 skips, and one failure: `CentralServerListenerTest.InvokesHandlerWithParsedFieldsAndAcks`; a standalone retry reproduced the local send failure. No networking code was changed. The VS Code panel remains at 643 total (2 errors, 641 warnings) with 178 files still analyzing, so current workspace findings remain unsettled.

### App UI Win32 representation casts — 2026-11-08

In `WtlCommon.h`, `Theme.h`, `LogsView.cpp`, and `MainFrame.cpp`, replaced Win32 message-value/handle `reinterpret_cast` operations with `std::bit_cast` for same-sized representations. Drawing and UI behavior remain unchanged. The edits were applied to the Desktop workspace and versioned checkout; FTP, proxy, and network modules were left untouched.

Both Debug app targets rebuilt successfully. No tests were rerun for these representation-only conversions; the latest full versioned run passed 506 tests with 6 skips. Desktop testing still reproduces the separate `CentralServerListenerTest.InvokesHandlerWithParsedFieldsAndAcks` local-send failure. The last IDE snapshot before this batch showed 643 findings and 178 files still analyzing; there is no post-batch count yet.

Follow-up: `UpdatesView.cpp` also converts its static cue-banner pointer to `LPARAM` with `std::bit_cast`. Both Debug app targets rebuilt successfully. A fresh VS Code snapshot remains at 643 (2 errors, 641 warnings) with 178 files analyzing, so the IDE has not yet provided a settled post-edit count; none of these candidate remediations is claimed as confirmed by Sonar.

### Sonar IDE log reconciliation — 2026-11-08

The local extension log confirms that the full Automatic Analysis completed at 19:22:14 with 1,461 findings across 178 files and 0 Security Hotspots. Later individual-file analyses logged 34 for `UpdatesView.cpp`, 5 for `GamesView.cpp`, 2 for `LogsView.cpp`, and 8 for `MainFrame.cpp`; these are per-file counts, not fresh workspace totals. The live Problems panel separately shows 643 (2 errors, 641 warnings) and “Analyzing 178 files.” The counts disagree, so neither is claimed as a settled post-edit total. Recent edits made after the last per-file records have not yet been confirmed by the IDE, and no full analysis was restarted.

### Remaining saved UI message conversions — 2026-11-08

Replaced the remaining app-view Win32 message/handle `reinterpret_cast` operations in `ComingSoonView.cpp`, `ReportsView.cpp`, `SettingsView.cpp`, and StorageView cue banners with `std::bit_cast` in both source copies. Existing brush handles, placeholder text, and control behavior are unchanged. Desktop and versioned Debug app targets built successfully, and `git diff --check` passed. No Sonar reduction is claimed because the latest per-file extension logs predate these edits. The live Problems panel shows 643 while the completed full scan log shows 1,461; further per-file refresh is needed to reconcile them. VS Code screenshot/filter interaction is currently failing, so the remaining rows are not visible to me.
