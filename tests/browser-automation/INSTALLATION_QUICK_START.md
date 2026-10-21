# Installation & Quick Start (5 Minutes)

Get the browser automation framework running in 5 minutes.

## Prerequisites

- Node.js 18+ installed
- Your test page running (e.g., on localhost:8080)

## Installation (2 minutes)

### 1. Install Dependencies

```powershell
cd C:\Users\DrGame\Desktop\research\browser-automation
npm install
```

Expected output:
```
added XX packages in Xs
```

### 2. Create Configuration

```powershell
Copy-Item .env.example .env
```

### 3. Edit Configuration

```powershell
notepad .env
```

Minimum required changes:

```env
# YOUR TEST PAGE URL - REQUIRED
TARGET_URL=http://localhost:8080

# SHOW BROWSER WINDOW
HEADLESS=false

# ALLOWED DOMAINS
ALLOWED_HOSTS=localhost,127.0.0.1
```

Save and close.

## First Test (3 minutes)

### Run Manual CAPTCHA Test

```powershell
npm run test:manual
```

**What happens:**
1. Browser opens and navigates to your test page
2. Script waits for CAPTCHA to appear
3. If CAPTCHA found:
   - Automation pauses
   - You manually complete CAPTCHA
   - Automation detects completion
   - Test reports success
4. Results saved to `./logs/` and `./screenshots/`

### Check Results

```powershell
# View latest log
Get-Content (Get-ChildItem .\logs\ -Latest 1).FullName

# Open screenshots
ii .\screenshots\
```

## Common Next Steps

### If Test Passed ✓
Great! The framework is working. Now you can:
- Customize configuration in `.env`
- Create custom interaction sequences
- Integrate with your CI/CD pipeline

### If CAPTCHA Not Detected ✗
Your CAPTCHA element needs a marker. Add to your HTML:
```html
<div data-captcha>
  <!-- Your CAPTCHA component -->
</div>
```

Then run test again.

### If Test Timed Out ⏱️
Increase timeout in `.env`:
```env
CAPTCHA_COMPLETION_TIMEOUT=600000
```

Then run test again.

## Configuration Quick Reference

```env
# Network
TARGET_URL=http://localhost:8080           # Your test page
HEADLESS=false                             # Show browser

# CAPTCHA timeouts (milliseconds)
CAPTCHA_DETECTION_TIMEOUT=15000            # Wait for CAPTCHA (15 sec)
CAPTCHA_COMPLETION_TIMEOUT=300000          # Wait for solve (5 min)

# Logging
LOG_LEVEL=info                             # debug, info, warn, error
DEBUG=false                                # Verbose output

# Interaction timing (milliseconds)
INTERACTION_DELAY=100                      # Delay between actions
MOUSE_MOVE_DELAY=300                       # Mouse move duration
CLICK_WAIT_DELAY=200                       # Wait after click
```

## Using the Framework

### Simple Test
```javascript
import GTHBrowserAutomation from './src/index.js';

const automation = new GTHBrowserAutomation();
const result = await automation.runCaptchaTest();
console.log(result);
```

### Custom Interactions
```javascript
const automation = new GTHBrowserAutomation();
await automation.initialize();
const session = automation.getSessionManager();

const steps = [
  { action: 'navigate', url: 'http://localhost:8080' },
  { action: 'click', selector: '#login-btn' },
  { action: 'waitForCaptcha' },
  { action: 'waitForCaptchaCompletion' },
  { action: 'screenshot', name: 'final' },
];

await session.executeInteractionSequence(steps);
await automation.cleanup();
```

## Available Commands

```powershell
# Run manual test (browser visible)
npm run test:manual

# Run automated workflow test (test mode)
npm run test:captcha

# Run unit tests
npm run test

# Run integration tests
npm run test:integration

# Watch tests
npm run test:watch
```

## Troubleshooting

### Browser Won't Open
```powershell
# Set these in .env
HEADLESS=false
DEBUG=true

# Run test and check logs
npm run test:manual
type .\logs\*.log | tail -50
```

