# Instructions for coding agents

Before making changes, read [the development policy](docs/DEVELOPMENT_POLICY.md), [project status](planning/PROJECT_STATUS.md), and the active phase's instructions.md and tasks.md. Then read the relevant requirements, architecture, security, and testing documents and inspect the affected code and nearby tests.

- Work only on the user-authorized scope. Do not implement product features until the user approves their requirements and acceptance criteria.
- Preserve the existing C++20, Windows, CMake, and modular architecture. Follow the dependency rules in [the architecture guide](docs/ARCHITECTURE.md); do not restructure modules or change public behavior incidentally.
- Treat open product questions in the project status as unresolved. Do not silently choose between conflicting product requirements.
- Do not add CAPTCHA solving, anti-bot or WAF bypass, authentication/DRM/licensing circumvention, or human-identity impersonation. Existing proposal files are not approval to implement these behaviors. Use human-completed CAPTCHA flows for authorized test targets. Follow applicable safety and service requirements even after an owner scope decision.
- Keep changes focused, preserve unrelated user work, and never overwrite local modifications. Do not commit secrets, tokens, private data, build output, or temporary artifacts.
- Add or update appropriate tests for behavior changes. Run and report only checks actually executed; state clearly when verification was not run or could not run.
- Update the authoritative documentation and phase task status when behavior, interfaces, architecture, or verification changes. Record important architecture choices as ADRs.
- Request owner approval before a major architecture change or destructive operation.
- Do not change the release version, publish releases, push to a remote, or make destructive changes without explicit authorization.

These instructions supplement, and never override, higher-priority system, security, or user instructions.
