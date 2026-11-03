# Phase 00 — Repository foundation

## Objective

Create a reviewable development foundation for the existing repository, preserve its established implementation, and record verified facts separately from unverified documentation claims.

## Scope

Included: local Git clone, source/documentation inventory, project entrypoint, specification, technology and architecture summaries, engineering policies, agent instructions, initial ADRs, roadmap, and status report.

Excluded: application behavior changes, source refactors, dependency changes, release packaging, CI activation, remote pushes, and claims that the program or tests work.

## Dependencies

None. The source repository and public project URL were supplied by the owner.

## Requirements

- Preserve the source tree, Git history, current branch, and remote configuration.
- Keep detailed existing architecture notes available at their original path.
- Record platform/security/documentation conflicts as open questions.
- Do not treat historical guides as approval for new product behavior.

## Architecture considerations

Keep the existing Windows C++20 modular monolith. Add only project-governance documentation and a root README entrypoint. Do not reorganize source modules or replace existing architecture notes.

## Expected files

AGENTS.md, README.md, docs/ project/policy/architecture files, docs/decisions/, planning/ status and phase files, and reports/01-project-foundation.md.

## Implementation strategy

Inspect Git state and current documents; create concise, project-specific guidance; link rather than duplicate existing technical documentation; preserve all source files and remote history.

## Security considerations

Do not expose credentials or alter project behavior. Record the CAPTCHA/anti-bot conflict and defer changes to that scope. Do not push any local changes.

## Testing requirements

No application tests are required for documentation-only changes. Inspect the resulting Git diff and status and report that no build or tests were run.

## Acceptance criteria

- Local clone retains main history and origin.
- Project has an actionable root README and concise AGENTS.md.
- Specification, policies, architecture overview, ADRs, status, and roadmap reflect this repository.
- Conflicts and unverified claims are clearly labeled.
- No application source has changed and no remote write has occurred.

## Definition of Done

The foundation documents are present and internally linked, the working-tree changes are reviewable, and the owner has reviewed the open requirements. The final owner-review item remains pending.
