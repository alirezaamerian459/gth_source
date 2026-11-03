# Testing policy

## Existing test layers

- **C++ unit tests:** GoogleTest under tests/unit, enabled through CMake and CTest.
- **Browser automation:** a separate Node.js/Playwright project under tests/browser-automation, documented for authorized test hosts and manual CAPTCHA completion.
- **Build validation:** Windows x64 with the Visual Studio 2022 CMake generator and MSVC/ATL.

## Expectations

- Add unit tests for significant domain, parsing, storage, transfer, update, and data-integrity behavior.
- Add regression tests for defects and important boundary cases: malformed/truncated package metadata, absent or disconnected storage, interrupted transfers, checksum mismatch, database migration/recovery, and network failure where relevant.
- Use temporary directories, in-memory databases, fakes, or local test servers. Do not let ordinary tests modify a user's archive, database, credentials, or real drives.
- Keep tests deterministic and avoid dependence on live Sony, Telegram, PSPrices, or community services unless a separately authorized integration test specifically requires them.
- Browser tests must use configured allowlisted hosts. Real CAPTCHA challenges require human completion; automated CAPTCHA bypass is out of scope pending owner clarification.
- Distinguish test count, coverage, and correctness. A reported test count is not proof that tests currently pass.

## Standard local commands

~~~powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
~~~

Run Release verification as well when the change affects packaging, optimization-sensitive behavior, or release readiness. Use the browser-automation project's own documented scripts only when that project is in scope.

## Reporting

Report the exact commands and actual result. If tests were not run, say so. If a test is blocked, include the reason and leave the verification task open. Do not infer success from documentation, code review, or a successful configure alone.

## CI

No CI workflow was present in the inspected root. Phase 02 should add a Windows build/test workflow after the local build baseline is established, with reviewed action versions, least-privilege workflow permissions, and documented dependency-download behavior. Do not claim the workflow passes until it has run.
