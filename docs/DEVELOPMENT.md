# Development

## Prerequisites

- Windows x64.
- Visual Studio 2022 or Build Tools with the Desktop development with C++ workload and ATL components.
- CMake 3.24 or newer.
- Network access for the first configure because CMake FetchContent retrieves project dependencies.

The repository uses CMake's Visual Studio generator, which selects the MSVC/ATL toolchain. Build configurations are multi-configuration; pass Debug or Release explicitly.

## Configure, build, and test

From the repository root:

~~~powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
~~~

The application executable is expected at build/src/app/Debug/GameTransferHub.exe for Debug and build/src/app/Release/GameTransferHub.exe for Release.

Tests are built by default. To configure without the C++ tests, set GTH_BUILD_TESTS=OFF when configuring. The commands above describe the configured workflow; they do not imply that a build or test has been run.

## Last verified local source

On 2026-11-03, the project configured and built with Windows 10.0.19045.6466, Visual Studio Build Tools 2022 17.14.39 (MSVC 19.44.35228.0), Windows SDK 10.0.26100.0, and CMake 4.4.3. After the local Sonar remediation changes, Debug and Release CTest runs each completed with 499 passed, 6 skipped, and 0 failed out of 505 registered tests. The separate browser suite passed all 12 unit tests and 7 integration tests with Node.js 24.21.0. See [report 03](../reports/03-code-audit-and-test-results.md) for the original baseline and [report 04](../reports/04-sonarqube-remediation.md) for this pass and its scan limitation.

On 2026-11-07, the Release build completed with `cmake --build build --config Release --parallel 2` after adding storage categories. `ctest --test-dir build -C Release --output-on-failure` completed in 14.23 seconds with 503 passed, 6 skipped, and 0 failed out of 509 registered tests. The skipped tests are listed in [phase 04 tasks](../planning/phases/04-storage-categories/tasks.md).

## Run

Run the executable produced by the build. The application stores its default database and logs under the user's local application data directory. Do not use a real archive or personal database as a test fixture.

## Browser tests

The optional Playwright test project is separate from the CMake build:

~~~powershell
Set-Location tests/browser-automation
npm install
npx playwright install chromium
Copy-Item .env.example .env
npm run test
~~~

Set TARGET_URL and ALLOWED_HOSTS in the local .env file for an authorized test environment. Do not commit .env, logs, screenshots, videos, or persistent browser session data. Real CAPTCHA challenges require human completion.

## Installer

The root GameTransferHub.nsi expects a Release build and writes its installer under dist. Create that output directory before invoking makensis:

~~~powershell
New-Item -ItemType Directory -Force dist
makensis GameTransferHub.nsi
~~~

Installer generation has not been verified as part of this repository cleanup.
