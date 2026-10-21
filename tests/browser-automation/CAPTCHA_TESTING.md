# CAPTCHA Testing Guide

Comprehensive guide to testing CAPTCHA integration with the GTH Browser Automation Framework.

## Overview

The automation framework supports two CAPTCHA testing modes:

1. **Manual Mode** - Automation pauses while you manually complete the CAPTCHA
2. **Test Mode** - Automation looks for test-mode signals from your implementation

## Manual Mode (Recommended for Development)

Manual mode is ideal during development when you want to test your actual CAPTCHA implementation.

### How It Works

1. Browser automation navigates to your test page
2. Detects CAPTCHA is present
3. **Pauses automation**
4. Waits for you to manually interact with and complete the CAPTCHA
5. Detects completion automatically
6. Resumes automation
7. Logs completion status and timing

### Configuration

Minimal `.env` configuration:

```env
TARGET_URL=http://localhost:8080/captcha-test
HEADLESS=false
CAPTCHA_COMPLETION_TIMEOUT=300000
```

### Running Manual Test

```powershell
npm run test:manual
```

### Step-by-Step Interaction

1. **Browser Opens** - You see the test page with CAPTCHA
2. **Automation Detects CAPTCHA**
   ```
   🔍 Waiting for CAPTCHA...
   ✓ CAPTCHA detected
   ```

3. **Your Turn** - Complete the CAPTCHA in the browser
   ```
   ============================================================
   👤 Please complete the CAPTCHA manually
   ⏱️  Waiting up to 300 seconds...
   ============================================================
   ```

4. **Automation Detects Completion** - After you solve it
   ```
   ✓ CAPTCHA completed successfully in 15234ms
   ```

5. **Results Logged** - Check `./logs/` for details

### CAPTCHA Detection

The framework detects CAPTCHA using common selectors:

```javascript
// Checked in order:
'[data-captcha]'                          // Data attribute
'[data-recaptcha]'                        // reCAPTCHA
'[class*="captcha"]'                      // CSS class
'[id*="captcha"]'                         // ID attribute
'.g-recaptcha'                            // Google reCAPTCHA
'.h-captcha'                              // hCaptcha
'[data-hcaptcha]'                         // hCaptcha data
'iframe[title*="captcha" i]'              // CAPTCHA iframe
'iframe[src*="recaptcha" i]'              // reCAPTCHA iframe
'iframe[src*="hcaptcha" i]'               // hCaptcha iframe
```

**If your CAPTCHA isn't detected:**

1. Add `[data-captcha]` to your CAPTCHA element:
   ```html
   <div id="my-captcha" data-captcha>
     <!-- CAPTCHA container -->
   </div>
   ```

2. Or update config in `.env`:
   ```env
   # Check specific selector
   CAPTCHA_DETECTION_TIMEOUT=30000
   ```

### Completion Detection

After you manually complete the CAPTCHA, the framework looks for:

1. **CAPTCHA element removal** - Element is hidden/removed after success
2. **Success attribute** - `[data-captcha-success]` appears
3. **Completion attribute** - `[data-captcha-completed]` appears

**If completion isn't detected:**

Add a signal to your CAPTCHA completion handler:

```javascript
// When CAPTCHA is successfully completed:
document.querySelector('.captcha-container').setAttribute('data-captcha-success', 'true');

// Or add hidden element:
const signal = document.createElement('div');
signal.setAttribute('data-captcha-success', 'true');
document.body.appendChild(signal);
```

### Manual Mode Example

Complete workflow test:

```bash
# Set configuration
$env:HEADLESS = "false"
$env:TARGET_URL = "http://localhost:8080/captcha-test"
$env:CAPTCHA_COMPLETION_TIMEOUT = "300000"

# Run test
npm run test:manual

# Results appear in ./logs/ and ./screenshots/
```

## Test Mode (For Automated Testing)

Test mode uses signals from your implementation to simulate CAPTCHA completion without manual interaction.

### Configuration

Enable test mode in `.env`:

```env
CAPTCHA_TEST_MODE=true
CAPTCHA_SUCCESS_SELECTOR=[data-captcha-success]
CAPTCHA_COMPLETION_SELECTOR=[data-captcha-completed]
CAPTCHA_FAILURE_SELECTOR=[data-captcha-failed]
```

### Test Mode Signals

Your test page should expose these signals:

