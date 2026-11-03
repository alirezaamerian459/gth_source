# Development policy

This is the authoritative engineering policy for changes in this repository. It complements the [architecture guide](ARCHITECTURE.md) and the language-specific [coding standards](CODING_STANDARDS.md).

## Scope and change behavior

- Confirm the requested behavior and acceptance criteria before implementing a significant feature.
- Inspect the current implementation, tests, and documentation first; follow established module patterns.
- Keep each change focused. Preserve existing files and unrelated local changes; do not silently overwrite or reorganize them.
- Do not implement business features until their scope is authorized. Do not silently change the approved architecture, supported platforms, release version, or external-service behavior.
- Treat contradictions recorded in planning/PROJECT_STATUS.md as open questions, not as permission to choose a side.

## Code and architecture

- Preserve the Windows-only C++20 modular-monolith architecture and explicit interface boundaries.
- Keep core and application responsibilities distinct from storage, network, database, and UI implementation details.
- Favor readable code, RAII, narrow responsibilities, explicit error handling, and existing conventions over extra abstractions.
- Add a dependency or CMake target only when the authorized requirement needs it; document material tradeoffs.
- Update public-interface and architecture documentation when a change affects them.

## Security and privacy

- Never hardcode or commit credentials, tokens, cookies, private keys, user data, or production configuration.
- Use the existing operating-system credential storage where appropriate; redact secrets and sensitive paths from logs.
- Validate external input and handle untrusted files, network responses, and archive paths defensively.
- Preserve least privilege, TLS certificate checks, checksum verification, and safe archive replacement.
- Do not add authentication, authorization, DRM, licensing, CAPTCHA, WAF, or anti-bot circumvention. The conflicting historical CAPTCHA documents must be resolved by the project owner before related behavior changes.
- Do not upload or transmit archive data, credentials, or personal information unless the user explicitly authorized the specific data and destination.

## Tests and verification

- Add regression coverage for significant behavior and defects using the existing test framework.
- Run the relevant build/tests when required by the task and applicable instructions. Report exact commands and actual outcomes.
- Separate static inspection from executed verification. Never claim a build, test, scan, or runtime check passed unless it was run.
- If environment constraints prevent verification, report the blocker and leave the task unverified.

## Documentation and continuity

- Keep one authoritative document per subject. Use root historical guides as references until reconciled; avoid copying their content into multiple new documents.
- Update specifications, ADRs, roadmap tasks, and reports when requirements, architecture, verification, or status changes.
- Keep planning status factual: proposed, approved, in progress, verified, and complete are distinct states.

## Git and dependencies

- Use small, meaningful commits and focused branches when work spans multiple changes.
- Prefer branch names such as **docs/topic**, **fix/topic**, or **feat/topic**, and concise commit subjects such as **docs:**, **fix:**, or **feat:**; keep these conventions subordinate to repository or user instructions.
- Review git status and diffs before editing or committing; keep generated build output ignored.
- Never commit secrets, local data, binaries, or temporary debugging files.
- Do not push to a remote, create remote resources, publish a release, or change deployment settings without explicit authorization.
- Review dependency source, version, licensing, and security impact before adding or upgrading packages.

## Completion

A change is complete only when its authorized acceptance criteria are met, appropriate verification is reported accurately, relevant documentation is updated, and unresolved risks or limitations are stated.
