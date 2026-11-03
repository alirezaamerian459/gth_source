# Game Transfer Hub

Game Transfer Hub (GTH) is a Windows desktop application for organizing a local or networked PlayStation game archive. The repository contains a native C++20 application with a Win32/WTL interface, SQLite persistence, storage indexing, update and transfer modules, and a separate browser-testing project.

The repository targets Windows x64. Feature descriptions in historical notes may not match current behavior; the code, tests, and focused documents linked below are the working references. This checkout has not been built or run as part of the repository cleanup.

## Build

Use Windows with Visual Studio 2022 or Build Tools, the C++ and ATL components, and CMake 3.24 or newer. CMake downloads declared dependencies during the first configure.

~~~powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
~~~

The Debug executable is produced at build/src/app/Debug/GameTransferHub.exe. See [development](docs/DEVELOPMENT.md) for prerequisites and the optional browser test project.

## Project documentation

- [Project specification](docs/PROJECT_SPECIFICATION.md) — documented scope, constraints, and unresolved owner questions.
- [Development policy](docs/DEVELOPMENT_POLICY.md) — engineering workflow and repository rules.
- [Architecture](docs/ARCHITECTURE.md) — module boundaries and implementation constraints.
- [Technology stack](docs/TECHNOLOGY_STACK.md) — technologies observed in the build configuration.
- [Database](docs/DATABASE.md) — SQLite migration and schema source of truth.
- [Development guide](docs/DEVELOPMENT.md) — configure, build, run, and test commands.
- [Coding standards](docs/CODING_STANDARDS.md), [testing policy](docs/TESTING_POLICY.md), and [security policy](docs/SECURITY_POLICY.md).
- [Roadmap](planning/roadmap.md) and [current status](planning/PROJECT_STATUS.md).
- [Agent instructions](AGENTS.md) — required reading and workflow for coding agents.

## Browser tests

The optional Playwright project lives in tests/browser-automation. It is for authorized test targets configured through its host allowlist. Real CAPTCHA challenges require a human to complete them; the project does not implement CAPTCHA or access-control bypass.

## Current status

The repository is being reconciled with its current source. Supported console generations, some external-service boundaries, and the Windows build/test baseline still need owner confirmation or verification. See [project status](planning/PROJECT_STATUS.md).
