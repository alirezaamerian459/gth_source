# Project status

**Last updated:** 2026-11-11

**Repository baseline:** branch `main` at `32a32b9` tracks `origin/main`; work proceeds on local branch `codex/gth-local-work`.

**Current phase:** Phase 06 — Page-level usability. Phase 05 navigation is implemented locally; Phase 04 is complete. Phase 02 CI and the phase 03 security review remain open.

## Current state

- The local clone retains the original Git history and origin remote.
- The root README and AGENTS.md now point to one current documentation set under docs/.
- Stale and conflicting root guides were removed from the working tree; Git history retains their prior versions.
- Architecture, development, database, browser-test, and installer guidance was consolidated or corrected.
- Three unreferenced network helper pairs for user-agent rotation, behavioral timing, and cookie persistence were removed after source search found no call sites outside those files and their CMake entries. No application feature behavior was intentionally changed.
- The broken build/release batch scripts and machine-specific launcher were removed. The broken source-archive script was replaced by scripts/package_source.py; it has not been executed.
- A clean x64 Debug configure/build and a Release build completed with Visual Studio Build Tools 2022. After the local Sonar remediation changes, CTest completed in both configurations with 499 passed, 6 skipped, and 0 failed out of 505 registered tests.
- The separate browser automation suite passed 12/12 unit tests and 7/7 integration tests. Its invalid `dotenv` version range was corrected from `^21.4.0` to `^17.4.2`; an npm lockfile is now present in the working tree for reproducible dependency resolution.
- The CMake inventory found all 129 production `.cpp` files referenced by module CMake files. No additional clearly unwired implementation file was found to delete; function-level reachability still needs a configured C++ analyzer.
- The Sonar issue snapshot for analysis `2680bb86-4582-43e6-9717-94f85666d053` (HEAD `32a32b9bab987862e7fc1089354dc0d2d1bf6eea`, 2026-11-03) contained 153 open findings. The reported dashboard categories were 112 security, 38 reliability, and 22 maintainability; categories overlap. The main actionable rule groups were reviewed and fixes are described in [report 04](../reports/04-sonarqube-remediation.md).
- The local SonarQube Server 2026.5.3.133540 responds with status UP, but anonymous API access returns HTTP 401, `sonar-scanner` is not installed, and no `SONAR_TOKEN` is configured in this environment. Therefore no new scan of the modified working tree has been submitted; the original issue totals are not a post-fix count. The current desktop distribution includes the CFamily analyzer, but the scan still needs an authenticated scanner setup.
- Debug linking emitted `LNK4098` CRT mismatch warnings for the app and unit-test executable. Release compilation emitted C4244 at `src/captcha/SonyLinkCopyWatcher.cpp:441`; the narrowing is documented as safe for the validated ASCII title ID. CAPTCHA-related code remains unchanged pending the owner scope decision.
- No CI workflow is present; phase 02 plans to establish a Windows build/test baseline before adding CI.
- Phase 04 is implemented: custom-named Storage-tab categories persist in SQLite, any storage can belong to multiple categories, and deleting a category preserves its storages and indexed games. The Windows Release build completed and CTest reported 503 passed, 6 skipped, and 0 failed out of 509 tests (14.23 seconds). Exact commands and skipped tests are in [phase 04 tasks](phases/04-storage-categories/tasks.md).
- Phase 05 is implemented locally: the shared page header now contains a hamburger navigation toggle, the sidebar shows expandable task groups, each page has a clearer header hint, and page views reflow when navigation is hidden. Route indices and existing page actions remain unchanged. The Release app and unit-test targets built successfully, and CTest completed with 506 passed, 6 skipped, and 0 failed out of 512 tests. Three attempts to launch the rebuilt app returned Windows error `0xC0000142`; it is not open for owner review. See [phase 05 tasks](phases/05-app-wide-navigation/tasks.md) for exact commands and logs.
- The owner resumed SonarQube for IDE remediation on 2026-11-08. The existing analysis session was left open; no scan was restarted. The completed baseline was 1,761 findings; later observations were 1,658 (earlier agent capture), then 1,714, 1,516, and 1,512 (owner-reported). The latest live IDE snapshot is 1,513 VS Code Problems; the Sonar filter shows 1,512 rows (the status bar shows 2 errors and 1,511 warnings). To meet the requested 80% goal against the original baseline, resolve at least 1,409 findings and leave no more than 352 open. Detailed fixes, checks, and limitations are in [the IDE run log](SONAR_RUN_LOG_2026-11-07.txt).
- Changes are local and uncommitted. No remote push or release was performed.

