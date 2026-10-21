# Quick Reference Guide

Fast reference for common tasks and commands.

## Installation

```powershell
# Navigate to project
cd C:\Users\DrGame\Desktop\research\browser-automation

# Install dependencies
npm install

# Create configuration
Copy-Item .env.example .env

# Edit .env with your settings
notepad .env
```

## Common Commands

```powershell
# Run manual CAPTCHA test (browser visible, you solve CAPTCHA)
npm run test:manual

# Run automated workflow test (test mode signals)
npm run test:captcha

# Run unit tests
npm run test

# Run integration tests
npm run test:integration

# Watch tests for changes
npm run test:watch
```

## Essential Configuration (.env)

```env
# Test target URL
TARGET_URL=http://localhost:8080

# Show browser window (false = headless)
HEADLESS=false

# Allowed domains (security boundary)
ALLOWED_HOSTS=localhost,127.0.0.1

# CAPTCHA completion timeout (5 minutes)
CAPTCHA_COMPLETION_TIMEOUT=300000

# Log level
LOG_LEVEL=info
```

## Quick Test

```powershell
# Step 1: Set basic config
$env:TARGET_URL = "http://localhost:8080"
$env:HEADLESS = "false"

# Step 2: Run test
npm run test:manual

# Step 3: Manually complete CAPTCHA when prompted
# Step 4: Check results in ./logs/ and ./screenshots/
```

## API Quick Start

```javascript
import GTHBrowserAutomation from './src/index.js';

// Create automation instance
const automation = new GTHBrowserAutomation({
  browser: { headless: false },
});

// Run CAPTCHA test
const result = await automation.runCaptchaTest();

// Or run custom sequence
await automation.initialize();
const session = automation.getSessionManager();
await session.executeInteractionSequence([
  { action: 'navigate', url: 'http://localhost:8080' },
  { action: 'click', selector: '#button' },
  { action: 'screenshot', name: 'result' },
]);

// Cleanup
await automation.cleanup();
```

## Debugging

```powershell
# Enable verbose logging
$env:DEBUG = "true"
$env:LOG_LEVEL = "debug"
npm run test:manual

# Keep browser open after test
$env:KEEP_BROWSER_OPEN = "true"
npm run test:manual

# Check latest logs
Get-Content (Get-ChildItem .\logs\ -Latest 1).FullName -Tail 50
```

## File Locations

| Item | Location |
|------|----------|
| Logs | `./logs/automation-*.log` |
| Screenshots | `./screenshots/` |
| Config | `.env` |
| Source | `./src/` |
| Tests | `./tests/` |
| Scripts | `./scripts/` |

## Common Issues

### Browser not opening
```env
HEADLESS=false
DEBUG=true
```
Then check `./logs/` for errors.

### CAPTCHA not detected
- Ensure page has CAPTCHA element
- Add `data-captcha` to CAPTCHA container
- Increase detection timeout:
  ```env
  CAPTCHA_DETECTION_TIMEOUT=30000
  ```

### Completion not detected
- Add `data-captcha-success` signal when CAPTCHA is solved
- Check browser console for JavaScript errors
- Increase completion timeout:
  ```env
  CAPTCHA_COMPLETION_TIMEOUT=600000
  ```

### Timeout errors
- Increase navigation timeout:
  ```env
  NAVIGATION_TIMEOUT=60000
  PAGE_READY_TIMEOUT=20000
  CAPTCHA_DETECTION_TIMEOUT=30000
  CAPTCHA_COMPLETION_TIMEOUT=600000
  ```

## Components

| Component | Purpose |
|-----------|---------|
| `BrowserManager` | Launch/close browser |
| `PageController` | Page navigation & interaction |
| `CaptchaMonitor` | Detect & monitor CAPTCHA |
| `SessionManager` | Manage test session |
| `AutomationLogger` | Logging & diagnostics |

Access via:
```javascript
automation.getBrowserManager()
automation.getPageController()
automation.getCaptchaMonitor()
automation.getSessionManager()
automation.getLogger()
```

## Interaction Actions

| Action | Description | Example |
|--------|-------------|---------|
| `navigate` | Go to URL | `{ action: 'navigate', url: '...' }` |
| `click` | Click element | `{ action: 'click', selector: '#btn' }` |
| `type` | Type text | `{ action: 'type', selector: '#input', text: '...' }` |
| `moveMouse` | Move to element | `{ action: 'moveMouse', selector: '#elem' }` |
| `interact` | Move + click | `{ action: 'interact', selector: '#btn' }` |
| `screenshot` | Take screenshot | `{ action: 'screenshot', name: 'img' }` |
| `waitForElement` | Wait for element | `{ action: 'waitForElement', selector: '.done' }` |
| `waitForCaptcha` | Wait for CAPTCHA | `{ action: 'waitForCaptcha' }` |
| `waitForCaptchaCompletion` | Wait for CAPTCHA completion | `{ action: 'waitForCaptchaCompletion' }` |
| `delay` | Wait time | `{ action: 'delay', ms: 1000 }` |

