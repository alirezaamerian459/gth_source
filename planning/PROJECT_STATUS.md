# Project status

**Last updated:** 2026-11-03  
**Repository baseline:** branch main at d3e52c2, cloned from the public GitHub repository.  
**Current phase:** 01 — Requirements and documentation reconciliation. The cleanup pass is recorded; owner scope decisions and build verification remain open.

## Current state

- The local clone retains the original Git history and origin remote.
- The root README and AGENTS.md now point to one current documentation set under docs/.
- Stale and conflicting root guides were removed from the working tree; Git history retains their prior versions.
- Architecture, development, database, browser-test, and installer guidance was consolidated or corrected.
- Three unreferenced network helper pairs for user-agent rotation, behavioral timing, and cookie persistence were removed after source search found no call sites outside those files and their CMake entries. No application feature behavior was intentionally changed.
- The broken build/release batch scripts and machine-specific launcher were removed. The broken source-archive script was replaced by scripts/package_source.py; it has not been executed.
- No CMake build, C++ test, browser test, security scan, installer build, or runtime check was run.
- No CI workflow is present; phase 02 plans to establish a Windows build/test baseline before adding CI.
- Changes are local and uncommitted. No remote push or release was performed.

## Verified repository observations

- Root CMake requires Windows and C++20; GTH_BUILD_TESTS defaults to ON.
- The app target sets its output name to GameTransferHub; the NSIS script now points to the matching Release output path.
- SQLite migrations are defined in src/database/src/Migrator.cpp and currently list versions 1 through 16.
- The browser test project is separate from CTest and uses Node.js/Playwright with a configured host allowlist.

## Open requirements

- Confirm supported console generations and package/archive formats.
- Confirm the production role, if any, for CAPTCHA-related code and clarify permitted external-service interactions.
- Establish a verified Windows build and test baseline.
- Select the next product goal and define acceptance criteria.

## Next actions

1. Resolve the product-scope questions in docs/PROJECT_SPECIFICATION.md and ADR-0002.
2. Run the documented Windows build and CTest commands in an appropriate environment and record actual results.
3. Review production external-service and credential data flows in phase 03.
4. Plan only the next owner-approved product task.
