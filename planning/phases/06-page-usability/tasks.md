# Phase 06 tasks — Page-level usability

- [x] Review all routes and identify pages that already have a focused single task versus pages with dense optional controls.
- [x] Add collapsible secondary controls to Storage, Updates, Backups, Settings, FTP, Transfers, Consoles, Enterprise, and Dashboard.
- [x] Replace Storage's flat drive table with selectable cards that keep each storage summary and its physical disk details together; retain category membership actions.
- [x] Keep primary task controls and existing page behavior available; preserve routing and service contracts.
- [x] Build the Debug app target: `cmake --build build --config Debug --target GameTransferHubApp --parallel 2` succeeded.
- [x] Launch the Debug app and leave it open for owner review. The window appears in the Windows app inventory.
- [ ] Complete visual review of page layouts. Windows screenshot capture timed out, so I could not inspect the window contents.
- [ ] Get owner review of the revised interface.
- [x] Do not run automated tests for this layout-only pass.

Notes:

- Storage cards show capacity, used/free space, status, health, temperature, and individual physical disk details in separate inset boxes where a storage spans multiple disks. Visual owner review is still pending.
- Storage health details now include the supported QNAP/ATA SMART counters in a disclosure-only view; raw LBA values are labeled as vendor units and unknown values remain unknown.
- The JB page recognizes the exFAT `GAME` layout and `.exfat` artifacts, and storage health has a read-only Windows temperature fallback for USB/SATA bridges. Unknown remains the honest result when no sensor is exposed.
- The Release executable was locked by an already-running GameTransferHub process, so the Debug output is used for owner review.
- The UI changes are local; no remote push or release was performed.
