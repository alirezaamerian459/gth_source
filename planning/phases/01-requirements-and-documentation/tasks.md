# Phase 01 tasks — Requirements and documentation reconciliation

- [x] Review current source/build structure and identify conflicting or duplicate documentation.
- [x] Establish the root README as the project entrypoint and consolidate current architecture, database, and development guidance under docs/.
- [x] Remove stale, duplicate, and unapproved proposal guides from the working tree; prior history remains available in Git.
- [x] Correct the NSIS executable path, version metadata, and dependency packaging; remove the stale destructive release script.
- [x] Remove the machine-specific launcher and obsolete build helper.
- [x] Replace the broken source-archive script with scripts/package_source.py.
- [x] Remove the unused network helper sources after confirming no call sites outside their own files/build entries.
- [x] Remove the browser project's missing dev-server command and unused node-test-utils dependency; reduce its docs to one README.
- [x] Add ignore rules for local secrets, browser runtime data, and generated packages.
- [x] Record the cleanup and verification limits in project status and a numbered report.
- [ ] Confirm supported console generations and package/archive formats.
- [ ] Confirm production CAPTCHA-related and external-service scope.
- [ ] Run and record the Windows build and CTest baseline in phase 02.
- [ ] Select the next product goal and define acceptance criteria.

## Current result

The repository cleanup tasks above are complete. Phase 01 remains open until owner decisions are recorded; build verification belongs to phase 02.
