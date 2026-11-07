# Phase 05 tasks — App-wide navigation usability

- [x] Replace the all-visible navigation list with accessible expandable groups.
- [x] Add a shared hamburger control and reflow page views when the navigation panel is hidden.
- [x] Preserve route indices, tab visibility rules, page actions, theme behavior, and queue navigation.
- [x] Add unit coverage for collapsed and expanded navigation rows.
- [x] Build the Release app and unit-test targets: `cmake --build build --config Release --target GameTransferHubApp gth_unit_tests --parallel 2` succeeded.
- [x] Run CTest: `ctest --test-dir build -C Release --output-on-failure` completed with 506 passed, 6 skipped, and 0 failed out of 512 tests.
- [ ] Launch the rebuilt app and complete visual/keyboard review. Windows returned `0xC0000142` for three launch attempts, so the rebuilt app is not currently open for review.

Logs:

- Build: `work/ui-refresh-build.log`
- Tests: `work/ui-refresh-ctest.log`
