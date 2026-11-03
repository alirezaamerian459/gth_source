# Coding standards

Follow the conventions already used by the adjacent module and file. The [architecture document](ARCHITECTURE.md) includes WTL and composition-root rules.

## C++

- Compile as C++20 with extensions disabled, as configured by the root CMake project.
- Keep filenames and type names aligned with the current PascalCase naming pattern; use the neighboring code's method, member, and constant conventions rather than introducing a second style.
- Use RAII and standard-library ownership types. Make ownership and resource lifetime explicit, especially for handles, sockets, files, and Win32 resources.
- Keep functions focused and make error paths explicit. Avoid hidden global state and unrelated responsibilities in UI views.
- Preserve module boundaries: UI -> application interfaces -> core contracts; concrete adapters are composed in infrastructure.
- Add a named local CRect for WTL/ATL Create() calls that accept ATL::_U_RECT; do not pass a temporary rectangle. Apply the same care to APIs taking non-const RECT&.
- Avoid new macros, dependencies, and abstractions unless they solve a concrete requirement.
- Comments should explain constraints or reasoning, not restate the code.

## CMake

- Put module sources and link dependencies in that module's CMakeLists.txt.
- Keep public include directories and linked targets intentional.
- Preserve the Windows target guard and the required C++ standard.
- Do not commit generated build trees or fetched dependency artifacts.

## Tests

- Follow the existing GoogleTest naming and fixture style.
- Keep test names behavior-focused; use test_<area>.cpp for new unit-test files.
- Prefer deterministic tests with fakes or local resources; do not make ordinary unit tests depend on real drives, live services, or user credentials.

## JavaScript browser tests

The optional browser-automation project is separate from the C++ build. Follow its package scripts and allowlist configuration. Keep tests scoped to authorized local/test hosts and use manual CAPTCHA completion when a real challenge appears.

## Formatting and static analysis

No repository-wide formatter or linter configuration is currently documented as authoritative. Before introducing one, inspect current formatting, choose a compatible tool, and propose the configuration as a focused change.
