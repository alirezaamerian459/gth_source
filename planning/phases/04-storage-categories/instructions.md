# Phase 04 — User-defined storage categories

## Objective

Let users organize existing storage locations into custom-named categories from the Storage tab.

## Approved requirements and acceptance criteria

- Users can create categories with their own names, rename them, and delete them.
- Category names persist across application restarts.
- Users can add or remove any registered local, removable, or network storage from a category.
- One category can contain many storages, and one storage can belong to multiple categories.
- The Storage tab can show all storages or the storages in the selected category.
- Deleting a category removes only that category and its memberships; it preserves storage rows, indexed games, and files.
- Existing storage detection, health display, scanning, and transfer behavior stays unchanged.

## Architecture and data

- Keep the Storage UI dependent on the application storage-service interface.
- Keep SQLite access in the database module and append migration 17; do not edit earlier migrations.
- Store category membership separately from `Storages` to support many-to-many grouping without copying storage records.
- Clean up memberships when a storage is deliberately removed.

## Verification

- Add focused repository and service regression coverage for category CRUD and multi-category membership.
- Build the Windows C++20 targets and run the relevant CTest suite when the configured toolchain is available.
- Record actual verification results in this task list and `planning/PROJECT_STATUS.md`.
