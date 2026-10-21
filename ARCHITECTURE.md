# Architecture

## Layering and dependency rule

```
GameTransferHubApp  (Win32 + WTL)
        |
        v
gth_application      <-- Core-only abstractions the UI calls
        |
        v
gth_core             (entities, enums, interfaces - no dependency on other gth_* libraries)
```

`gth_infrastructure`, `gth_database`, `gth_storage_detection`, `gth_network`,
`gth_transfer`, and `gth_logging` sit *beside* this chain as implementations
of Core/Application interfaces. `WinMain.cpp` is the only place allowed to
touch `gth_infrastructure` directly - it builds one `ServiceContainer` at
startup and passes `IStorageService&`/`ITransferQueueService&`/etc. down into
`MainFrame`, which passes each into the one view that needs it. No view ever
references `Database`, `StorageRepository`, `LocalStorageDetector`, or
`LocalFileTransferProvider` directly. This is what lets the storage/database/
network/transfer implementations change without ever touching the UI.

```
gth_infrastructure
        |
        +--> gth_database            (SQLite)
        +--> gth_storage_detection   (Win32 local drive detection)
        +--> gth_network             (IP Helper + Winsock2 reachability)
        +--> gth_transfer            (CopyFileExW-based file transfer)
        +--> gth_logging             (spdlog)
        +--> gth_application
```

## Why C++ instead of C#/.NET

Milestone 1 was originally built in C#/WinUI 3 (preserved under
[legacy-csharp-milestone1/](legacy-csharp-milestone1/)); it was rebuilt from
scratch in native C++ at the project owner's explicit request for a
managed-runtime-free stack. The domain model and the storage
detection/reconciliation logic are a deliberate 1:1 port - same entity
shape, same reconciliation rules (add / update / mark-offline, never
silently delete) - so behavior is directly comparable between the two
versions; only the implementation language and UI toolkit changed.

## Composition root, not a DI container

C++ has no framework-provided dependency injection container the way the
C# version used `Microsoft.Extensions.DependencyInjection`. Rather than pull
in a third-party DI library for a handful of long-lived singletons,
`gth::infrastructure::ServiceContainer` (src/infrastructure) does explicit
manual wiring: it owns one `Database`, one `StorageRepository`, one
`LocalStorageDetector`, and one `StorageService` for the lifetime of the
process, constructed in dependency order via its member-initializer list.
`WinMain` constructs exactly one `ServiceContainer` and passes
`container.storageService()` into `MainFrame`. This is the standard,
idiomatic C++ answer to "composition root" - no reflection, no container,
just ordinary object ownership.

## The `_U_RECT` footgun (WTL/ATL)

