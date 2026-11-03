# Report 02 — Repository cleanup

## Phase

Phase 01 — Requirements and documentation reconciliation. The cleanup work is complete; the phase remains open for owner decisions and the later Windows verification baseline.

## Summary

Consolidated the root entrypoint and current project guidance, retired stale/conflicting guides, repaired the installer configuration, and removed obsolete helpers. The original Git history remains available. No product feature work was performed.

## Files changed

- Rebuilt README.md as the single root entrypoint and updated AGENTS.md.
- Consolidated architecture, database, development, technology, and specification documents under docs/.
- Updated phase/status records, the security policy, and ADR-0002.
- Replaced the redundant browser-automation guides with one README; removed its missing dev-server command and unused package dependency.
- Corrected GameTransferHub.nsi to use the current app output path and consistent existing version metadata.
- Removed stale root guides, machine-specific launcher, broken build/release batch scripts, and the obsolete source-archive script.
- Added scripts/package_source.py and ignore rules for local configuration and generated browser/package artifacts. The archive utility was not executed.
- Removed UserAgentProvider, BehavioralEvader, and CookieManager source/header pairs after a repository search found no references outside the files themselves and their network target entries; removed those target entries as well.

## Decisions

- Keep the production CAPTCHA-related scope pending owner review. The old bypass proposal guides were removed from the current tree as unapproved; their history remains.
- Preserve the current C++20/Windows modular architecture.
- Do not publish, push, or create a release as part of local cleanup.

## Verification

- Reviewed source references before removing the three unused network helpers.
- Reviewed current paths and project references while consolidating documentation.
- No CMake configure/build, C++ tests, browser tests, installer build, security scan, or runtime check was executed.
- Changes remain local and uncommitted.

## Limitations and remaining work

- Supported console generations and archive formats still need owner confirmation.
- Production CAPTCHA and external-service scope still need owner clarification and integration review.
- Windows build, tests, and installer packaging remain unverified.
- Phase 02 and phase 03 work remain open; select no feature until its requirements and acceptance criteria are approved.
