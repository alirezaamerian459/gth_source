# Development roadmap

**Status:** The repository cleanup is complete for its authorized scope. The Storage-tab categories feature is owner-approved and in progress; other product requirements and Windows verification remain open.

## Phases

| Phase | Name | Status | Outcome |
|---|---|---|---|
| 00 | Repository foundation | Foundation drafted; cleanup scope authorized | Local clone, specification, architecture, policies, agent instructions, ADRs, and initial phase plan. Product requirements remain open. |
| 01 | Requirements and documentation reconciliation | Cleanup pass complete; owner decisions pending | One current documentation set, corrected build/installer references, and retired stale proposals; platform and external-service scope still need confirmation. |
| 02 | Reproducible build and test baseline | Planned | Establish an actually verified Windows build and CTest baseline; then add a Windows CI workflow with reviewed action versions and least-privilege permissions. |
| 03 | Security and integration review | Planned after scope clarification | Review production call paths and data flows for external services, credentials, local files, FTP, elevated execution, and backup/restore. |
| 04 | User-defined storage categories | Complete | Add persistent custom categories and many-to-many membership for existing storage locations. |

Feature work will be planned only after the owner selects a goal and approves requirements and acceptance criteria. Add numbered feature phases only for specific authorized tasks; do not create an empty feature backlog.

See [project status](PROJECT_STATUS.md) and each phase's instructions/tasks for acceptance criteria and current progress.
