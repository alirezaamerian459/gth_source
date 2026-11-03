# Phase 03 — Security and integration review

## Objective

Review security-sensitive production call paths and external data flows using the owner-approved product scope.

## Scope

Inspect credentials, logging, archive path handling, update/download verification, FTP, Telegram backup/restore, local network services, elevated execution, browser link import, and any CAPTCHA/anti-bot helpers that the owner confirms are in scope.

## Dependencies

Owner decisions from phase 01, particularly platform and anti-bot scope. Phase 02's build/test baseline is recommended so findings can be reproduced.

## Functional and technical requirements

- Identify source-to-sink data flows and trust boundaries.
- Confirm how tokens are stored and redacted.
- Review path validation, overwrite behavior, checksum validation, cancellation, and failure recovery.
- Review service exposure, firewall changes, authentication, and authorization for FTP and enterprise network functions.
- Record findings with concrete file/function references and severity tied to demonstrated impact.

## Architecture considerations

Preserve current module boundaries. Route remediation through the responsible module and add regression tests for validated defects.

## Expected files

Security findings or review report under reports/, relevant policy/ADR updates, and focused source/test changes only when separately authorized.

## Implementation strategy

First perform source review and trace actual production call sites. Distinguish documentation examples, unused helpers, test code, and reachable runtime behavior. Validate any finding before describing it as exploitable.

## Security considerations

Use test credentials and local test endpoints only. Do not transmit archive data, attack third-party services, bypass access controls, or change firewall/permission settings during review.

## Testing requirements

Use targeted unit/integration tests in an isolated environment for each validated issue. Report what was and was not executed.

## Acceptance criteria

- The review covers approved, reachable flows and records scope/limitations.
- Findings are evidenced and prioritized.
- No unrelated behavior changes are included.

## Definition of Done

The owner has reviewed the findings; approved remediations have tests and accurate documentation, or remaining risks are explicitly accepted/deferred.
