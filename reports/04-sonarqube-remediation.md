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

The next visible IDE group contained 17 findings; eight rows were exposed and matched to `src/app/src/StorageView.cpp` by their messages and source locations. Those eight were addressed in the local checkout with equivalent optional checks, rating branches, formatting, and fixed-size `std::array` buffers. The other nine rows in that group and the rest of the project findings are not yet enumerated. The run log records the individual visible rows and current status. Across the documented proxy and StorageView work, 23 distinct visible findings have a code disposition; the requested 80% threshold (1,409 of 1,761 baseline findings) has not been reached. The Debug app build compiled the changed StorageView source but could not replace the running executable (LNK1168). No Sonar re-analysis was run.

The IDE's Explorer also showed eight problems in `BackupsView.cpp`, but their diagnostic text was unavailable. Two same-pattern fixed destination buffers were converted to `std::array` based on the confirmed `cpp:S5945` pattern from StorageView. These are candidate fixes only and are excluded from the confirmed issue count until their actual IDE rows are captured.

The same IDE-visible rule families also motivated a behavior-preserving source pass: 22 optional-presence checks now use `has_value()`, and nested conditional expressions in package selection, transfer status, health scoring, and native control color selection were expressed as ordered branches or an equivalent boolean condition. Since their exact rows were not captured, these are tracked as candidate remediations rather than verified Sonar findings. Debug unit-test build passed and CTest completed with 506 passed, 6 skipped, and 0 failed (512 total). The app sources compiled, but final linking remained blocked by the already-running app executable (LNK1168). No Sonar re-analysis was run.

The next behavior-preserving candidate batch changed 13 message-formatting expressions in `PackageSyncService.cpp` to `std::format`, matching the `cpp:S6185` family directly observed in StorageView. The exact IDE rows for these locations were unavailable, so they remain candidate remediations and are excluded from the count of 23 confirmed IDE rows. The application module and unit test executable built successfully; all 25 `PackageSyncServiceTest` cases passed, and focused `git diff --check` passed. The original run logs confirm the finished 1,761-finding snapshot but do not expose individual issue details. The IDE cache remains locked, and the IDE window could not be activated for paging; the 80% threshold (1,409 findings) remains open. No Sonar re-analysis was run.

A second candidate batch replaced 13 fixed byte buffers in `PkgFileParser.cpp` with same-size `std::array` instances and `.data()`/`.size()` at the parsing boundaries. All 15 `PkgFileParserTest` cases passed after rebuilding `gth_unit_tests`; focused diff checking passed. This follows the C-style buffer rule shown in the IDE, but the actual parser issue rows were not available, so the 13 locations remain candidates and are excluded from the 23 confirmed IDE rows.

The three fixed text buffers in `Win32NetworkInterfaceDetector.cpp` were converted to same-size `std::array` storage, preserving IP/MAC formatting and Win32 output capacities. The network module rebuilt successfully and all 5 focused network-interface tests passed. This is a candidate match to the visible C-style buffer rule and remains excluded from the 23 confirmed IDE-row count.

The Sony automation formatter now uses same-size `std::array` storage for its time and size buffers, and its duration strings use equivalent `std::format` expressions. The application library and test executable rebuilt successfully; all 8 Sony automation tests passed. These are candidate rule-family matches only and remain outside the confirmed IDE-row count.