## Verified repository observations

- Root CMake requires Windows and C++20; GTH_BUILD_TESTS defaults to ON.
- The app target sets its output name to GameTransferHub; the NSIS script now points to the matching Release output path.
- SQLite migrations are defined in src/database/src/Migrator.cpp and currently list versions 1 through 16.
- The browser test project is separate from CTest and uses Node.js/Playwright with a configured host allowlist.
- The verified toolchain, test totals, and commands are recorded in [development guidance](../docs/DEVELOPMENT.md) and [report 03](../reports/03-code-audit-and-test-results.md).

## Open requirements

- Confirm supported console generations and package/archive formats.
- Confirm the production role, if any, for CAPTCHA-related code and clarify permitted external-service interactions.
- Refresh the SonarQube CFamily scan against the current local working tree after configuring an authenticated scanner for the running server.
- Complete owner review of the Phase 06 page-level usability changes.

## Next actions

1. Add the planned Windows CI workflow for phase 02 after the local baseline is reviewed.
2. Review production external-service and credential data flows in phase 03; do not change CAPTCHA-related code before resolving its scope questions.
3. Continue the existing IDE issue remediation and reconcile the live count when the VS Code findings view is accessible; keep the separate authenticated server scan deferred.
4. Collect the owner's Phase 06 visual feedback before closing the usability phase.

## Phase 06 — Page-level usability

- The UI/UX Pro Max skill is installed locally. Its guidance was adapted for GTH's native Win32/WTL desktop UI: clearer task hierarchy, progressive disclosure, concise labels, and keyboard-accessible foldouts.
- The Storage page now presents each storage location as a selectable card, with capacity usage and health grouped alongside the physical disk's model, size, health, and temperature. Category service calls and stored data remain unchanged. Debug and Release app builds succeeded, and the updated Desktop executable is installed.
- Storage health now preserves the QNAP/ATA SMART counters used by per-bay reports (power-on time, cycles, sector/error counters, and vendor-unit LBA counters), parses the supplied QNAP text layout, and shows the extra metrics only in the existing health-details disclosure. Reachable NAS/SMB capacity/free space is refreshed during rescans without changing local-drive reconciliation.
- External-drive support now recognizes the exFAT `GAME` JB layout and `.exfat` game files in both the JB page and the shared FTP/proxy catalog. Storage health also falls back to Windows' read-only storage-temperature property when a USB/SATA bridge exposes a sensor but does not pass ATA SMART through; unavailable readings remain unknown.
- Poster lookup now falls back to cleaned game names when a JB folder or archive has no title ID. PSPrices requests use stable title/platform cache keys, so posters are fetched for all supported file formats without changing scan or transfer behavior.
- JB discovery now includes reachable NAS/SMB/UNC share roots, including UNC directory shortcuts on the Desktop such as `Public (NAS...)`; the target selector and shared catalog can scan those roots without moving or modifying files.
- The Storage header no longer shows the Manage categories button, reducing primary-action clutter while leaving category filtering and service data intact. Debug and Release app targets build successfully after this UI-only adjustment.
- The Storage page now scans automatically on open, removes manual storage entry, and presents one category box per saved category with nested HDD cards. Category creation is a compact header action; right-clicking a category box adds or removes storages or deletes the category. Debug and Release app targets build successfully; the updated Desktop executable is installed.
- Category boxes now use the native listbox multi-column layout, placing each category as a true side-by-side column with horizontal scrolling when needed. HDD cards remain stacked inside each category. Debug and Release targets passed, and the Desktop executable was refreshed.
- The category board now uses a dedicated native horizontally scrollable canvas instead of listbox column flow, so category boxes remain side by side even when their HDD content is taller than the window. Debug and Release targets passed, and the Desktop executable was refreshed.
- The Storage page now adds a compact overview strip and a responsive two-column category grid inspired by the approved reference. Each category remains a distinct card with nested drive cards, while the board keeps horizontal/vertical scrolling for narrower windows. Debug and Release app targets passed; the Desktop executable was refreshed.
- The Updates page now uses a responsive native card canvas with library metrics, status badges, title/version metadata, filtering, keyboard selection, and double-click sync. Posters are requested through the existing background PSPrices cache and update actions still route through the existing hidden selection model. Debug and Release app targets passed, and the five non-live PSPrices parser tests passed. The stale Desktop copy was replaced with the verified Release build after stopping two hidden old GTH processes; the source and Desktop hashes now match.
- Updates text density was reduced: the status area is one compact line, automation and selected-game summaries use short labels, and each card keeps only title, platform, status, ID, version, and its primary action. Debug and Release builds passed, and the Desktop executable was refreshed with the matching Release build.
- The Updates page no longer shows a permanent More button; secondary Sony-link, import, tracker, and guide actions remain available from the updates grid's keyboard/right-click context menu. A 15-minute idle poll keeps update results current, and the shared header now shows a bell-style Updates notification button with the hamburger navigation control beside it. Debug and Release app targets passed; the Desktop executable was refreshed after verification.
- Update All now automatically starts the existing human-in-the-loop Prosperopatches collector when PS5 links are missing, resets and writes `Desktop/GTH-Prosperopatches-links.txt` as links are learned, and continues the existing one-game-at-a-time package sync. PackageSync still places verified updates in the game-specific directory/Updates folder. Debug and Release app targets passed; the seven SonyLinkImporter tests passed; and the Desktop executable was refreshed.
- Secondary controls are now collapsed by default on Storage, Updates, Backups, Settings, FTP, Transfers, Consoles, Enterprise, and Dashboard. Pages already focused around one task (Games, JB Games, Downloads, Network, Reports, Logs, and Coming Soon) did not need extra foldouts.
- The JB catalog is now shareable through both FTP and the LAN Console Proxy. The FTP host keeps its chosen shared folder and optionally adds a read-only `/JB Library` virtual tree; the proxy serves exact JB package/archive matches after the normal library lookup. Existing FTP task paths and origin fallback remain unchanged. Debug builds and focused FTP/proxy tests passed; see [JB sharing verification](JB_FTP_PROXY_SHARING_2026-11-11.md) and [ADR-0004](../docs/decisions/0004-jb-library-sharing.md).
- Existing app processes remain open. The normal Release output could not be relinked while locked, so a staged Release build was produced at `build/storage-release-staged/GameTransferHub.exe`; the Debug app target also built successfully.
- Automated tests were not run for this layout-only pass. Visual review of the Debug app is pending.

