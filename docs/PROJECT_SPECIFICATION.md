# Project specification

**Status:** Repository-derived baseline. Product claims below describe documented intent and structure; they are not a runtime certification.

## Purpose and users

Game Transfer Hub (GTH) is a Windows desktop application for people who organize PlayStation game files across local or network storage. The repository includes a native C++ application, SQLite-backed catalog, storage/indexing, update, transfer, network, backup, and monitoring modules.

The precise supported console generations, archive layouts, external-service terms, and behavior of each user-facing feature remain subject to implementation review and owner confirmation.

## Environment and constraints

- Target: Windows x64, using C++20, Win32, WTL/ATL, and CMake.
- Build environment: Visual Studio 2022 or Build Tools with C++/ATL components; CMake 3.24 or newer.
- Persistence: SQLite with schema migrations in src/database/src/Migrator.cpp.
- The application requests administrator privileges in its current app manifest/build configuration. Changing this needs a security and compatibility review.
- Update downloads and backups can involve large files, local archive data, credentials, and external services.

## Documented capability areas

The source tree and current documentation contain code for:

1. Local storage detection, scanning, and game catalog/indexing.
   - Owner-approved Storage-tab categories let users create named groups and assign any known storage to multiple groups.
2. Update metadata, downloads, verification, and archive replacement.
3. File transfers and FTP-related connections.
   - The FTP host can optionally expose the read-only JB catalog as `/JB Library`, and the LAN Console Proxy can serve matching JB package/archive files while preserving ordinary FTP and origin-fallback behavior.
4. Network discovery/reachability, local services, and remote health reporting.
5. Database-backed settings, logs/reports, storage health, and backup transport.

These are areas present in the repository, not guarantees that every flow is complete, supported, or verified. Consult source and tests before making user-facing claims.

## Security and product boundaries

- Treat package metadata, archive paths, external responses, and imported URLs as untrusted input.
- Preserve archive integrity and avoid unsafe paths, partial replacements, or silent data loss.
- Keep credentials out of source, examples, logs, and reports. Use the existing credential-store abstraction where applicable.
- Honor service access controls and terms. Do not add authentication, DRM, licensing, CAPTCHA, WAF, or anti-bot bypass behavior.
- The browser test project must target hosts the owner is authorized to test. A real challenge is completed by a human.
- Production CAPTCHA-related code and external-service scope require a separate review before modification.

## Open requirements

1. Confirm the supported PlayStation generations and package/archive formats. Conflicting historical claims were removed from the current documentation; parser support has not been certified.
2. Confirm the approved production scope for CAPTCHA-related integrations and external-site automation. The old bypass proposal documents were removed from the working tree; Git history retains them. Existing production call paths still need review.
3. Confirm which external services are intended to remain supported and what data may be sent to them.
4. Establish an actually verified Windows build and CTest baseline. The documented commands are not a record of a successful run.
5. Select the next product goal after phase 04 and define its requirements and acceptance criteria.

The Storage-tab category feature is approved and tracked in [phase 04](../planning/phases/04-storage-categories/instructions.md). Its acceptance criteria and many-to-many storage membership are recorded in [ADR-0003](decisions/0003-storage-categories.md).

The JB sharing behavior is recorded in [ADR-0004](decisions/0004-jb-library-sharing.md).

Record approved changes here and in the relevant ADR. Historical notes or code samples do not constitute product approval.
