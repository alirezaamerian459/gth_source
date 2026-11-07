# Development roadmap

**Status:** The repository cleanup and Storage-tab categories are complete. The owner has approved an app-wide navigation usability refresh; other product requirements and Windows CI/security review remain open.

## Phases

| Phase | Name | Status | Outcome |
|---|---|---|---|
| 00 | Repository foundation | Foundation drafted; cleanup scope authorized | Local clone, specification, architecture, policies, agent instructions, ADRs, and initial phase plan. Product requirements remain open. |
| 01 | Requirements and documentation reconciliation | Cleanup pass complete; owner decisions pending | One current documentation set, corrected build/installer references, and retired stale proposals; platform and external-service scope still need confirmation. |
| 02 | Reproducible build and test baseline | Planned | Establish an actually verified Windows build and CTest baseline; then add a Windows CI workflow with reviewed action versions and least-privilege permissions. |
| 03 | Security and integration review | Planned after scope clarification | Review production call paths and data flows for external services, credentials, local files, FTP, elevated execution, and backup/restore. |
| 04 | User-defined storage categories | Complete | Add persistent custom categories and many-to-many membership for existing storage locations. |
| 05 | App-wide navigation usability | Implementation and tests complete; app launch blocked | Reduce navigation overload with expandable page groups, a shared menu toggle, and a consistent page header while preserving existing page actions. |

Add numbered feature phases only for specific owner-authorized tasks; do not create an empty feature backlog.

See [project status](PROJECT_STATUS.md) and each phase's instructions/tasks for acceptance criteria and current progress.
