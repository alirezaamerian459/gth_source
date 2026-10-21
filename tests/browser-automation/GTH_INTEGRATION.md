# GTH Browser Automation Integration

This browser automation framework is integrated into the GTH testing suite at `tests/browser-automation/`.

## Quick Start

### 1. Install Dependencies

From the `tests/browser-automation/` directory:

```powershell
cd C:\Users\DrGame\Desktop\local\tests\browser-automation
npm install
```

### 2. Configure for GTH

Copy and customize the configuration:

```powershell
Copy-Item .env.example .env
```

Edit `.env` with GTH test settings:

```env
# Your GTH test server
TARGET_URL=http://localhost:8080

# Show browser for interactive testing
HEADLESS=false

# Allowed domains (adjust for your GTH setup)
ALLOWED_HOSTS=localhost,127.0.0.1,gth.local,gth-test.internal

# Logging
LOG_LEVEL=info
```

### 3. Run Tests

```powershell
# Manual test (browser visible)
npm run test:manual

# Automated workflow test
npm run test:captcha

# Unit tests
npm run test

# Integration tests
npm run test:integration
```

## GTH Integration Points

### For GTH's CAPTCHA Testing

The framework automatically detects and tests CAPTCHA integration:

1. **Detection** - Finds CAPTCHA elements on your GTH pages
2. **Manual Completion** - Waits while you solve CAPTCHA manually
3. **Automation** - Verifies completion and logs results
4. **Screenshots** - Captures state at each step

### For GTH's CI/CD Pipeline

Add to your build/test script:

```powershell
# In your GTH build script
cd tests\browser-automation
npm install
npm run test:captcha
if ($LASTEXITCODE -ne 0) {
  Write-Error "Browser automation tests failed"
  exit 1
}
```

### For GTH's Custom Testing

Use the framework in your own GTH test scripts:

```javascript
import GTHBrowserAutomation from './tests/browser-automation/src/index.js';

async function testGTHCaptcha() {
  const automation = new GTHBrowserAutomation({
    browser: { headless: false },
    target: {
      targetUrl: 'http://localhost:8080/gth-login',
    },
  });

  try {
    const result = await automation.runCaptchaTest();
    console.log('GTH CAPTCHA test:', result);
    return result.success;
  } finally {
    await automation.cleanup();
  }
}
```

## Configuration for GTH

### Default GTH Test Configuration

If you're running GTH's web interface locally on port 8080:

```env
TARGET_URL=http://localhost:8080
HEADLESS=false
ALLOWED_HOSTS=localhost,127.0.0.1

# GTH-specific timeouts
CAPTCHA_COMPLETION_TIMEOUT=300000
LOG_LEVEL=info
```

### GTH with Custom Port

If GTH runs on a different port:

```env
TARGET_URL=http://localhost:3000
# ... other settings
```

### GTH with Authentication

For GTH pages that require login, create a custom sequence:

```javascript
const steps = [
  { action: 'navigate', url: 'http://localhost:8080/login' },
  { action: 'type', selector: '#username', text: 'testuser' },
  { action: 'type', selector: '#password', text: 'password' },
  { action: 'click', selector: '#login-button' },
  { action: 'waitForElement', selector: '.dashboard' },
  { action: 'waitForCaptcha' },
  { action: 'waitForCaptchaCompletion' },
  { action: 'screenshot', name: 'gth-authenticated' },
];

await session.executeInteractionSequence(steps);
```

## Files & Structure

```
tests/
└── browser-automation/
    ├── src/                    # Framework modules
    ├── tests/                  # Test suites
    ├── scripts/                # Test scripts
    ├── logs/                   # Test logs (auto-created)
    ├── screenshots/            # Screenshots (auto-created)
    ├── .env.example           # Configuration template
    ├── package.json           # Dependencies
    ├── README.md              # Full documentation
    ├── GTH_INTEGRATION.md     # This file
    └── [other documentation]
```

## Running from GTH

### From GTH's Root

```powershell
# Navigate to browser automation tests
cd tests\browser-automation

# Run test
npm run test:manual

# View results
type logs\automation-*.log
```

### From GTH's Build Script

Add to your `build.ps1` or test runner:

```powershell
Write-Host "Running browser automation tests..."
Push-Location tests\browser-automation

if (-not (Test-Path node_modules)) {
  npm install
}

npm run test:captcha
$testResult = $LASTEXITCODE

Pop-Location

if ($testResult -ne 0) {
  Write-Error "Browser automation tests failed"
  exit 1
}

Write-Host "✓ Browser automation tests passed"
```

## Debugging GTH Tests

### Enable Debug Mode

```powershell
$env:DEBUG = "true"
$env:LOG_LEVEL = "debug"
npm run test:manual
```

### Check Logs

Logs are saved to `logs/automation-*.log`:

```powershell
Get-Content (Get-ChildItem logs\ -Latest 1).FullName -Tail 50
```

### View Screenshots

Screenshots are saved to `screenshots/`:

```powershell
ii screenshots\
```

### Keep Browser Open

```powershell
$env:KEEP_BROWSER_OPEN = "true"
npm run test:manual
```

## Common GTH Test Scenarios

### Test GTH Login with CAPTCHA

```javascript
const automation = new GTHBrowserAutomation();
await automation.initialize();
const session = automation.getSessionManager();

const steps = [
  { action: 'navigate', url: 'http://localhost:8080' },
  { action: 'click', selector: '.login-button' },
  { action: 'type', selector: '#email', text: 'test@example.com' },
  { action: 'type', selector: '#password', text: 'testpass' },
  { action: 'click', selector: '#login' },
  { action: 'waitForCaptcha' },
  { action: 'waitForCaptchaCompletion' },
  { action: 'waitForElement', selector: '.dashboard' },
  { action: 'screenshot', name: 'gth-logged-in' },
];

const results = await session.executeInteractionSequence(steps);
await automation.cleanup();
```

### Test GTH Transfer Page

```javascript
const automation = new GTHBrowserAutomation();
await automation.initialize();
const session = automation.getSessionManager();

const steps = [
  { action: 'navigate', url: 'http://localhost:8080/transfers' },
  { action: 'waitForElement', selector: '.transfer-list' },
  { action: 'click', selector: '.new-transfer-btn' },
  { action: 'waitForElement', selector: '.transfer-form' },
  { action: 'screenshot', name: 'gth-transfer-form' },
];

await session.executeInteractionSequence(steps);
await automation.cleanup();
```

## Troubleshooting GTH Tests

### Port Already in Use

If GTH won't start on the configured port:

1. Check if another instance is running
2. Change port in `.env`
3. Verify TARGET_URL

### CAPTCHA Not Detected

1. Ensure CAPTCHA element has `data-captcha` attribute
2. Check browser console for JavaScript errors
3. Increase detection timeout: `CAPTCHA_DETECTION_TIMEOUT=30000`

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

Then check `logs/` for errors.

## Next Steps

1. **Install:** `npm install`
2. **Configure:** Edit `.env` for your GTH setup
3. **Test:** `npm run test:manual`
4. **Integrate:** Add to GTH's build/test pipeline
5. **Automate:** Use in CI/CD

## Documentation

For detailed information, see:

- **README.md** - Full framework documentation
- **CAPTCHA_TESTING.md** - CAPTCHA testing strategies
- **QUICK_REFERENCE.md** - Commands & API reference
- **SETUP.md** - Installation & troubleshooting
- **PROJECT_STRUCTURE.md** - Architecture details

## Support

- Check `logs/` for test logs
- View `screenshots/` for visual state
- Enable DEBUG mode for verbose output
- See documentation files for detailed help
