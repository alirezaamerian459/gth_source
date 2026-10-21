# Browser Automation Testing - GTH Integration

Browser automation framework for testing GTH's CAPTCHA integration and web components.

**Location in GTH:** `tests/browser-automation/`

## Quick Start

### 1. Install

```powershell
cd C:\Users\DrGame\Desktop\local\tests\browser-automation
npm install
```

### 2. Configure

```powershell
Copy-Item .env.example .env
# Edit .env with GTH test settings
```

Minimum `.env`:
```env
TARGET_URL=http://localhost:8080
HEADLESS=false
ALLOWED_HOSTS=localhost,127.0.0.1
```

### 3. Test

```powershell
# Run GTH CAPTCHA test
node scripts/gth-captcha-test.js

# Or via npm
npm run test:manual
```

## What's Included

✅ **Framework**
- BrowserManager - Browser control
- PageController - Page interactions
- CaptchaMonitor - CAPTCHA detection
- SessionManager - Test orchestration
- AutomationLogger - Logging & diagnostics

✅ **Tests**
- Unit tests for components
- Integration tests
- Manual interaction test
- GTH-specific test script

✅ **Documentation**
- GTH_INTEGRATION.md - How to use with GTH
- README.md - Full documentation
- CAPTCHA_TESTING.md - CAPTCHA testing guide
- QUICK_REFERENCE.md - Commands & API

## Testing GTH Features

### CAPTCHA Integration

```powershell
npm run test:manual
```

This:
1. Navigates to your GTH test page
2. Detects CAPTCHA
3. Waits while you solve it manually
4. Verifies completion
5. Logs results with screenshots

### Custom GTH Workflows

Create interaction sequences for GTH-specific testing:

```javascript
const steps = [
  { action: 'navigate', url: 'http://localhost:8080/login' },
  { action: 'type', selector: '#email', text: 'test@example.com' },
  { action: 'click', selector: '#login-button' },
  { action: 'waitForCaptcha' },
  { action: 'waitForCaptchaCompletion' },
  { action: 'screenshot', name: 'gth-logged-in' },
];

await session.executeInteractionSequence(steps);
```

## Files & Structure

```
tests/browser-automation/
├── src/                         Framework code
│   ├── index.js                Main orchestrator
│   ├── config.js               Configuration
│   ├── BrowserManager.js       Browser control
│   ├── PageController.js       Page interactions
│   ├── CaptchaMonitor.js       CAPTCHA detection
│   ├── SessionManager.js       Session management
│   └── AutomationLogger.js     Logging
├── tests/                       Test suites
│   ├── CaptchaMonitor.test.js  Unit tests
│   └── integration.test.js     Integration tests
├── scripts/                     Test scripts
│   ├── manual-interaction-test.js
│   ├── captcha-workflow-test.js
│   ├── custom-interaction-sequence.js
│   └── gth-captcha-test.js     GTH-specific
├── logs/                        Test logs (auto-created)
├── screenshots/                 Screenshots (auto-created)
├── GTH_INTEGRATION.md          GTH setup guide
├── GTH_README.md               This file
├── README.md                   Full documentation
├── SETUP.md                    Installation guide
├── CAPTCHA_TESTING.md          CAPTCHA testing
├── QUICK_REFERENCE.md          Commands & API
├── .env.example                Configuration template
└── package.json                Dependencies
```

## Configuration for GTH

### GTH on Port 8080

```env
TARGET_URL=http://localhost:8080
HEADLESS=false
ALLOWED_HOSTS=localhost,127.0.0.1
CAPTCHA_COMPLETION_TIMEOUT=300000
```

### GTH with Custom Port

```env
TARGET_URL=http://localhost:3000
# ... other settings
```

### GTH Production Testing

```env
TARGET_URL=https://gth.example.com
HEADLESS=true
CAPTCHA_TEST_MODE=false
```

## Running Tests

### Manual Test (Interactive)

```powershell
npm run test:manual
```

Browser opens → You solve CAPTCHA → Results logged

### Automated Test

```powershell
npm run test:captcha
```

Uses test mode signals (no manual CAPTCHA solving)

### GTH Specific Test

```powershell
node scripts/gth-captcha-test.js
```

Runs full GTH CAPTCHA workflow

### Unit Tests

```powershell
npm run test
```

### Integration Tests

```powershell
npm run test:integration
```

## Logging & Debugging

### View Logs

```powershell
# Latest log
Get-Content (Get-ChildItem logs\ -Latest 1).FullName -Tail 50

# Or view directly
type logs\automation-*.log
```

### View Screenshots

```powershell
ii screenshots\
```

### Enable Debug Mode

```powershell
$env:DEBUG = "true"
$env:LOG_LEVEL = "debug"
npm run test:manual
```

### Keep Browser Open

```powershell
$env:KEEP_BROWSER_OPEN = "true"
npm run test:manual
```

