# Security policy

## Application and archive safety

- Treat package files, archive paths, metadata, network responses, imported URLs, and user-supplied configuration as untrusted input.
- Validate paths and file operations against intended archive/storage roots. Avoid path traversal, unsafe overwrite, and partial replacement.
- Preserve checksum verification before downloaded data is committed and use safe temporary/atomic replacement where the implementation supports it.
- Handle cancellation, disk-full conditions, interrupted transfers, and corrupted input without silently damaging existing archive data.
- Minimize administrator privileges where possible. The current guides say the app requests elevation; changing that behavior needs explicit threat and compatibility review.

## Credentials and sensitive data

- Never commit tokens, passwords, cookies, signing material, user archives, or real logs.
- Use Windows Credential Manager or another approved secret store for long-lived credentials. Do not copy credentials into plaintext config or documentation.
- The local admin sign-in stores and verifies the first non-empty user-selected credential in Windows Credential Manager before unlocking; the application has no built-in default password.
- Redact tokens, authorization headers, cookies, private identifiers, and sensitive local paths from logs and error reports.
- Before changing backup, upload, or restore behavior, document exactly what data leaves the machine, its destination, retention, and recovery path.
- Keep sample environment files free of real values.

## Network and external services

- Keep TLS certificate validation enabled. Pin or restrict trust only where the current protocol and documented service requirements justify it.
- Validate redirects, response sizes, status codes, manifests, hashes, and downloaded content before use.
- Use reasonable retry and rate behavior; honor service terms and access controls.
- Do not bypass authentication, authorization, DRM, licensing, CAPTCHA, WAF, or anti-bot controls. Automated CAPTCHA testing may pause for a human on an authorized test site.
- Architecture and browser-test guidance prohibit access-control bypass. Production CAPTCHA-related code and external-site automation scope remain open for owner review; do not change those paths before that review.

### Known transport constraints requiring follow-up

- Sony update and package metadata currently includes legacy HTTP URLs. The app preserves them for compatibility; they do not provide TLS transport integrity. Do not replace them with HTTPS until the endpoint and certificate are verified, and treat this as an unresolved risk.
- The FTP protocol option can send credentials and data without encryption. FTPS is available and should be selected when the server supports it; plain FTP remains for compatibility and is an unresolved risk.
- The local caching proxy supports HTTP requests as requested by its client. Those requests are not encrypted by the proxy.

## Dependencies and release

- Review dependency versions, provenance, licenses, and known vulnerabilities before adopting or upgrading them.
- Do not publish binaries or releases until the supported platforms, dependency pins, build instructions, and verification results are reconciled.
- Keep build artifacts and runtime data out of Git.

## Incident handling

If a committed credential or sensitive archive data is found, stop further exposure, notify the owner promptly, and recommend revocation/rotation and history cleanup as appropriate. Do not reproduce the secret in chat or reports.