`CWindowImpl<T>::Create()` (and every WTL control's `Create()`) takes its
rect parameter as `ATL::_U_RECT`, which only has a converting constructor
from `RECT&` (a non-const lvalue reference) - not from a temporary. Writing
```cpp
control.Create(parent, CRect(0, 0, 100, 100), L"text", WS_CHILD);
```
fails to compile (`cannot convert argument 2 from 'CRect' to 'ATL::_U_RECT'`)
because `CRect(0, 0, 100, 100)` is a temporary (rvalue), and a temporary
cannot bind to a non-const reference. Every `Create()` call in this codebase
therefore assigns the rect to a named local variable first:
```cpp
CRect rect(0, 0, 100, 100);
control.Create(parent, rect, L"text", WS_CHILD);
```
The same applies to `MoveWindow(RECT&)` and any other WTL/ATL API typed as
`_U_RECT`. This bit every single `Create()` call when this module was first
written, so it's worth knowing before adding a new view.

## Member-declaration order in `ServiceContainer` (composition-root footgun)

C++ constructs class members in **declaration order** (the order they're
listed in the header), not the order they appear in the constructor's
member-initializer list - the compiler warns (`-Wreorder`) but still builds
if these two orders disagree, which makes the bug easy to introduce silently.
`ServiceContainer` learned this the hard way in Milestone 11: `transferQueue_`
(a `TransferQueueService`) queries the `TransferJobs`/`TransferItems` tables
*at construction time* (crash recovery - see `recoverIncompleteItemsFromPreviousRun()`),
but migrations were originally run in the constructor *body*, which executes
only after every member is already constructed. Since member construction
happens before the body regardless of member-initializer-list order, this
meant the app queried tables that didn't exist yet on first launch.

The fix is `ServiceContainer::MigrationRunner`, a private sentinel struct
declared immediately after `database_` whose constructor calls
`Migrator::applyMigrations(db)`. Because it's declared right after
`database_`, it's guaranteed to run before any later member - now and in any
future milestone that adds another member touching the database at
construction time. Any new member whose constructor queries the database
must be declared *after* `migrationRunner_`.

## Update Manager boundary (for later milestones)

Per the specification, the Update Manager keeps the *archive* current - it
never patches or otherwise modifies a console, and it never bypasses
authentication, DRM, or licensing. `IUpdateSource` (introduced in Milestone 4)
is the abstraction boundary for wherever update metadata/files come from;
only authorized/legitimate providers may implement it. This is a hard
constraint on every milestone from here on, not just an initial note.

## Multi-storage / canonical archive (design reserved, not yet implemented)

The specification anticipates a game variant existing on more than one
storage (e.g. replicated across two NAS units) and introduces the idea of a
canonical copy vs. replicas. Milestone 1 does not implement `Game`/
`GameVariant` yet, so there is nothing to make canonical - this note exists so
that when those entities are introduced (Milestone 2+), the schema leaves
room for a `IsCanonical`/`CanonicalStorageId`-style concept on whatever links
a game variant to its storage(s), rather than assuming a 1:1 mapping.

## UI theming (Milestone 13)

`src/app/src/Theme.h` is the single source of truth for color, font, and
spacing constants; every view includes it instead of hand-rolling its own
`CreateFontIndirect` calls or literal `RGB(...)` values. Two conventions to
follow when touching or adding a view:

- **Buttons**: use `ModernButton` (`src/app/src/ModernButton.h`), not
  `CButton`. It superclasses the real "Button" window class (WTL's
  `DECLARE_WND_SUPERCLASS`) and repaints it as a rounded, hover/press-aware
  control - standard ComCtl32 v6 buttons don't get Windows 11's rounded
  look automatically, that's opt-in per-control. Because it's a
  superclass rather than owner-draw-via-parent, swapping `CButton foo_;` to
  `ModernButton foo_;` needs no other changes - `Create()`, `EnableWindow()`,
  and `BN_CLICKED` via `COMMAND_ID_HANDLER` all keep working. Call
  `foo_.setKind(ModernButton::Kind::Primary/Secondary/Subtle/Danger)` after
  `Create()` to pick which of the four visual treatments it gets (see any
  existing view for the convention: the one primary action per page is
  `Primary`, destructive actions are `Danger`, everything else is
  `Secondary`/`Subtle`).
- **ListViews**: call `ApplyExplorerTheme(listView_)` (from `WtlCommon.h`)
  right after `Create()` - one line, gives File-Explorer-style row
  hover/selection instead of the flat classic-blue highlight, no owner-draw
  needed.
- **`WM_CTLCOLORSTATIC` must return a real, opaque brush, never
  `NULL_BRUSH`.** This bit the first draft of this milestone: a `CStatic`
  whose text changes via `SetWindowText` (a status label going from
  "Scanning..." to a shorter result string, for instance) relies on that
  brush to erase its old content before drawing the new text.  `NULL_BRUSH`
  skips the erase entirely, leaving old glyphs visible under the new ones.
  Every view's `OnCtlColorStatic` now returns a cached solid brush matching
  whatever's actually behind that control (the page background, or - on
  Dashboard - the metric cards' own background), picked per-control by
  comparing `HWND`s.

## Enterprise (Milestone 14) - scope and why it stops where it does

The specification's Section 83 envisions a full Central Server + Agents
architecture: multiple PCs/NAS units replicating archive state, a central
catalog, coordinated updates across machines. Milestone 14 implements the
*visibility* half of that (a server that knows which agents exist and their
current archive-health summary) and deliberately does not implement
replication of actual archive data (games, versions, update jobs) between
installations. Reasons:

1. **No second machine to validate against.** Everything here is tested
   against real sockets, but only over loopback (127.0.0.1) - a genuine
   multi-PC/NAS deployment was never available to exercise. Shipping a
   real replication protocol untested against actual network conditions
   (packet loss, partial writes, concurrent writers) would be worse than
   not shipping one.
2. **Replicating archive data changes the trust model.** Once a server can
   push data that mutates another installation's `Games`/`GameVersions`/
   `UpdateJobs` tables, that server is a privileged, unauthenticated actor
   over the network - a materially different security posture than "reports
   its own health for a human to look at." The wire protocol here (see
   below) has no authentication at all, which is fine for a one-way health
   ping but not for anything that writes to another machine's archive.
3. **It's genuinely additive later.** `ICentralServerListener`/
   `IRemoteReportSender` give the transport primitives (accept a
   connection, send a line, get an ack); a future replication feature can
   reuse them with a richer message format rather than starting over.

### Wire protocol

A single TCP line, tab-separated: `<nodeId>\t<name>\t<summary>\n`, answered
with `OK\n` (or `ERROR\t<message>\n`). No JSON library, no framing beyond
"read until newline" - deliberately as simple as `LocalMetadataSource`'s
manifest format, for the same reason (nothing here needs more structure yet,
and a hand-rolled parser this small is easier to reason about than pulling
in a new third-party dependency for it).

### Identity

An Agent generates a GUID once, on first use, and persists it to a small
text file (`%LOCALAPPDATA%\GameTransferHub\node-id.txt`) rather than the
database - it's local machine identity, not archive data, and needs to
survive independently of which database file the app happens to be pointed
at. Repeated reports from the same machine reuse that id, so
`RemoteNodeRepository::upsert` overwrites the same row instead of
accumulating a new one every time the app restarts.

## Milestone discipline

Each milestone extends existing abstractions rather than rewriting them.
Concretely: new capabilities get a new CMake target (e.g. `gth_network` in
Milestone 9) or a new interface, not a restructuring of `gth_core`/
`gth_application`. If a later milestone needs to change an existing
interface's shape, that's a deliberate, documented decision, not an
incidental refactor.
