# ADR-0003: User-defined storage categories

**Status:** Accepted by the project owner on 2026-11-07

## Context

The Storage tab currently presents all known locations as one flat list. The owner wants custom-named groups and the ability to place multiple storage locations into a group. A storage may also belong to any number of groups.

## Decision

- Persist category names as first-class rows, separate from storage locations.
- Persist category membership in a many-to-many join table.
- Let users create, rename, and delete categories, assign or remove existing storages, and view all storages or one selected category.
- Deleting a category removes its memberships only. It never deletes the storage location or indexed game data.
- Do not change storage detection, scanning, transfers, or archive behavior.

## Consequences

- A versioned SQLite migration adds category and membership tables.
- The application service and repository expose category operations through existing module boundaries.
- UI and persistence tests cover duplicate/empty names, multiple memberships, rename/delete behavior, and storage preservation.
