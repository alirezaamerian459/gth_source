# ADR-0002: CAPTCHA and external-site automation scope

- **Status:** Pending owner decision
- **Date:** 2026-11-03

## Context

The repository had proposal documents describing CAPTCHA, Cloudflare, and anti-bot bypass. Those proposals conflicted with the test project's manual challenge flow and were removed from the current working tree as unapproved guidance. The original files remain in Git history.

The current test project is documented for authorized hosts and human completion of real CAPTCHA challenges. Production CAPTCHA-related code and the intended boundaries for external-site integrations have not been fully reviewed.

## Interim rule

Do not add, enable, or expand CAPTCHA solving, WAF/anti-bot bypass, access-control bypass, or human-identity impersonation. Authorized browser tests may wait for a person to complete a real challenge.

## Owner decision required

Confirm the intended production role, if any, of the CAPTCHA-related module and the permitted scope for external-site automation. Update this ADR and the project specification after the decision.

## Consequences

Unrelated archive, storage, UI, build, and documentation work can proceed within the approved scope. CAPTCHA-related production changes remain out of scope until this record is resolved.