## SonarQube follow-up — 2026-11-08

Continued safe, behavior-preserving remediation for confirmed optional-presence and nested-conditional rule families. The Debug app and unit-test targets built in a separate output directory; focused test runs passed 141 and 113 cases (overlapping selections). The IDE has not refreshed the findings, so there is no updated issue count or 80% completion claim. Details are in [the IDE run log](SONAR_RUN_LOG_2026-11-07.txt).
The matching fixes have now been applied to the Desktop source tree that the IDE scans, using exact text substitutions and leaving the previously identified unsaved tabs unchanged. The Desktop app/test build passed and 212 focused tests passed; 8 AdminAuth tests passed after a final follow-up. The IDE has not been restarted, and its updated problem count has not been read. Do not treat the 80% goal as complete until the refreshed total confirms no more than 352 open findings.
The Desktop source pattern sweep found one more optional check in `CurrentVersion.h`; after changing it, five focused suites passed 77/77. The local app also rebuilt after a page-label branch cleanup. Remaining pattern matches are in the unsaved proxy source or are not applicable to the confirmed rule. The IDE count has not been captured; the 80% goal remains open.
### SonarQube follow-up — 2026-11-08 continued

The latest owner-reported project total before this batch is 1,512; the live total after these edits has not been read. The IDE rechecked `WindowsStorageHealthProvider.cpp` and showed 36 findings before the WMI simplification and 30 after it. A later incremental analysis covered six edited files and logged 120 findings across those files, so a per-file analysis result must not be presented as the whole-project count. No full scan was restarted. The 80% target remains open.

Further changes used `std::jthread` only where the existing code already detached or explicitly joined the worker. The WMI helper extraction preserves COM scope, query order, and SMART fallback flow. Both source copies built; focused service/FTP tests passed 15/15 per copy, and the storage-health selection passed 28 with 2 environment skips per copy. The separate-output versioned test run was inconsistent; a canonical rebuild and isolated rerun passed. See the [IDE run log](SONAR_RUN_LOG_2026-11-07.txt) for exact details.
### 2026-11-08 — refreshed panel count

After the six-file incremental analysis, a read-only VS Code snapshot showed 1,513 Problems total. The Sonar filter showed 1,512 rows; the status bar showed 2 errors and 1,511 warnings. The IDE log's 120 findings were the current per-file results across six analyzed files, not an increase of 120 in the project total. This live total remains above the 80% target (no more than 352 open findings). No full scan was restarted.
## SonarQube IDE count — 2026-11-08 update

