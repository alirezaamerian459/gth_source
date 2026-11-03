# ADR-0001: Preserve the existing modular Windows architecture

- **Status:** Accepted as the current repository direction
- **Date:** 2026-11-03

## Context

The repository already has a Windows-only C++20 application with CMake targets organized around core, application, UI, database, storage, network, transfer, verification, and infrastructure concerns. The root architecture notes describe an explicit service container and interface-driven boundaries. Replacing this established design during project initialization would add migration risk without a confirmed requirement.

## Decision

Keep the existing modular monolith, C++20/Win32/WTL stack, and explicit composition root. Make focused additions through current interfaces and module targets. Treat any proposal to change platform, persistence, or architecture as a separate owner-reviewed decision.

## Consequences

- Existing module boundaries and tests remain the baseline.
- New work must justify new dependencies and targets.
- This ADR records current direction; it does not certify every implementation detail or claim runtime verification.
