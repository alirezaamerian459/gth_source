# Dr Game Transfer Hub - source code (v6.9.0)

This archive is the complete source of Game Transfer Hub v6.9.0: every C++ source and header, the
resources, the build files, the unit tests, and the documentation. It contains no binaries.

## What it does

Game Transfer Hub indexes a PS4/PS5 game archive (local drives or network shares) and keeps it in
step with Sony's own update servers:

* **Scans storages** for `.pkg` files, groups a release's split parts together, and reads the
  title, region and version out of the package itself where possible.
* **Checks every game against Sony**: PS4 through the console's own per-title update address
  (no link needed); PS5 through a per-title "version file" link that has to be known first (see
  below), which Sony's servers then answer with the latest version and the exact size/checksum of
  every piece of the package.
* **Downloads what's missing or out of date**, one game after another, with everything checked
  against Sony's published SHA-256 before it touches the archive, free space checked first, and a
  failed download retried automatically before it's given up on. PS5 downloads add the small
  metadata piece a package needs to be read back by title/version; PS4 updates are joined from
  Sony's pieces into the one file the console expects and saved beside the game without touching it.
* **Finds new games and learns links from files already on disk** before a full run, so a growing
  archive needs less manual upkeep over time.
* A PS5 game's link normally has to be found once, by hand, on the community tracker site
  (`prosperopatches.com`) - the app opens the page and picks up the address the moment it's copied,
  so that's the only step that isn't automatic. A game's own downloaded metadata piece carries the
  same link, so a game with one already needs no site visit at all.

See `release/UPDATES_GUIDE.md` for the full walkthrough.

## Build

Requirements

* Windows 10/11, x64
* Visual Studio 2022 or the Build Tools, with the "Desktop development with C++" workload
* CMake 3.24 or newer (built and tested with 4.4)
* Internet access the first time you configure - CMake downloads the pinned third-party libraries:
  WTL 10.01, SQLite 3.53.4, curl 8.11.1, libssh2 1.11.1, spdlog 1.17.0 and googletest 1.18.0

Commands (from the folder that contains `CMakeLists.txt`)

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release -- -m
    build\tests\unit\Release\gth_unit_tests.exe

The program is `build\src\app\Release\GameTransferHub.exe`. It is one self-contained file (static
runtime), and its manifest asks for administrator rights - so don't start it just to "try it out"
against a real library you care about.

In Git Bash, MSBuild switches take dashes instead of slashes: `-- -m -nologo -v:m`.

## Layout

    CMakeLists.txt              top level: options and the module list
    src/core                    domain model and the interfaces everything else depends on
    src/application             services (indexing, Sony update check + sync, automation, ...)
    src/indexing                storage scanner and the .pkg header parser
    src/network                 WinHTTP to Sony (version files, manifests, resumable downloader,
                                pinned Sony root certificate), link import, online title lookup
    src/database                SQLite repositories and migrations
    src/infrastructure          the service container that wires the modules together
    src/app                     the WTL user interface (tabs, views, theme, resources)
    src/ftp, src/ftpserver,     FTP client/server and transfer support
    src/transfer, ...
    tests/unit                  googletest suite (431 tests)
    release/                    end-user guides and the browser helper script

`python make_source_zip.py` rebuilds this archive (source only, no binaries) from the folder it sits in.

Version numbers live in four places, and each release bumps them together: `src/app/src/MainFrame.cpp`
(title bar), `src/app/src/SplashWindow.h`, `src/app/resources/AppResources.rc` and
`GameTransferHub.nsi`; the `GameTransferHub/x.y` user agents in `src/network/src/` follow the same number.

## Where to start reading

* `ARCHITECTURE.md` - how the modules fit together
* `release/UPDATES_GUIDE.md` - what the Updates tab does and why (Sony links, the download queue, guided collection)
* `src/application/include/gth/application/indexing/IPackageSync.h` and `IUpdateChecker.h` - the update pipeline's contracts
* `src/network/include/gth/network/SonyUpdateService.h` and `SonyLinkImport.h` - talking to Sony
* `src/app/src/UpdatesView.cpp` - the Updates tab
* `src/application/include/gth/application/indexing/DownloadHistory.h` and `src/app/src/DownloadsView.cpp` - the Downloads tab's per-game stats

## Browser helper

`release/gth-sony-link-collector.user.js` is a Tampermonkey/Violentmonkey userscript. It is not part of the
build. See `release/UPDATES_GUIDE.md`.