### CAPTCHA Not Found
Add marker to your CAPTCHA HTML:
```html
<div data-captcha><!-- CAPTCHA --></div>
```

### Test Hangs/Timeouts
Edit `.env`:
```env
CAPTCHA_COMPLETION_TIMEOUT=600000
CAPTCHA_DETECTION_TIMEOUT=30000
```

### Chrome Not Found
```powershell
# Option 1: Install Chrome
# https://www.google.com/chrome/

# Option 2: Use custom path in .env
BROWSER_EXECUTABLE_PATH=C:\Program Files\Google\Chrome\Application\chrome.exe

# Option 3: Install Playwright browsers
npx playwright install chromium
```

## Project Structure

```
browser-automation/
├── src/              ← Framework source code
├── tests/            ← Test files
├── scripts/          ← Test scripts
├── logs/             ← Test logs (auto-created)
├── screenshots/      ← Screenshots (auto-created)
├── .env              ← Your configuration
├── package.json      ← Dependencies
└── README.md         ← Full documentation
```

## Important Files

| File | Purpose |
|------|---------|
| `.env` | Your configuration |
| `src/index.js` | Main framework entry |
| `scripts/manual-interaction-test.js` | Manual test script |
| `logs/automation-*.log` | Test logs |
| `screenshots/` | Screenshot storage |

## Typical Workflow

1. **Setup** (one time)
   ```powershell
   npm install
   Copy-Item .env.example .env
   notepad .env
   ```

2. **Test** (each time)
   ```powershell
   npm run test:manual
   ```

3. **Review**
   ```powershell
   # Check logs
   Get-Content .\logs\*.log -Tail 20
   
   # View screenshots
   ii .\screenshots\
   ```

4. **Iterate**
   - Update configuration or code
   - Run test again
   - Verify results

## Need Help?

### Check These Files for Answers

| Question | File |
|----------|------|
| How do I install? | `SETUP.md` |
| How do I use it? | `README.md` |
| CAPTCHA specific issues? | `CAPTCHA_TESTING.md` |
| Commands & configuration? | `QUICK_REFERENCE.md` |
| Architecture details? | `PROJECT_STRUCTURE.md` |

### Check Logs

Every test creates a detailed log:
```powershell
# Latest log
$log = (Get-ChildItem .\logs\ -Latest 1).FullName
Get-Content $log | tail -100
```

### Enable Debug Mode

```powershell
$env:DEBUG = "true"
$env:LOG_LEVEL = "debug"
npm run test:manual
```

## What's Included

✓ **5 Core Components**
- BrowserManager
- PageController
- CaptchaMonitor
- SessionManager
- AutomationLogger

✓ **3 Test Modes**
- Manual (browser visible, you solve CAPTCHA)
- Automated (test mode signals)
- Custom sequences

✓ **Comprehensive Documentation**
- Installation guide
- API documentation
- CAPTCHA testing guide
- Quick reference
- Troubleshooting

✓ **Logging & Diagnostics**
- Timestamped logs
- Screenshots on error
- State tracking
- Performance metrics

✓ **Security**
- Host validation
- Configuration management
- No CAPTCHA bypass
- Test signals only

## Next Steps

1. ✅ Install and run first test
2. 📖 Read `README.md` for full documentation
3. ⚙️ Customize `.env` for your setup
4. 🎯 Create custom test sequences
5. 🔄 Integrate into your CI/CD pipeline

## Support

- **Logs:** Check `./logs/automation-*.log`
- **Screenshots:** Check `./screenshots/`
- **Debug:** Set `DEBUG=true` in `.env`
- **Docs:** See files listed above

## Quick Commands Reference

```powershell
# Install
npm install

# Test
npm run test:manual
npm run test:captcha

# Debug
$env:DEBUG="true"
npm run test:manual

# View logs
Get-Content .\logs\*.log -Tail 50

# View screenshots
ii .\screenshots\
```

---

**You're ready to go!** Run `npm run test:manual` and start testing.
