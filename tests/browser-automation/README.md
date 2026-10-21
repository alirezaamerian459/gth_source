# GTH Browser Automation Framework

A Playwright-based browser automation module for testing Game Transfer Hub (GTH) web components and CAPTCHA integration. Built for internal testing of your own website and CAPTCHA implementation.

## Features

- **Playwright-based automation** - Real browser instance with Chromium/Chrome
- **CAPTCHA testing workflow** - Detect, wait for manual completion, and verify
- **Mouse/keyboard interactions** - Realistic user interaction simulation
- **Session management** - Persistent context for test session continuity
- **Comprehensive logging** - Timestamped logs with screenshots and state tracking
- **Security boundaries** - Only allowed hosts can be tested
- **Headless & headed mode** - Debug with visible browser window
- **Configuration management** - Environment-based configuration for flexibility

## Architecture

```
GTH Browser Automation
├── BrowserManager      - Manages browser lifecycle
├── PageController      - Handles page interactions
├── CaptchaMonitor      - Detects & monitors CAPTCHA state
├── SessionManager      - Maintains test session
├── AutomationLogger    - Logging & diagnostics
└── Configuration       - Centralized settings management
```

## Installation

### Prerequisites

- Node.js 18.0.0 or higher
- npm

### Setup Steps

1. **Clone/copy the module to your GTH test directory:**

```bash
cd C:\Users\DrGame\Desktop\research
# (already in browser-automation directory)
```

2. **Install dependencies:**

```bash
npm install
```

This installs:
- `playwright` - Browser automation framework
- `dotenv` - Environment variable management

3. **Create `.env` file:**

```bash
cp .env.example .env
```

Edit `.env` with your test configuration:

```env
# Browser
HEADLESS=false
ALLOWED_HOSTS=localhost,127.0.0.1,gth-test.internal

# Target
TARGET_URL=http://localhost:8080/test-page

# CAPTCHA
CAPTCHA_DETECTION_TIMEOUT=15000
CAPTCHA_COMPLETION_TIMEOUT=300000

# Logging
LOG_LEVEL=info
SCREENSHOT_ON_ERROR=true
```

## Quick Start

### 1. Manual CAPTCHA Test (with visible browser)

```bash
npm run test:manual
```

This runs an interactive test where:
1. Browser window opens and navigates to your test page
2. Automation detects the CAPTCHA
3. You manually complete the CAPTCHA in the browser
4. Automation verifies completion
5. Screenshots are saved to `./screenshots/`
6. Logs are saved to `./logs/`

### 2. Automated CAPTCHA Workflow Test

```bash
npm run test:captcha
```

Runs the full workflow assuming your test page has automated CAPTCHA signals (test mode).

### 3. Custom Interaction Sequence

```bash
npm run test:integration
```

Runs example custom interaction sequences.

## Usage Examples

### Basic CAPTCHA Test

```javascript
import GTHBrowserAutomation from './src/index.js';

async function testCaptcha() {
  const automation = new GTHBrowserAutomation({
    browser: { headless: false },
  });

  try {
    const result = await automation.runCaptchaTest();
    console.log('Test result:', result);
  } finally {
    await automation.cleanup();
  }
}

testCaptcha();
```

### Custom Interaction Sequence

```javascript
import GTHBrowserAutomation from './src/index.js';

async function customTest() {
  const automation = new GTHBrowserAutomation();

  try {
    await automation.initialize();
    const session = automation.getSessionManager();

    const steps = [
      { action: 'navigate', url: 'http://localhost:8080/login' },
      { action: 'type', selector: '#username', text: 'testuser' },
      { action: 'type', selector: '#password', text: 'testpass' },
      { action: 'click', selector: '#login-button' },
      { action: 'waitForElement', selector: '.dashboard' },
      { action: 'screenshot', name: 'dashboard' },
    ];

    const results = await session.executeInteractionSequence(steps);
    console.log('Steps completed:', results);
  } finally {
    await automation.cleanup();
  }
}

customTest();
```

### Advanced Page Control

```javascript
import GTHBrowserAutomation from './src/index.js';

async function advancedTest() {
  const automation = new GTHBrowserAutomation();

  try {
    await automation.initialize();
    const page = automation.getPageController();

    // Navigate
    await page.navigate('http://localhost:8080');
    await page.waitForPageReady();

    // Interact
    await page.moveMouse('#captcha-button');
    await page.click('#captcha-button');

    // Check state
    const text = await page.getElementText('.success-message');
    console.log('Success:', text);

    // Screenshot
    await page.takeScreenshot('final-state');
  } finally {
    await automation.cleanup();
  }
}

advancedTest();
```

