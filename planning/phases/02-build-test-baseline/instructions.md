# Phase 02 — Reproducible build and test baseline

## Objective

Establish an executed, reproducible Windows x64 configure/build/test baseline and document the actual toolchain and results.

## Scope

Verify the existing CMake/MSVC build and CTest suite, resolve build-documentation mismatches, identify failures, and then add a least-privilege Windows CI workflow. Do not change product behavior to make the baseline appear green.

## Dependencies

Phase 01's build and dependency claims must be reconciled. A Windows environment with Visual Studio 2022 C++/ATL components and CMake 3.24+ is required.

## Functional and technical requirements

- Record Windows version, compiler/toolset, CMake version, generator, configuration, and dependency sources.
- Configure and build from a clean build directory.
- Run CTest and record the exact command, test totals, failures, and duration.
- Keep build artifacts outside tracked source and honor existing .gitignore rules.

## Architecture considerations

Do not change module boundaries or dependency versions during baseline execution unless the owner approves a separate fix.

## Expected files

Build documentation, .github/workflows/windows-ci.yml, planning/PROJECT_STATUS.md, phase tasks, and a numbered report in reports/ after results are available.

## Implementation strategy

Use the canonical commands selected in phase 01. Diagnose failures individually and propose narrowly scoped fixes; do not claim verification from configure-only success.

## Security considerations

Review FetchContent sources before first configure. Do not run the elevated application against a real archive as part of baseline testing.

## Testing requirements

The build and CTest run are the objective of this phase. If the required Windows toolchain is unavailable, leave verification incomplete and document the blocker.

## Acceptance criteria

- A clean Windows configure/build completes, or each blocker is documented.
- CTest results are actual and reproducible.
- Commands and environment details are recorded in canonical development docs.
- The CI workflow runs the verified build/test commands with least privilege and reviewed action versions.

## Definition of Done

The baseline status is supported by captured command output or is explicitly marked blocked/unverified with the required environment change.
