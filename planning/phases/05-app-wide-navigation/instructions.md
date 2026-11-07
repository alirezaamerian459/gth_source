# Phase 05 — App-wide navigation usability

## Objective

Reduce the amount of navigation shown at once and give every page a consistent, uncluttered application header.

## Approved requirements and acceptance criteria

- The shared header keeps the active page name, page hint, status, queue shortcut, and navigation toggle available on every page.
- The navigation panel can be shown or hidden; all page views resize to use the available width.
- Navigation destinations remain grouped by task area. Each group can expand or collapse independently, and Workspace is expanded on launch.
- Mouse users can toggle a group by clicking its heading. Keyboard users can focus a heading and toggle it with Space or Enter.
- The selected route, hidden-tab preferences, signed-out restrictions, and existing page action behavior remain unchanged.
- The navigation state is session-only; it does not add a new persisted preference or alter application data.

## Architecture and design

- Keep the change in the Win32/WTL application shell and its small pure sidebar model helper.
- Preserve the existing page views, service contracts, routing indices, and action handlers.
- Use the existing theme palette, owner-drawn navigation style, and modern button control.
- Follow the UI/UX Pro Max principles of clear hierarchy, progressive disclosure, consistent navigation, and keyboard accessibility.

## Verification

- Build the Release app and unit-test targets with the existing Visual Studio 2022 CMake build.
- Run the full CTest suite.
- Launch the Release app for owner review. Interactive visual and keyboard review remains a separate manual check when the owner can inspect the elevated app window.
