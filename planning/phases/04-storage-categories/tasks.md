# Phase 04 tasks — User-defined storage categories

- [x] Add the versioned SQLite migration and repository operations for categories and memberships.
- [x] Add storage-service operations with name validation and safe category deletion.
- [x] Add category selection, management, and storage membership controls to the Storage tab.
- [x] Add repository and service regression coverage for persistence, multiple membership, rename/delete, and storage preservation.
- [x] Update the project specification, database guide, ADR, roadmap, and project status.
- [x] Build the app and run the CTest suite; Release build succeeded with `cmake --build build --config Release --parallel 2`.

  `ctest --test-dir build -C Release --output-on-failure` completed in 14.23 seconds: 503 passed, 6 skipped, 0 failed out of 509 tests. Skips: three Windows Credential Manager integration tests, one live PSPrices test, and two Windows provider live tests.