## Configuration

### Environment Variables

Copy `.env.example` to `.env` and customize:

#### Browser
- `HEADLESS` - Launch in headless mode (false for debugging)
- `BROWSER_EXECUTABLE_PATH` - Custom Chrome path
- `VIEWPORT_WIDTH`, `VIEWPORT_HEIGHT` - Window size
- `PERSISTENT_CONTEXT` - Reuse session across navigation
- `ALLOWED_HOSTS` - Security boundary (comma-separated)

#### Target
- `TARGET_URL` - Base URL for test target
- `NAVIGATION_TIMEOUT` - How long to wait for page load
- `PAGE_READY_TIMEOUT` - How long to wait for networkidle

#### CAPTCHA
- `CAPTCHA_DETECTION_TIMEOUT` - How long to wait for CAPTCHA to appear
- `CAPTCHA_COMPLETION_TIMEOUT` - How long to wait for manual completion
- `CAPTCHA_TEST_MODE` - Enable test mode signals

#### Logging
- `LOG_LEVEL` - debug, info, warn, error
- `LOG_DIR` - Where to save logs
- `SCREENSHOT_DIR` - Where to save screenshots
- `SCREENSHOT_ON_ERROR` - Auto-capture on errors

#### Interaction
- `INTERACTION_DELAY` - Delay between actions (ms)
- `MOUSE_MOVE_DELAY` - How long mouse moves take (ms)
- `CLICK_WAIT_DELAY` - Wait after clicking (ms)

## Interaction Actions

In custom sequences, supported actions:

- `navigate` - Go to URL
- `waitForElement` - Wait for element to appear
- `click` - Click element
- `type` - Type text into input
- `moveMouse` - Move mouse to element
- `interact` - Move mouse + click (realistic)
- `screenshot` - Take screenshot
- `delay` - Wait (ms)
- `waitForCaptcha` - Wait for CAPTCHA to appear
- `waitForCaptchaCompletion` - Wait for CAPTCHA completion

## CAPTCHA Test Mode

For automated testing without manual CAPTCHA completion, your test page should expose signals:

```html
<!-- CAPTCHA detected -->
<div data-captcha></div>

<!-- CAPTCHA completed successfully -->
<div data-captcha-success></div>

<!-- CAPTCHA failed/expired -->
<div data-captcha-failed></div>

<!-- Generic completion signal -->
<div data-captcha-completed></div>
```

Enable test mode in `.env`:
```env
CAPTCHA_TEST_MODE=true
CAPTCHA_SUCCESS_SELECTOR=[data-captcha-success]
```

## Logging & Debugging

### Log Files

Logs are saved to `./logs/` with session ID:
```
logs/
└── automation-2025-09-21_143022-431.log
```

Each log contains:
- Navigation events with timing
- CAPTCHA state changes
- Interaction events
- Timeouts and errors
- Session start/end

### Screenshots

Screenshots are saved to `./screenshots/` on errors or manually:
```
screenshots/
├── automation-2025-09-21_143022-431_initial-page.png
├── automation-2025-09-21_143022-431_captcha-visible.png
└── automation-2025-09-21_143022-431_after-completion.png
```

### Verbose Logging

Enable debug output in `.env`:
```env
DEBUG=true
LOG_LEVEL=debug
NETWORK_DEBUG=true
```

## Running Tests

### Unit Tests (CaptchaMonitor)

```bash
npm run test
```

Tests:
- CaptchaMonitor state transitions
- Configuration validation
- Host security checks

### Integration Tests

```bash
npm run test:integration
```

Tests configuration and integration points.

### Manual Test (Interactive)

```bash
npm run test:manual
```

- Opens browser
- Navigates to test page
- Waits for you to complete CAPTCHA
- Verifies completion
- Takes screenshots

### Workflow Test

```bash
npm run test:captcha
```

- Runs full CAPTCHA workflow
- Uses test mode signals if enabled

## Troubleshooting

### Chrome/Chromium Not Found

**Error:** `Failed to launch browser: Executable not found`

**Solution:** Set path in `.env`:
```env
BROWSER_EXECUTABLE_PATH=C:\Program Files\Google\Chrome\Application\chrome.exe
```

Or install Playwright browsers:
```bash
npx playwright install chromium
```

### Navigation Timeout

**Error:** `Navigation failed to http://...`

**Increase timeout in `.env`:**
```env
NAVIGATION_TIMEOUT=60000
```

### CAPTCHA Not Detected

**Error:** `CAPTCHA not detected, continuing`