## Log Format

```
[2025-09-21T14:30:22.431Z] [INFO] [NAVIGATION] Navigated to http://localhost:8080 {"duration_ms":234,"status":"success"}
[2025-09-21T14:30:22.665Z] [INFO] [CAPTCHA] CAPTCHA state changed: DETECTED {"selector":"[data-captcha]"}
[2025-09-21T14:30:37.899Z] [INFO] [CAPTCHA] CAPTCHA state changed: COMPLETED {"elapsed_ms":15234}
```

View logs:
```powershell
Get-Content .\logs\automation-*.log -Tail 100
```

## Environment Variables Reference

```env
# Browser
HEADLESS=false|true
BROWSER_EXECUTABLE_PATH=<path>
VIEWPORT_WIDTH=1280
VIEWPORT_HEIGHT=720
PERSISTENT_CONTEXT=true
USER_DATA_DIR=./session-data
ALLOWED_HOSTS=localhost,127.0.0.1

# Target
TARGET_URL=http://localhost:8080
NAVIGATION_TIMEOUT=30000
PAGE_READY_TIMEOUT=10000

# CAPTCHA
CAPTCHA_DETECTION_TIMEOUT=15000
CAPTCHA_COMPLETION_TIMEOUT=300000
CAPTCHA_TEST_MODE=false|true
CAPTCHA_SUCCESS_SELECTOR=[data-captcha-success]
CAPTCHA_COMPLETION_SELECTOR=[data-captcha-completed]
CAPTCHA_FAILURE_SELECTOR=[data-captcha-failed]

# Interaction
INTERACTION_DELAY=100
MOUSE_MOVE_DELAY=300
CLICK_WAIT_DELAY=200

# Logging
LOG_DIR=./logs
LOG_LEVEL=debug|info|warn|error
SCREENSHOT_ON_ERROR=true|false
SCREENSHOT_DIR=./screenshots
RECORD_VIDEO=false|true
VIDEO_DIR=./videos

# Retry
MAX_RETRIES=3
RETRY_DELAY=1000

# Debug
DEBUG=false|true
NETWORK_DEBUG=false|true
KEEP_BROWSER_OPEN=false|true
```

## Project Structure

```
browser-automation/
├── src/
│   ├── index.js                    # Main entry
│   ├── config.js                   # Configuration
│   ├── AutomationLogger.js         # Logging
│   ├── BrowserManager.js           # Browser control
│   ├── PageController.js           # Page interactions
│   ├── CaptchaMonitor.js           # CAPTCHA monitoring
│   └── SessionManager.js           # Session management
├── tests/
│   ├── CaptchaMonitor.test.js      # Unit tests
│   └── integration.test.js         # Integration tests
├── scripts/
│   ├── manual-interaction-test.js  # Manual test
│   ├── captcha-workflow-test.js    # Workflow test
│   └── custom-interaction-sequence.js
├── logs/                           # Auto-created
├── screenshots/                    # Auto-created
├── session-data/                   # Auto-created
├── .env                            # Your config
├── .env.example                    # Config template
├── package.json                    # Dependencies
├── README.md                       # Full documentation
├── SETUP.md                        # Installation guide
├── CAPTCHA_TESTING.md             # CAPTCHA guide
└── QUICK_REFERENCE.md             # This file
```

## Testing Checklist

- [ ] Node.js installed (`node --version`)
- [ ] npm installed (`npm --version`)
- [ ] Dependencies installed (`npm install`)
- [ ] `.env` configured with `TARGET_URL`
- [ ] Test server running
- [ ] `npm run test:manual` executes
- [ ] Browser opens with test page
- [ ] CAPTCHA detected (or message shown)
- [ ] Logs created in `./logs/`
- [ ] Screenshots created in `./screenshots/`

## Common Workflows

### Manual CAPTCHA Testing
```powershell
npm run test:manual
# Browser opens → Manual complete CAPTCHA → Results logged
```

### Automated Testing
```powershell
$env:CAPTCHA_TEST_MODE = "true"
npm run test:captcha
# Runs with test mode signals
```

### Custom Workflow
```powershell
# Edit scripts/custom-interaction-sequence.js
npm run test:integration
```

### Debug Mode
```powershell
$env:DEBUG = "true"
$env:KEEP_BROWSER_OPEN = "true"
npm run test:manual
```

## Version Info

- **Node.js:** 18.0.0+
- **Playwright:** 1.40.0+
- **Framework:** Vanilla JavaScript (no framework dependencies)

## Support

1. Check `.env` configuration
2. Review `./logs/` for detailed errors
3. Check `./screenshots/` for visual state
4. Run with `DEBUG=true` for verbose output
5. See README.md for full documentation
