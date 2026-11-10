# Report 03 — Source audit and test results

## Scope

The request was to remove clearly unused/unwired application code, improve code quality without changing behavior, and test with SonarQube. This pass preserved production behavior and did not push or commit changes.

## Changes

- Updated sidebar tests to use the current logical tab indexes, including the JB tab, and check every admin-restricted tab.
- Made Windows Credential Manager integration tests skip when the current logon cannot store machine-persistent generic credentials. This runner reports that only session credentials are supported and `CredWriteW` returns `ERROR_NO_SUCH_LOGON_SESSION`; the application implementation was not changed.
- Corrected the browser test dependency `dotenv` from the unpublished `^21.4.0` range to published `^17.4.2`. The package API used by this project remains `config()`. The npm package page lists 17.4.2 as the latest version at the time of this report: [npm package](https://www.npmjs.com/package/dotenv).
- CMake references all 129 production `.cpp` files; no additional clearly unwired implementation unit was found. No production source was deleted in this pass. The three obsolete network helper pairs removed during the previous cleanup are recorded in [report 02](02-repository-cleanup.md).

## Verification

Environment: Windows 10.0.19045.6466; Visual Studio Build Tools 2022 17.14.39; MSVC 19.44.35228.0; Windows SDK 10.0.26100.0; CMake 4.4.3; Visual Studio 17 2022 x64 generator; Node.js 24.21.0; npm 11.19.0.

Commands and results:

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` — passed.
- `cmake --build build --config Debug --parallel 4` — passed.
- `ctest --test-dir build -C Debug --output-on-failure` — 497 passed, 6 skipped, 0 failed (503 registered).
- `cmake --build build --config Release --parallel 4` — passed.
- `ctest --test-dir build -C Release --output-on-failure` — 497 passed, 6 skipped, 0 failed (503 registered).
- `npm install --no-save --package-lock=false --ignore-scripts` in `tests/browser-automation` — installed the declared packages without a lockfile or package scripts; npm reported 0 vulnerabilities.
- `npm test` in `tests/browser-automation` — 12 passed, 0 failed.
- `git diff --check` — passed.

The six CTest skips are three Windows Credential Manager operations unsupported by this logon and three existing optional live drive/network checks. The browser tests are Node tests; no browser was launched.

## SonarQube and static-analysis status

SonarQube was not run. No local server answered at `http://localhost:9000`, and `sonar-scanner` is not installed. SonarSource documents CFamily analysis (C/C++/Objective-C) as available starting with Developer Edition, with C++20 supported. For this repository, use SonarQube Server **2026.1 LTA Developer Edition or higher**, plus the scanner and Build Wrapper matching that server's analyzer. See [CFamily prerequisites](https://docs.sonarsource.com/sonarqube-server/2025.1/analyzing-source-code/languages/c-family/prerequisites), the [C++ support overview](https://docs.sonarsource.com/sonarqube-server/analyzing-source-code/languages/c-family/overview), and [2026.1 LTA documentation](https://docs.sonarsource.com/sonarqube-server/2026.1/server-installation/introduction).

## Findings and limits

- Debug linking produced `LNK4098` CRT mismatch warnings for the app and unit-test executable. They did not prevent the build or tests; the cause has not been identified.
- Release compilation produced warning C4244 in `src/captcha/SonyLinkCopyWatcher.cpp:441`. The narrowing occurs after the title ID is validated as ASCII, as the adjacent comment documents. No CAPTCHA-related implementation change was made because the project's scope decision is still open.
- The source-to-CMake audit rules out orphan `.cpp` translation units, not every potentially unreachable member function. A SonarQube CFamily scan remains the needed next static-analysis step.
- Changes remain local and uncommitted; no remote push or release was performed.
