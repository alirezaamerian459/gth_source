# Phase 02 tasks — Reproducible build and test baseline

- [x] Confirm phase 01's canonical dependency and build instructions.
- [x] Record Windows, MSVC/ATL, CMake, and generator versions.
- [x] Configure a clean x64 Debug build.
- [x] Build all configured targets in Debug and Release.
- [x] Run CTest in both configurations: after the local Sonar remediation pass, 499 passed, 6 skipped, and 0 failed out of 505 registered tests.
- [x] Investigate failures without weakening tests: stale sidebar indices were corrected, and Credential Manager integration tests now skip only when the logon lacks required persistence support.
- [x] Run Release verification as an additional configuration check.
- [x] Record the verified toolchain, commands, and results in development docs, status, and report 03.
- [ ] Add a Windows GitHub Actions build/test workflow using reviewed action versions and least-privilege permissions.
- [ ] Review the workflow file locally; verify it on GitHub only after an authorized push or pull request.

The Sonar remediation and refreshed-scan status are recorded separately in [report 04](../../../reports/04-sonarqube-remediation.md); the updated local tree has not yet been rescanned.
