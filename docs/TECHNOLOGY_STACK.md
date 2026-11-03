# Technology stack

This describes technologies present in the current build configuration. It is not a proposal to replace the stack, and the repository has not yet had a clean Windows build in this workspace.

| Area | Current technology | Evidence and notes |
|---|---|---|
| Language | C++20 | Required by the root CMake project. |
| Operating system and UI | Windows x64, Win32, WTL/ATL | The root CMake project rejects non-Windows builds; WTL is fetched by the app target and ATL is supplied by Visual Studio. |
| Build | CMake 3.24+, Visual Studio 2022 generator | Multi-configuration build; select Debug or Release explicitly. |
| Persistence | SQLite | SQLite amalgamation and versioned migrations in src/database. |
| Logging | spdlog | Fetched by src/logging/CMakeLists.txt. |
| Networking | libcurl, libssh2, Winsock and Windows networking APIs | Present in networking/FTP targets; inspect those target files before changing dependencies. |
| C++ tests | GoogleTest with CTest | Fetched by tests/unit/CMakeLists.txt; tests are under tests/unit. |
| Browser tests | Node.js and Playwright | Optional project under tests/browser-automation; uses dotenv for local configuration. |
| Installer | NSIS | The root GameTransferHub.nsi describes the Windows installer. |

Dependency URLs and hashes belong in the relevant CMake files. Avoid copying version claims into this document unless they are checked against those declarations.

## Architecture choice

The application is a native Windows modular monolith. There is no separate web frontend or backend. The current repository does not establish a need for containers, Kubernetes, Redis, message brokers, or microservices.

## Dependency changes

Before adding or upgrading a dependency, inspect its source, version or hash, license, transitive dependencies, and security impact. Prefer the build configuration over historical guides for current dependency details.