## Integration with GTH Build

Add to GTH's build/test script:

```powershell
# In your GTH build.ps1 or test runner
Write-Host "Testing GTH CAPTCHA integration..."
Push-Location tests\browser-automation

# Install dependencies if needed
if (-not (Test-Path node_modules)) {
  npm install
}

# Run GTH CAPTCHA test
node scripts/gth-captcha-test.js
$testResult = $LASTEXITCODE

Pop-Location

if ($testResult -ne 0) {
  Write-Error "GTH CAPTCHA test failed"
  exit 1
}

Write-Host "✓ GTH CAPTCHA test passed"
```

## API Usage

### Simple CAPTCHA Test

```javascript
import GTHBrowserAutomation from './tests/browser-automation/src/index.js';

const automation = new GTHBrowserAutomation({
  target: { targetUrl: 'http://localhost:8080' },
});

const result = await automation.runCaptchaTest();
console.log(result);
```

### Custom Interaction

```javascript
import GTHBrowserAutomation from './tests/browser-automation/src/index.js';

const automation = new GTHBrowserAutomation();
await automation.initialize();
const session = automation.getSessionManager();

const steps = [
  { action: 'navigate', url: 'http://localhost:8080' },
  { action: 'click', selector: '.transfer-btn' },
  { action: 'waitForElement', selector: '.transfer-form' },
  { action: 'screenshot', name: 'gth-transfer' },
];

const results = await session.executeInteractionSequence(steps);
await automation.cleanup();
```

## Common GTH Test Scenarios

### GTH Login Flow

```javascript
const steps = [
  { action: 'navigate', url: 'http://localhost:8080' },
  { action: 'click', selector: '.login-link' },
  { action: 'type', selector: '#email', text: 'test@example.com' },
  { action: 'type', selector: '#password', text: 'password' },
  { action: 'click', selector: '#login-btn' },
  { action: 'waitForCaptcha' },
  { action: 'waitForCaptchaCompletion' },
  { action: 'waitForElement', selector: '.dashboard' },
  { action: 'screenshot', name: 'gth-dashboard' },
];
```

### GTH Transfer Initiation

```javascript
const steps = [
  { action: 'navigate', url: 'http://localhost:8080/transfers' },
  { action: 'click', selector: '.new-transfer' },
  { action: 'waitForElement', selector: '.transfer-form' },
  { action: 'type', selector: '#source', text: 'C:\\Games' },
  { action: 'type', selector: '#destination', text: 'D:\\Transfer' },
  { action: 'screenshot', name: 'gth-transfer-form' },
];
```

### GTH Settings Update

```javascript
const steps = [
  { action: 'navigate', url: 'http://localhost:8080/settings' },
  { action: 'click', selector: '.settings-tab' },
  { action: 'type', selector: '#username', text: 'newusername' },
  { action: 'click', selector: '#save-button' },
  { action: 'waitForElement', selector: '.success-message' },
  { action: 'screenshot', name: 'gth-settings-saved' },
];
```

## Troubleshooting

### GTH Server Won't Connect

1. Ensure GTH is running on configured port
2. Check `TARGET_URL` in `.env`
3. Verify port is correct: `http://localhost:8080`

### CAPTCHA Not Detected

1. Add `data-captcha` to CAPTCHA element in GTH
2. Check browser console for JavaScript errors
3. Increase timeout: `CAPTCHA_DETECTION_TIMEOUT=30000`

### Test Timeout

Increase timeouts in `.env`:
```env
NAVIGATION_TIMEOUT=60000
CAPTCHA_COMPLETION_TIMEOUT=600000
```

### Browser Won't Open

```env
HEADLESS=false
DEBUG=true
```

Check `logs/` for details.

## Documentation

For detailed information:

- **GTH_INTEGRATION.md** - Complete GTH integration guide
- **README.md** - Full framework documentation
- **CAPTCHA_TESTING.md** - CAPTCHA testing strategies
- **QUICK_REFERENCE.md** - Commands, configuration, API
- **SETUP.md** - Installation & troubleshooting
- **PROJECT_STRUCTURE.md** - Architecture & components

## Next Steps

1. **Install:** `npm install`
2. **Configure:** Edit `.env` with GTH settings
3. **Test:** `npm run test:manual`
4. **Integrate:** Add to GTH's build pipeline
5. **Automate:** Use in CI/CD

## Support

- **Logs:** `logs/automation-*.log`
- **Screenshots:** `screenshots/`
- **Debug:** Set `DEBUG=true` in `.env`
- **Documentation:** See files listed above

---

**Quick commands:**
```powershell
npm install                    # Install dependencies
npm run test:manual           # Interactive test
npm run test:captcha          # Automated test
npm run test                  # Unit tests
node scripts/gth-captcha-test.js  # GTH-specific test
```
