# JB FTP and Proxy Sharing — 2026-11-11

## Behavior

- The FTP tab's **Host a Server** section now has **Include JB Library (read-only)** enabled by default.
- When enabled, the hosted FTP server keeps the chosen Shared Folder exactly as before and adds a virtual `/JB Library` tree. Each current JB game is mapped to its real path; browsing and downloading are allowed, while `STOR` into the JB tree is rejected.
- The existing LAN Console Proxy now consults the same JB catalog after the normal indexed-library lookup. Matching `.pkg`, `.fpkg`, `.pup`, and supported archive/image files are served locally; unmatched requests still follow the existing origin path.
- Toggling JB FTP sharing while the server is running only affects the next restart, so active FTP sessions and existing upload/download tasks are not interrupted.
- FTP traversal remains confined to the configured root and explicitly mapped JB paths.

## Verification

- FTP integration tests: 9/9 passed, including browse/download/read-only JB mapping and existing root upload/download/path-escape behavior.
- JB proxy fallback test passed: a JB `.fpkg` request was served locally and never reached the origin.
- Existing LocalGameServer/proxy tests passed in the focused run.
- Debug application and unit-test targets build successfully.
- FTP tab's existing client/server and proxy controls remain available; no FTP task is stopped or rewritten when JB sharing is disabled.
