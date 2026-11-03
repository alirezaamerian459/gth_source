# Architecture

## System shape

GTH is a Windows desktop modular monolith. The root CMake project builds separate targets for core contracts, application services, infrastructure modules, and the Win32/WTL application. Keep module dependencies explicit in their CMake files.

~~~text
src/app  (Win32 + WTL user interface)
   |
   v
src/application  (application services and use cases)
   |
   v
src/core  (domain types and interfaces)

src/infrastructure  (composition root)
   +-- src/database
   +-- src/storage_detection and src/indexing
   +-- src/network and src/updates
   +-- src/transfer, src/ftp, and src/ftpserver
   +-- src/verification, src/credentials, and src/logging
~~~

This is the intended dependency direction, not a substitute for inspecting the affected target and call path. The root CMakeLists.txt is the source of truth for configured modules.

## Boundaries

- Keep core types independent of concrete database, network, UI, and operating-system adapters.
- Keep UI views dependent on application-facing interfaces. Do not make views construct repositories or concrete infrastructure services.
- Use ServiceContainer as the explicit process composition root. Avoid adding a general-purpose dependency-injection framework without a concrete need.
- Apply database migrations before constructing services whose constructors read database state. C++ member initialization follows declaration order; preserve the ordering around the migration runner in ServiceContainer.
- Put module source files and link dependencies in the relevant CMakeLists.txt.
- Keep archive update work separate from console modification. Do not add authentication, DRM, licensing, CAPTCHA, WAF, or anti-bot bypass behavior.

## Windows UI constraints

WTL/ATL APIs that accept ATL::_U_RECT or a non-const RECT& require a named rectangle with a stable lifetime. Do not pass a temporary CRect to Create() or similar calls:

~~~cpp
CRect rect(0, 0, 100, 100);
control.Create(parent, rect, L"text", WS_CHILD);
~~~

Follow existing theme, control, ownership, and window-message patterns in src/app when changing a view.

## Persistence and integrity

SQLite schema changes are versioned in src/database/src/Migrator.cpp using PRAGMA user_version. Add a new migration; never edit an already-shipped migration in place. See [database documentation](DATABASE.md).

Update and transfer changes must preserve existing archive-safety behavior: validate paths and input, verify downloaded content where applicable, and avoid leaving partially replaced files. Inspect the implementation and tests before changing those guarantees.

## External interaction scope

The production CAPTCHA-related scope remains unresolved and requires review before changes. Browser tests are restricted by configuration to authorized test hosts and must wait for a human to complete a real challenge. See [ADR-0002](decisions/0002-captcha-and-anti-bot-scope.md).