```html
<!-- CAPTCHA is present -->
<div data-captcha id="my-captcha"></div>

<!-- CAPTCHA was completed successfully -->
<div data-captcha-success hidden></div>

<!-- CAPTCHA completion (any status) -->
<div data-captcha-completed hidden></div>

<!-- CAPTCHA failed or expired -->
<div data-captcha-failed hidden></div>
```

### Simulating CAPTCHA Workflow in Tests

When testing, your page should:

1. Show CAPTCHA when loaded
2. After some delay or on button click, show success signal
3. Remove CAPTCHA or show completion message

Example test implementation:

```javascript
// On page load, show CAPTCHA
document.querySelector('#my-captcha').style.display = 'block';

// Simulate CAPTCHA completion after 2 seconds
setTimeout(() => {
  // Show success signal for automation to detect
  document.querySelector('[data-captcha-success]').hidden = false;
  
  // Also set completion signal
  document.querySelector('[data-captcha-completed]').hidden = false;
  
  // Optionally remove or hide CAPTCHA
  document.querySelector('#my-captcha').style.display = 'none';
}, 2000);
```

### Running Test Mode

```powershell
npm run test:captcha
```

This runs with test mode signals instead of waiting for manual completion.

## Production CAPTCHA Testing

### Strategy

For production CAPTCHAs (Google reCAPTCHA, hCaptcha, etc.):

1. **Development:** Use manual mode
   ```
   npm run test:manual
   ```
   You manually complete real CAPTCHA while automation watches

2. **CI/CD:** Use test mode
   ```
   CAPTCHA_TEST_MODE=true npm run test:captcha
   ```
   Use test environment with mocked/bypassed CAPTCHA

3. **Staging:** Use both
   - Manual tests for verification
   - Test mode for automated CI/CD pipeline

### Staging Environment Setup

Create a test endpoint that:

1. Shows CAPTCHA UI (visual test)
2. Has a "Skip for Testing" mode
3. Exposes test signals:

```html
<!-- test.html -->
<html>
<head>
  <title>CAPTCHA Test Page</title>
</head>
<body>
  <!-- CAPTCHA container -->
  <div id="captcha-container" data-captcha></div>
  
  <!-- Test signals (hidden) -->
  <div data-captcha-success hidden></div>
  <div data-captcha-completed hidden></div>
  
  <script>
    // Detect test mode from URL
    const isTestMode = new URL(window.location).searchParams.has('test');
    
    if (isTestMode) {
      // Hide real CAPTCHA, show test version
      document.querySelector('#captcha-container').innerHTML = 'Test Mode: CAPTCHA Skipped';
      
      // Simulate completion
      setTimeout(() => {
        document.querySelector('[data-captcha-success]').hidden = false;
        document.querySelector('[data-captcha-completed]').hidden = false;
      }, 500);
    } else {
      // Load real CAPTCHA
      // ... your actual CAPTCHA code
    }
  </script>
</body>
</html>
```

Configuration:

```env
# Staging with test mode
TARGET_URL=http://staging.gth.local/captcha-test?test=true
CAPTCHA_TEST_MODE=true

# Production - manual mode only
TARGET_URL=https://gth.example.com/login
CAPTCHA_TEST_MODE=false
```

## Debugging CAPTCHA Issues

### CAPTCHA Not Detected

**Problem:** Script reports "No CAPTCHA detected" but page has one

**Debug Steps:**

1. Enable debug logging:
   ```env
   DEBUG=true
   LOG_LEVEL=debug
   ```

2. Check logs:
   ```powershell
   Get-Content .\logs\*.log -Tail 50 | grep "CAPTCHA"
   ```

3. Check what selectors are found:
   ```env
   DEBUG=true
   # Logs will show which selectors were checked
   ```

4. Increase detection timeout:
   ```env
   CAPTCHA_DETECTION_TIMEOUT=30000
   ```

5. Add explicit marker to your CAPTCHA:
   ```html
   <div data-captcha><!-- your CAPTCHA --></div>
   ```

### Completion Not Detected

**Problem:** CAPTCHA is manually completed but not detected

**Debug Steps:**

1. Check success signals on page:
   ```javascript
   // In browser console after completing CAPTCHA:
   document.querySelector('[data-captcha-success]');
   document.querySelector('[data-captcha-completed]');
   document.querySelector('[data-captcha]');
   ```

2. If none exist, add them when completion handler fires:
   ```javascript
   // Your CAPTCHA completion handler:
   yourCaptchaLibrary.onSuccess(() => {
     // Add success signal
     const signal = document.createElement('div');
     signal.setAttribute('data-captcha-success', 'true');
     document.body.appendChild(signal);
   });
   ```