The owner reports 1,492 total VS Code Problems, 20 below the previous owner-reported 1,512. The last direct snapshot showed 1,513 total Problems with 1,512 Sonar-filtered rows; the latest Sonar-only subtotal has not been independently read. The requested 80% threshold remains at no more than 352 findings from the 1,761 baseline. See the [IDE run log](SONAR_RUN_LOG_2026-11-07.txt).

## SonarQube IDE remediation — 2026-11-08 focused update

Applied the init-statement recommendation for local Winsock startup data in four network/server files, and used compile-time lengths for the listener's fixed replies. The Desktop IDE recheck reported CentralServerListener.cpp at 13 findings after the changes; focused network tests passed 10/10 in both the versioned and Desktop-source builds. A two-file batch reported 32 combined findings, not a project total. The owner reports 1,492 total Problems; a separate read-only IDE snapshot still shows 1,513 total, so the latest project-wide Sonar subtotal remains unverified. Full details are in the IDE run log.

## SonarQube IDE remediation — 2026-11-08 follow-up

IcmpHostPinger now uses a mutable std::array payload with the same bytes and length, std::byte reply storage, and memcpy into a local reply struct. Its per-file recheck reports 4 findings versus the earlier file decoration of 5. WindowsStorageHealthProvider's extent buffer now uses std::byte, but its recheck remains at 30 findings, so that edit is not counted as a cleared row. Both versioned and Desktop-source builds passed; focused storage-health tests passed 22 with 2 expected environment skips in each build. The latest owner-reported total is 1,492, while the direct IDE snapshot remains older at 1,513 total. See the [IDE run log](SONAR_RUN_LOG_2026-11-07.txt).

Win32NetworkInterfaceDetector's UTF-8 output allocation now includes the terminating NUL written by the Windows conversion API before trimming it from the returned string. Desktop app/test targets and the versioned test target built; the focused network-interface tests passed 5/5 in both builds. The per-file Sonar result stayed at 11, so this correction is not counted as a cleared finding. The direct IDE panel still shows its stale 1,513 total; the owner-reported total is 1,492.

Per owner request, VS Code Sonar automatic analysis is now disabled. Captured proxy issue messages are recorded in the run log; safe matching fixes were applied only to the versioned checkout. The three unsaved VS Code files were preserved. Debug accelerator-server and unit-test targets built, and 35 focused proxy/local-server tests passed. No new Sonar count is claimed.

## Sonar IDE saved snapshot — 2026-11-08

A read-only VS Code Problems snapshot taken before the latest edits showed 1,485 total Problems and 1,484 filtered findings. The project-level count is now stale because Sonar automatic analysis remains disabled by owner request. Do not treat it as a post-fix count. StorageView's final saved warning and nine SettingsView rows have been addressed in both the versioned checkout and the VS Code Desktop copy. Exact diagnostics and build/test results are in [the run log](SONAR_RUN_LOG_2026-11-07.txt).

The saved findings in ReportsView, ComingSoonView, WinMain, Logging, and DiskStatus have also been reviewed and addressed in both source copies where applicable. Debug app and unit-test targets build successfully to separate output locations. Existing unit tests were not run for the Logging/DiskStatus-only pass; there are no direct tests for the affected helpers. Sonar remains disabled, so the 1,485 / 1,484 snapshot is stale and does not establish the remaining count. Networking source and the three unsaved VS Code files remain untouched.

The saved hard-coded-password finding in the Desktop `AdminAuthService.cpp` has been aligned with the already-versioned implementation and security policy: first-use credentials are user-selected and stored/read back before sign-in succeeds. The clean Desktop build and focused auth suite passed; both auth suites passed 10/10. The earlier forced-output Desktop attempt used a stale library and is documented in the run log. Sonar remains disabled; no updated project count is claimed.

The two saved `AgentService.cpp` findings are also addressed in both source copies with an explicit destructor stop call and `std::unique_lock` CTAD. Report sending and network behavior were left unchanged. Both focused loopback AgentService tests passed in each build; no external endpoint was contacted. The 1,485 / 1,484 snapshot remains stale.

`UpdatesView.cpp` remains open: the saved VS Code panel reports 51 rows, but its accessibility view exposes only 10 messages and window capture times out. No edits were made to that file, and it has not been passed as complete. Exact visible messages and the access limitation are recorded in [the IDE run log](SONAR_RUN_LOG_2026-11-07.txt). No scan was restarted.
