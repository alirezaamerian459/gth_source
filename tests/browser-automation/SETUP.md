# Setup & Installation Guide

Complete guide to setting up the GTH Browser Automation Framework.

## Prerequisites

### System Requirements

- **OS:** Windows 10/11 Pro or Server
- **Node.js:** 18.0.0 or higher
- **npm:** 9.0.0 or higher
- **Memory:** 2GB minimum (4GB recommended)
- **Disk Space:** 1GB for Playwright browsers + logs/screenshots

### Verify Prerequisites

```powershell
node --version
npm --version
```

Should output:
```
v18.x.x (or higher)
9.x.x (or higher)
```

## Installation Steps

### 1. Navigate to Project Directory

```powershell
cd C:\Users\DrGame\Desktop\research\browser-automation
```

### 2. Install Node Dependencies

```powershell
npm install
```

This installs:
- `playwright` (~300MB) - Browser automation framework
- `dotenv` - Configuration management

**Expected output:**
```
added XX packages in Xs
```

**First run:** Playwright automatically downloads Chromium (~150MB)

### 3. Configure Environment

Copy the example configuration:

```powershell
Copy-Item .env.example .env
```

Edit `.env` with your test configuration. Minimal configuration:

```env
TARGET_URL=http://localhost:8080
HEADLESS=false
ALLOWED_HOSTS=localhost,127.0.0.1
```

### 4. Verify Installation

Test that everything works:

```powershell
npm run test:manual
```

This should:
1. Launch a browser window
2. Navigate to your TARGET_URL
3. Wait for CAPTCHA (or show "No CAPTCHA detected")
4. Exit gracefully

**If browser doesn't open:**
1. Check `HEADLESS` is set to `false`
2. Check logs in `./logs/`
3. Verify `TARGET_URL` is correct

## Directory Structure After Setup

After successful installation, you'll have:

```
browser-automation/
├── node_modules/          # Installed dependencies
├── src/                   # Source code
├── tests/                 # Test files
├── scripts/               # Test scripts
├── logs/                  # Test logs (auto-created)
├── screenshots/           # Test screenshots (auto-created)
├── session-data/          # Browser session data (auto-created)
├── .env                   # Your configuration (created from .env.example)
└── package.json
```

## Configuration Options

### Essential Configuration

**Minimum `.env` for testing:**

```env
# Where to test
TARGET_URL=http://localhost:8080

# Show browser window (false for headless)
HEADLESS=false

# Allowed test domains (security boundary)
ALLOWED_HOSTS=localhost,127.0.0.1,gth-test.internal

# CAPTCHA completion timeout (5 minutes for manual)
CAPTCHA_COMPLETION_TIMEOUT=300000

# Logging
LOG_LEVEL=info
```

### Browser Configuration

```env
# Chrome executable path (leave empty for bundled Chromium)
BROWSER_EXECUTABLE_PATH=C:\Program Files\Google\Chrome\Application\chrome.exe

# Window size
VIEWPORT_WIDTH=1280
VIEWPORT_HEIGHT=720

# Keep browser session across navigations
PERSISTENT_CONTEXT=true

# User data directory
USER_DATA_DIR=./session-data
```

### Timing Configuration

```env
# Navigation timeout (30 seconds)
NAVIGATION_TIMEOUT=30000

# Page ready timeout (10 seconds)
PAGE_READY_TIMEOUT=10000

# CAPTCHA detection timeout (15 seconds)
CAPTCHA_DETECTION_TIMEOUT=15000

# Mouse move duration (realistic interaction)
MOUSE_MOVE_DELAY=300

# Delay between interactions
INTERACTION_DELAY=100
```

### Logging & Debugging

```env
# Log level: debug, info, warn, error
LOG_LEVEL=info

# Capture screenshots on errors
SCREENSHOT_ON_ERROR=true

# Verbose debug output
DEBUG=false

# Keep browser open after test (for debugging)
KEEP_BROWSER_OPEN=false
```

## First Test Run

### Step 1: Start Your Test Server

Make sure your test page is running at `TARGET_URL`:

```powershell
# Example: if running a local server
npm start  # or your server startup command
```

Verify URL is accessible:
```powershell
curl http://localhost:8080
```

### Step 2: Run Manual Test

```powershell
npm run test:manual
```

**Expected behavior:**
1. Browser window opens
2. Navigates to TARGET_URL
3. Waits for CAPTCHA (if present on page)
4. If CAPTCHA is present: waits for you to complete it manually
5. Detects completion or timeout
6. Browser closes
7. Logs show success/failure

**Output example:**
```
🚀 Starting manual CAPTCHA interaction test...

📍 Target URL: http://localhost:8080
⏱️  CAPTCHA timeout: 300000ms

📄 Navigating to test page...
✓ Page loaded

🔍 Waiting for CAPTCHA...
✓ CAPTCHA detected

============================================================
👤 Please complete the CAPTCHA manually
⏱️  Waiting up to 300 seconds...
============================================================

[wait for user to complete CAPTCHA in browser]

✓ CAPTCHA completed successfully in 15234ms

📋 Log file: ./logs/automation-2025-09-21_143022-431.log
Session ID: 2025-09-21_143022-431
```

### Step 3: Check Results

After test completes:

1. **Logs:** `./logs/automation-*.log`
   ```powershell
   type .\logs\automation-*.log | tail -50
   ```

2. **Screenshots:** `./screenshots/`
   ```powershell
   ls .\screenshots\
   ```

3. **Success:** Look for "TEST PASSED" message

## Troubleshooting Installation

### Issue: Node/npm not found

**Error:** `'node' is not recognized as an internal or external command`

**Solution:**
1. Install Node.js from https://nodejs.org/ (LTS version)
2. Add Node to PATH during installation
3. Restart PowerShell/terminal
4. Verify: `node --version`

### Issue: npm install fails

**Error:** `npm ERR! Cannot find module`

**Solution:**
1. Delete `node_modules` folder: `rm -r node_modules`
2. Clear npm cache: `npm cache clean --force`
3. Reinstall: `npm install`

### Issue: Playwright download fails

**Error:** `Error: Timeout downloading Chromium`

**Solution:**
1. Ensure internet connection is stable
2. Manually download: `npx playwright install chromium`
3. Set proxy if behind corporate firewall:
   ```powershell
   npm config set proxy http://proxy.company.com:8080
   npm config set https-proxy http://proxy.company.com:8080
   ```

### Issue: Permission denied on .env

**Error:** `Permission denied: ./.env`

**Solution:** Create .env with explicit content:
```powershell
@'
TARGET_URL=http://localhost:8080
HEADLESS=false
ALLOWED_HOSTS=localhost,127.0.0.1
'@ | Out-File -Encoding UTF8 .env
```

## Advanced Installation

### Using Custom Chrome Installation

If you have Chrome installed elsewhere:

1. Find Chrome executable:
   ```powershell
   Get-Command chrome  # or
   ls "C:\Program Files*\Google\Chrome\Application\chrome.exe"
   ```

2. Set in `.env`:
   ```env
   BROWSER_EXECUTABLE_PATH=C:\Program Files\Google\Chrome\Application\chrome.exe
   ```

### Using Chromium Instead of Chrome

Playwright uses Chromium by default. To use Chrome:

1. Install Chrome: https://www.google.com/chrome/
2. Set path in `.env`:
   ```env
   BROWSER_EXECUTABLE_PATH=C:\Program Files\Google\Chrome\Application\chrome.exe
   ```

### Network Proxy Configuration

If behind corporate proxy:

```powershell
npm config set proxy http://proxy.company.com:8080
npm config set https-proxy http://proxy.company.com:8080
npm install
```

Or set environment variables:

```powershell
$env:HTTP_PROXY="http://proxy.company.com:8080"
$env:HTTPS_PROXY="http://proxy.company.com:8080"
npm install
```

## Integrating with GTH

To use this automation in your GTH testing pipeline:

1. **Copy to GTH directory:**
   ```powershell
   Copy-Item -Recurse browser-automation C:\Users\DrGame\Desktop\local\tests\browser-automation
   ```

2. **Add to GTH build:**
   - Link from GTH's test runner
   - Call `npm run test:captcha` during CI/CD

3. **Use in GTH code:**
   ```javascript
   import GTHBrowserAutomation from '../tests/browser-automation/src/index.js';
   const automation = new GTHBrowserAutomation();
   await automation.runCaptchaTest();
   ```

## Running Tests from CI/CD

### Example: GitHub Actions

```yaml
name: CAPTCHA Tests
on: [push, pull_request]

jobs:
  captcha-test:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Setup Node
        uses: actions/setup-node@v2
        with:
          node-version: '18'
      
      - name: Install Playwright
        working-directory: browser-automation
        run: npm install
      
      - name: Run CAPTCHA Test
        working-directory: browser-automation
        env:
          TARGET_URL: http://localhost:8080
          HEADLESS: true
          CAPTCHA_TEST_MODE: true
        run: npm run test:captcha
      
      - name: Upload Logs
        if: always()
        uses: actions/upload-artifact@v2
        with:
          name: automation-logs
          path: browser-automation/logs/
```

## Next Steps

1. **Run your first test:** `npm run test:manual`
2. **Customize configuration:** Edit `.env` for your setup
3. **Create custom sequences:** See examples in `scripts/`
4. **Integrate with GTH:** Link in your build pipeline
5. **Set up CI/CD:** Automate tests in your workflow

## Getting Help

### Check Logs

Detailed logs are saved to `./logs/`:

```powershell
# Show latest log
Get-Content (Get-ChildItem .\logs\ | Sort-Object LastWriteTime | Select-Object -Last 1).FullName -Tail 50
```

### Enable Debug Mode

```env
DEBUG=true
LOG_LEVEL=debug
```

### Take Screenshots

Screenshots are auto-saved to `./screenshots/` on errors. View them:

```powershell
ii .\screenshots\  # Opens in Windows Explorer
```

### Check Dependencies

```powershell
npm list playwright
npm list
```

## Support Resources

- **Playwright Docs:** https://playwright.dev/
- **Node.js Docs:** https://nodejs.org/docs/
- **GTH Issues:** See memory files for context

## Uninstallation

To remove the automation framework:

```powershell
# Remove dependencies
rm -r node_modules

# Remove cache
npm cache clean --force

# Remove logs/screenshots (optional)
rm -r logs screenshots session-data
```
