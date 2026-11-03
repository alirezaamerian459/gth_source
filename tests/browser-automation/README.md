# Browser tests

This is an optional Node.js/Playwright test project for web interfaces and service pages the project owner is authorized to test. It is separate from the C++ application and CTest suite.

## Setup

Requirements: Node.js 18 or newer and npm.

~~~powershell
npm install
npx playwright install chromium
Copy-Item .env.example .env
~~~

Set TARGET_URL and ALLOWED_HOSTS in .env. Keep targets limited to systems you own or have permission to test. The allowlist is a navigation guard; it does not grant authorization.

## Commands

- npm run test — run the Node test suite.
- npm run test:integration — run configuration and host-allowlist checks.
- npm run test:manual — open a browser for a manual challenge-completion flow.
- npm run test:captcha — run the configured CAPTCHA workflow test.

The manual flow pauses for a person to complete a real CAPTCHA. This project does not solve or bypass CAPTCHAs, WAFs, authentication, or other access controls.

## Local data

The .env.example file contains safe defaults for local test services. Do not commit .env, persistent browser profiles, logs, screenshots, videos, or credentials. These runtime folders are ignored by the root .gitignore.

For implementation details, see src/config.js, src/CaptchaMonitor.js, and src/PageController.js. Configure only hosts authorized for your test.