**Check:**
1. Page actually has CAPTCHA element
2. Selector matches CAPTCHA framework
3. Increase `CAPTCHA_DETECTION_TIMEOUT`
4. Enable `DEBUG=true` to see what selectors are checked

### Manual Completion Timeout

**Error:** `CAPTCHA completion timeout`

**Increase in `.env`:**
```env
CAPTCHA_COMPLETION_TIMEOUT=600000
```

Or complete CAPTCHA faster in browser.

## File Structure

```
browser-automation/
├── src/
│   ├── index.js              # Main entry point
│   ├── config.js             # Configuration management
│   ├── AutomationLogger.js    # Logging
│   ├── BrowserManager.js      # Browser lifecycle
│   ├── PageController.js      # Page interactions
│   ├── CaptchaMonitor.js      # CAPTCHA monitoring
│   └── SessionManager.js      # Session management
├── tests/
│   ├── CaptchaMonitor.test.js # Unit tests
│   └── integration.test.js    # Integration tests
├── scripts/
│   ├── manual-interaction-test.js    # Manual test
│   ├── captcha-workflow-test.js      # Workflow test
│   └── custom-interaction-sequence.js # Custom sequences
├── logs/                      # Test logs (auto-created)
├── screenshots/               # Test screenshots (auto-created)
├── package.json              # Dependencies
├── .env.example              # Configuration template
└── README.md                 # This file
```

## Security Boundaries

**The implementation only targets configured allowed hosts.** This prevents accidental or malicious automation against external services.

**Allowed hosts are configured in:**

```env
ALLOWED_HOSTS=localhost,127.0.0.1,gth.local,gth-test.internal
```

**Any URL outside this list will be rejected** with an error message.

## Important Notes

- **Test mode only:** This automation framework is designed for testing your own website/CAPTCHA implementation
- **No CAPTCHA bypass:** This does not implement or attempt to bypass CAPTCHA systems
- **Manual interaction:** When real CAPTCHA is present, automation pauses and waits for human interaction
- **Session persistence:** Cookies and session data are maintained across navigation in persistent mode
- **Realistic interactions:** Mouse movements and timing are configurable for realistic behavior

## Component Overview

### BrowserManager

Manages browser lifecycle:
- Launch/close browser
- Create contexts (persistent/non-persistent)
- Handle browser errors

```javascript
const browserManager = automation.getBrowserManager();
await browserManager.launch();
await browserManager.createPage();
```

### PageController

Handles page interactions:
- Navigation with validation
- Mouse movements and clicks
- Text input
- Element detection
- Screenshots

```javascript
const page = automation.getPageController();
await page.navigate(url);
await page.click('#button');
await page.type('#input', 'text');
```

### CaptchaMonitor

Detects and monitors CAPTCHA state:
- Presence detection
- State tracking (DETECTED, WAITING, COMPLETED, FAILED)
- Timeout handling

```javascript
const monitor = automation.getCaptchaMonitor();
await monitor.waitForCaptcha();
const state = monitor.getState();
```

### SessionManager

Manages overall test session:
- Initialization
- Workflow execution
- Cleanup

```javascript
const session = automation.getSessionManager();
await session.executeInteractionSequence(steps);
```

### AutomationLogger

Comprehensive logging system:
- File-based logging
- Console output
- Screenshots on error
- Session tracking

```javascript
const logger = automation.getLogger();
logger.info('TEST', 'Message');
logger.saveScreenshot(data, 'name');
```

## API Reference

See individual module files for detailed API documentation:
- `src/BrowserManager.js` - Browser control
- `src/PageController.js` - Page interactions
- `src/CaptchaMonitor.js` - CAPTCHA monitoring
- `src/SessionManager.js` - Session management
- `src/AutomationLogger.js` - Logging

## Support & Debugging

1. **Check logs:** `./logs/automation-*.log`
2. **Review screenshots:** `./screenshots/`
3. **Enable debug mode:** `DEBUG=true` in `.env`
4. **Keep browser open:** `KEEP_BROWSER_OPEN=true` to debug live
5. **Check configuration:** Verify `.env` values match your setup

## Development

### Running Tests

```bash
npm run test              # Run all tests
npm run test:watch       # Watch mode
npm run test:manual      # Manual CAPTCHA test
npm run test:captcha     # Workflow test
```

### Project Structure

Each module is self-contained:
- Single responsibility principle
- Dependency injection for testability
- Comprehensive error handling
- Detailed logging at each step

## License

MIT

## Credits

Built for Game Transfer Hub (GTH) internal testing.
Powered by Playwright and Node.js.
