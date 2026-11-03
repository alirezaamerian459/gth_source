# Phase 01 — Requirements and documentation reconciliation

## Objective

Keep one accurate, current project guide set and record product requirements that cannot be inferred safely from repository history.

## Scope

Consolidate current documentation against source and build configuration, retire stale or conflicting guides, correct broken developer/package helpers, and record unresolved owner decisions. Do not implement product features or infer support claims from historical notes.

## Dependencies

The phase 00 foundation exists. The owner has authorized repository cleanup; product requirements remain open in docs/PROJECT_SPECIFICATION.md.

## Requirements

- Keep the repository root focused on the application entrypoint, agent instructions, build configuration, packaging configuration, and source/test directories.
- Maintain one authoritative document for each subject under docs/.
- Keep historical implementation notes out of current operating guidance unless still accurate and useful.
- Preserve Git history and unrelated user work.
- Do not add or expand CAPTCHA, WAF, anti-bot, authentication, DRM, or licensing bypass behavior.
- Do not claim a build, test, or installer succeeds unless it was run.

## Architecture considerations

Preserve the existing Windows C++20 modular architecture. Maintain explicit module dependencies and keep production code behavior unchanged except for removal of the verified-unused network helper files.

## Expected files

README.md, AGENTS.md, docs/, planning/, reports/, .gitignore, installer/package helpers, and any confirmed unused sources or references.

## Implementation strategy

Inspect current code and build configuration first. Consolidate unique valid instructions, remove duplicative or demonstrably stale materials from the working tree, update links, and record unresolved scope questions. Run no application behavior changes as part of this phase.

## Security considerations

Keep secrets and runtime data out of Git. Maintain the host allowlist and human CAPTCHA completion requirement in browser testing. Review production CAPTCHA and external-service paths only after owner scope is confirmed.

## Verification requirements

Review relative documentation links and references to removed files. Record any build, test, or packaging verification only if actually executed. This cleanup pass did not execute those checks.

## Acceptance criteria

- Root README links to the canonical docs.
- No stale root guides or broken root build/release helpers remain.
- Packaging and browser-test documentation matches current paths and scripts.
- Product questions and verification tasks remain explicitly open.

## Definition of Done

Documentation consolidation, cleanup, and reporting are complete. The phase stays open until owner decisions are recorded; Windows build/test verification is tracked in phase 02.
