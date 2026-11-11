# JB Scan Target Verification — 2026-11-11

## Change

The JB Games page now exposes a **Scan target** selector. It always includes **All local drives** and adds each currently available local HDD, SSD, external HDD, or USB storage with its friendly name and root path. Choosing a drive changes the action to **Scan selected drive** and limits the scan to that storage. The scan never writes, moves, or deletes files.

The target storage ID is captured when a scan starts, so changing the selector while a scan is running cannot redirect that scan. Network and unavailable storages remain excluded from the selector and scan scope.

## Verification

- Focused target-filter tests: 3/3 passed (`JbScanTargetTest.*`).
- Unit suite excluding live Central Server tests: 511 tests, 505 passed, 6 skipped; 0 failures.
- Debug `GameTransferHubApp` build: passed.
- Release `GameTransferHubApp` build: passed.
- FTP source and UI were not changed.