3. Check completion timeout:
   ```env
   CAPTCHA_COMPLETION_TIMEOUT=600000  # 10 minutes
   ```

4. Take screenshot to see state:
   ```powershell
   npm run test:manual
   # Browser stays open - check ./screenshots/
   ```

### Timeout Errors

**Problem:** Tests timeout before completing

**Causes & Solutions:**

1. **Navigation timeout:**
   ```env
   NAVIGATION_TIMEOUT=60000  # Increase from 30s to 60s
   ```

2. **CAPTCHA detection timeout:**
   ```env
   CAPTCHA_DETECTION_TIMEOUT=30000  # Increase from 15s to 30s
   ```

3. **CAPTCHA completion timeout:**
   ```env
   CAPTCHA_COMPLETION_TIMEOUT=600000  # Increase to 10 minutes
   ```

4. **Page ready timeout:**
   ```env
   PAGE_READY_TIMEOUT=20000  # Increase from 10s to 20s
   ```

### Browser Crashes

**Problem:** Browser closes during test

**Debug:**

1. Keep browser open:
   ```env
   KEEP_BROWSER_OPEN=true
   ```

2. Run test:
   ```powershell
   npm run test:manual
   ```

3. Check browser console for errors
4. Check automation logs: `./logs/*.log`

**Common causes:**
- Page has script errors
- JavaScript crashes on navigation
- Memory issues with large pages
- CAPTCHA library incompatibility

## Performance Metrics

Typical CAPTCHA test performance:

- **Detection time:** 200-500ms
- **Manual completion:** 5-30 seconds (user-dependent)
- **Automated completion:** 100-500ms (test mode)
- **Total workflow:** 10-45 seconds

## Best Practices

1. **Use Manual Mode for Development**
   - Test with actual CAPTCHA implementation
   - Verify real user flow
   - Catch timing issues

2. **Use Test Mode for CI/CD**
   - Automate without manual interaction
   - Consistent test runs
   - Fast feedback in pipeline

3. **Expose Test Signals**
   - Add `data-captcha` to container
   - Add `data-captcha-success` on completion
   - Make signals easy to detect

4. **Monitor Timing**
   - Check logs for performance regression
   - Alert on timeouts
   - Optimize slow CAPTCHAs

5. **Test Both Scenarios**
   - Success path (CAPTCHA solved)
   - Failure path (timeout/expiration)
   - Error handling

## Example Test Scenarios

### Scenario 1: Google reCAPTCHA v3

**Config:**
```env
TARGET_URL=http://localhost:8080/login
HEADLESS=false
CAPTCHA_TEST_MODE=false  # Real reCAPTCHA
```

**Process:**
1. Run `npm run test:manual`
2. Manually complete reCAPTCHA
3. Automation detects iframe removal
4. Test passes

### Scenario 2: Custom CAPTCHA with Test Mode

**Config:**
```env
TARGET_URL=http://localhost:8080/test?mode=automated
HEADLESS=true
CAPTCHA_TEST_MODE=true
```

**Process:**
1. Page loads in test mode
2. Shows fake CAPTCHA for UI testing
3. After 1 second, sets `data-captcha-success`
4. Automation detects and passes
5. CI/CD pipeline reports success

### Scenario 3: Failure Handling

**Config:**
```env
TARGET_URL=http://localhost:8080/captcha-fail
CAPTCHA_TEST_MODE=true
```

**Process:**
1. Page loads with CAPTCHA
2. After timeout, sets `data-captcha-failed`
3. Automation detects failure state
4. Test reports failure with timing
5. Logs saved for analysis

## Troubleshooting Checklist

- [ ] CAPTCHA element has `data-captcha` attribute
- [ ] Success handler adds `data-captcha-success` signal
- [ ] Timeouts configured appropriately
- [ ] Logs show detection attempts
- [ ] Screenshots show CAPTCHA state
- [ ] Test mode enabled for automated testing
- [ ] Manual mode used for development
- [ ] Browser window visible (HEADLESS=false)

## Getting Support

1. **Check logs:** `./logs/automation-*.log`
2. **Review screenshots:** `./screenshots/`
3. **Enable debug:** `DEBUG=true`, `LOG_LEVEL=debug`
4. **Test manually:** `npm run test:manual`
5. **Check selectors:** Inspect page with browser dev tools
