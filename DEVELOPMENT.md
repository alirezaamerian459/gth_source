# Development

## Prerequisites

- **CMake** 3.24+ - `winget install Kitware.CMake --silent` or https://cmake.org/download
- **Visual Studio 2022 Build Tools** with:
  - the "Desktop development with C++" workload (`Microsoft.VisualStudio.Workload.VCTools`)
  - the "C++ ATL" component (`Microsoft.VisualStudio.Component.VC.ATL`) - WTL builds on top of ATL, and this is not installed by the base C++ workload
  - a Windows 10/11 SDK (`Microsoft.VisualStudio.Component.Windows11SDK.22621` or similar)

  One-line unattended install:
  ```powershell
  winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget `
    --override "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.ATL --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --includeRecommended"
  ```

No .NET SDK, no C# compiler, and no Windows App SDK are required anywhere in
this stack.

## Dependencies

All fetched automatically by CMake's `FetchContent` at configure time (network
access required on first configure; cached under `build/_deps/` afterward):

| Dependency | Source | Pinned to |
|---|---|---|
| SQLite | official amalgamation, sqlite.org | 3.53.4 |
| WTL | official release, SourceForge | 10.01 |
| spdlog | github.com/gabime/spdlog | v1.17.0 |
| GoogleTest | github.com/google/googletest | v1.18.0 |

## Build

```bash
cmake -G "Visual Studio 17 2022" -A x64 -B build
cmake --build build --config Debug
```

The Visual Studio generator is used deliberately (rather than Ninja/Makefiles)
because it sets up the MSVC/ATL environment automatically - no need to
manually invoke `vcvarsall.bat` in every shell before building.

## Run

```bash
build\src\app\Debug\GameTransferHub.exe
```

On first run this creates `%LOCALAPPDATA%\GameTransferHub\gth.db` (applying
migrations automatically) and starts logging to
`%LOCALAPPDATA%\GameTransferHub\logs\log_<date>.txt` (spdlog daily rolling
file).

## Test

```bash
ctest --test-dir build -C Debug --output-on-failure
```

Tests use SQLite's `:memory:` database (a real SQLite engine, not a fake) and
a hand-written `FakeStorageDetector` test double, so they never touch a real
drive or the app's actual database file.

## Adding a new view (WTL)

Every `Create()` call on a `CWindowImpl`/WTL control needs a **named** local
`CRect` variable, not an inline temporary - see ARCHITECTURE.md's `_U_RECT`
section. This is the single most common compile error when extending the UI.

## Milestone roadmap

All 14 milestones are implemented: CMake project structure, the domain
foundation, SQLite + migrations, spdlog, local storage detection, the Game
Indexer, the full Update Manager pipeline (detection, scheduling, download,
verification, atomic archive replacement), the Network Manager, the Console
Manager, the Transfer Engine, Monitoring (Logs/Reports), Enterprise (Central
Server / Agent health reporting - see ARCHITECTURE.md for why this stops
short of full archive replication), and UI/UX polish (a shared `Theme.h` +
`ModernButton` + owner-drawn sidebar - see the README's M13 entry) - see the
specification's Section 83 for the full milestone list. Each milestone should extend existing interfaces/targets rather
than restructure them; see [ARCHITECTURE.md](ARCHITECTURE.md), including its
note on the `ServiceContainer` member-declaration-order footgun that any new
composition-root member touching the database at construction time must
respect.

## Known environment quirks

- If `cmake` isn't found in a freshly opened terminal right after installing
  it, that terminal's process started before the installer updated `PATH` -
  open a new terminal.
- A plain `cmake --build build` (no `--config`) after generating with the
  Visual Studio generator builds the default `Debug` configuration; always
  pass `--config Debug`/`--config Release` explicitly to avoid ambiguity.
